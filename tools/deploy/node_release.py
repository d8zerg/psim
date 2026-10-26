"""Apply a verified release to one node; persist each replacement before changing a process."""
import argparse
import fcntl
import json
import os
from pathlib import Path
import platform
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery import digest, migrate_config, require, verify


def run(*arguments):
    return subprocess.check_output(arguments, text=True, stderr=subprocess.STDOUT).strip()


def atomic(path, value, mode=0o600):
    path = Path(path)
    require(not path.is_symlink(), 'state destination is a symlink')
    descriptor, name = tempfile.mkstemp(prefix='.release-', dir=path.parent)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(value, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
        os.chmod(name, mode); os.replace(name, path)
        descriptor = os.open(path.parent, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(descriptor)
        finally: os.close(descriptor)
    finally:
        Path(name).unlink(missing_ok=True)


def preconditions(prefix, node, release):
    require(os.geteuid() == 0, 'release application requires root on the target node')
    require(platform.machine() == 'x86_64' and Path('/etc/os-release').read_text().find('VERSION_ID="13"') >= 0,
            'Debian 13 x86_64 is required')
    require(prefix.is_absolute() and len(prefix.parts) >= 3 and '..' not in prefix.parts,
            'invalid installation prefix')
    require(not any(path.is_symlink() for path in (prefix, *prefix.parents)), 'installation traverses a symlink')
    installed = json.loads((prefix / 'installation.json').read_text())
    require(installed['node'] == node, 'release target differs from the installed node')
    required = sum(path.stat().st_size for path in release.iterdir()) * 2 + 256 * 1024 * 1024
    require(shutil.disk_usage(prefix).free > required, 'insufficient space for staged artifacts and rollback')
    run('docker', 'compose', 'version')
    return installed


class Node:
    def __init__(self, prefix, node):
        self.prefix, self.node = Path(prefix), node
        self.directory = self.prefix / 'lifecycle'
        self.base = self.prefix / 'config' / node / 'compose.json'
        self.compose = json.loads(self.base.read_text())
        self.state_file = self.directory / 'state.json'

    def state(self):
        require(self.state_file.is_file() and not self.state_file.is_symlink(), 'release state is not initialized')
        state = json.loads(self.state_file.read_text())
        if state.get('identity_scheme') != 'sha256sums':
            # Upgrade retained release identifiers without changing active configuration paths.
            identities = {}
            for folder in (self.directory / 'releases').iterdir():
                if not folder.is_dir() or folder.name.startswith('.'): continue
                verify(folder / 'artifacts', self.directory / 'trusted.pub.pem')
                identity = digest(folder / 'artifacts/SHA256SUMS')
                identities[folder.name] = identity
                target = folder.parent / identity
                if not target.exists(): shutil.copytree(folder, target, copy_function=os.link)
            def convert(value):
                if isinstance(value, dict):
                    for key, item in value.items():
                        if key == 'release':
                            require(item in identities, 'legacy release identity has no retained signed artifacts')
                            value[key] = identities[item]
                        else: convert(item)
                elif isinstance(value, list):
                    for item in value: convert(item)
            convert(state)
            state['identity_scheme'] = 'sha256sums'; atomic(self.state_file, state)
        return state

    def initialize(self, release, trusted):
        manifest = verify(release, trusted)
        installed = preconditions(self.prefix, self.node, Path(release))
        if self.state_file.exists():
            require(self.state()['installation'] == installed, 'release installation identity differs')
            return self.state()
        require(not self.directory.exists(), 'lifecycle directory already exists without state')
        components = {}
        for name in self.compose['services'].keys() & manifest['components'].keys():
            container = self.container(name)
            image = json.loads(run('docker', 'inspect', container))[0]['Image']
            require(image in manifest['components'][name]['image'].get('digests', [manifest['components'][name]['image']['id']]), 'baseline image differs from running process: ' + name)
            native = subprocess.run(['systemctl', 'is-active', '--quiet', 'psim-' + name + '.service']).returncode == 0
            components[name] = {'release': digest(Path(release) / 'SHA256SUMS'), 'config': str(self.prefix / 'config' / self.node / 'services' / (name + '.json')),
                                'image': image, 'config_format': manifest['config_format'], 'runtime': 'systemd' if native else 'containers'}
        require(components, 'node has no release components')
        self.directory.mkdir(mode=0o755)
        (self.directory / 'releases').mkdir(mode=0o755)
        shutil.copyfile(trusted, self.directory / 'trusted.pub.pem')
        (self.directory / 'trusted.pub.pem').chmod(0o400)
        state = {'format': 1, 'identity_scheme': 'sha256sums', 'installation': installed, 'trusted_sha256': digest(trusted),
                 'components': components, 'operations': [], 'contract_floor': None}
        atomic(self.state_file, state)
        self.stage(release)
        return state

    def container(self, name, component=None):
        if component is None and self.state_file.exists(): component = self.state()['components'].get(name, {})
        if component and component.get('container'): return component['container']
        return run('docker', 'compose', '--file', str(self.base), 'ps', '--all', '--quiet', name)

    def stage(self, release):
        state = self.state()
        trusted = self.directory / 'trusted.pub.pem'
        require(digest(trusted) == state['trusted_sha256'], 'installation trust changed')
        identity = digest(Path(release) / 'SHA256SUMS')
        target = self.directory / 'releases' / identity
        if target.exists():
            require(digest(target / 'artifacts/SHA256SUMS') == identity, 'retained release inventory differs from its identity')
            manifest = verify(target / 'artifacts', trusted, selected_artifacts=())
            return identity, target, manifest
        manifest = verify(release, trusted)
        if not target.exists():
            temporary = Path(tempfile.mkdtemp(prefix='.stage-', dir=target.parent))
            try:
                shutil.copytree(release, temporary / 'artifacts')
                verify(temporary / 'artifacts', trusted)
                (temporary / 'config').mkdir(mode=0o755)
                temporary.chmod(0o755)
                temporary.rename(target)
            finally:
                if temporary.exists(): shutil.rmtree(temporary)
        verify(target / 'artifacts', trusted)
        return identity, target, manifest

    def overlay(self, components):
        return {'services': {name: {'image': value['image'], 'volumes': [
            {'type': 'bind', 'source': value['config'], 'target': '/etc/psim-platform/services/' + name + '.json', 'read_only': True}]}
            for name, value in components.items()}}

    def execute(self, components, *arguments):
        overlay = self.directory / 'active.json'
        atomic(overlay, self.overlay(components))
        return run('docker', 'compose', '--file', str(self.base), '--file', str(overlay), *arguments)

    def native_packages(self, target, manifest):
        files = {value['deb']['file'] for value in manifest['components'].values()}
        files.update(manifest.get('auxiliary_debs', []))
        verify(target / 'artifacts', self.directory / 'trusted.pub.pem', selected_artifacts=files)
        paths = [target / 'artifacts' / name for name in sorted(files)]
        for path in paths:
            require(path.is_file() and path.suffix == '.deb', 'missing signed native package')
        expected = {path.name.split('_')[0]: path.name.split('_')[1] for path in paths}
        installed = subprocess.run(['dpkg-query', '--show', '--showformat=${Package} ${Version}\n', *expected], text=True, capture_output=True)
        actual = dict(line.split(' ', 1) for line in installed.stdout.splitlines() if ' ' in line)
        if installed.returncode == 0 and actual == expected: return
        run('dpkg', '--force-confold', '--install', *map(str, paths))

    def fence_contract(self, epoch):
        state = self.state()
        require(type(epoch) is int and epoch > 0, 'invalid contract reader epoch')
        state['contract_floor'] = max(epoch, state['contract_floor'] or 0)
        atomic(self.state_file, state)

    def resume(self, wait=True):
        state = self.state()
        containers = [name for name, value in state['components'].items()
                      if value.get('runtime', 'containers') == 'containers' and not value.get('container')]
        if containers: self.execute(state['components'], 'up', '-d', '--no-deps', '--pull', 'never', *containers)
        for name, value in state['components'].items():
            if value.get('runtime') == 'systemd': run('systemctl', 'start', 'psim-' + name + '.service')
            elif value.get('container'): run('docker', 'start', value['container'])
        if wait:
            for name in state['components']: self.readiness(name)

    def record_runtime(self, runtime):
        require(runtime in ('containers', 'systemd'), 'unsupported application runtime')
        state = self.state()
        for name, value in state['components'].items():
            if runtime == 'systemd':
                require(subprocess.run(['systemctl', 'is-active', '--quiet', 'psim-' + name + '.service']).returncode == 0,
                        'native service is not running: ' + name)
            else:
                inspected = json.loads(run('docker', 'inspect', self.container(name)))[0]
                require(inspected['State']['Running'] and inspected['Image'] == value['image'], 'container differs from the retained release')
            self.readiness(name)
            value['runtime'] = runtime
        atomic(self.state_file, state)

    def park_streams(self):
        state = self.state()
        for value in state['components'].values():
            if value.get('operator_config') and value.get('container'):
                run('docker', 'stop', '--timeout', '40', value['container'])
                value['config'] = value.pop('operator_config')
                value.pop('container')
        self.stream_admin(state['components'])
        atomic(self.state_file, state)

    def native_start(self, name, value, manifest):
        folder = Path('/etc/systemd/system') / ('psim-' + name + '.service.d')
        require(not folder.is_symlink(), 'systemd override directory is a symlink')
        folder.mkdir(exist_ok=True)
        destination = '/etc/psim-platform/services/' + name + '.json'
        content = '[Service]\nBindReadOnlyPaths=' + value['config'] + ':' + destination + '\n'
        path = folder / '50-psim-release.conf'
        require(not path.is_symlink(), 'systemd release override is a symlink')
        path.write_text(content); path.chmod(0o644)
        run('systemctl', 'daemon-reload')
        run('systemctl', 'start', 'psim-' + name + '.service')

    def readiness(self, name, version=None, timeout=300, component=None):
        profile = json.loads((self.prefix / 'config/profile.json').read_text())
        address = next(item['address'] for item in profile['nodes'] if item['name'] == self.node)
        component = component or self.state()['components'][name]
        port = int(json.loads(Path(component['config']).read_text())['admin']['listen'].rsplit(':', 1)[1])
        deadline = time.monotonic() + timeout
        last = None
        while time.monotonic() < deadline:
            try:
                with urllib.request.urlopen(f'http://{address}:{port}/health/ready', timeout=3) as response:
                    require(response.status == 200, 'replica is not ready')
                if version:
                    with urllib.request.urlopen(f'http://{address}:{port}/info', timeout=3) as response:
                        require(json.load(response)['version'] == version, 'ready process has a different version')
                return
            except (OSError, ValueError) as error:
                last = type(error).__name__; time.sleep(1)
        raise ValueError('replica readiness timed out: ' + name + ' (' + str(last) + ')')

    def stream_config(self, name, candidate, previous):
        candidate.pop('container', None)
        candidate['operator_config'] = candidate.get('operator_config', candidate['config'])
        config = json.loads(Path(candidate['operator_config']).read_text())
        current = json.loads(Path(previous['config']).read_text())
        address, port = config['admin']['listen'].rsplit(':', 1)
        original = int(port)
        selected = original + (2000 if current['admin']['listen'].endswith(':' + str(original + 1000)) else 1000)
        require(selected <= 65535, 'administrative port leaves no room for overlapping stream replicas')
        config['admin']['listen'] = address + ':' + str(selected)
        config['service']['instance'] += '-release-' + candidate['release'][:12]
        path = Path(candidate['operator_config']).parent / (name + '-stream-' + str(selected) + '.json')
        atomic(path, config, 0o644)
        candidate['config'] = str(path)

    def stream_groups(self, name, container, timeout=300):
        expected = {'psim-normalizer'} if name == 'normalizer' else {'psim-correlation-router', 'psim-correlation-engine'}
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            assigned = set()
            for line in run('docker', 'logs', container).splitlines():
                try: value = json.loads(line)
                except ValueError: continue
                if value.get('msg') == 'kafka partitions assigned' and value.get('count', 0) > 0:
                    assigned.add(value.get('group'))
            if expected <= assigned: return
            inspected = json.loads(run('docker', 'inspect', container))[0]
            require(inspected['State']['Running'], 'new stream replica stopped before joining its consumer groups')
            time.sleep(1)
        raise ValueError('new stream replica did not receive Kafka assignments: ' + name)

    def stream_admin(self, components):
        path = self.prefix / 'material' / self.node / 'haproxy/haproxy.cfg'
        original = path.read_text()
        begin, end = '\n# psim lifecycle admin begin\n', '\n# psim lifecycle admin end\n'
        base = original
        if begin in base:
            head, _, tail = base.partition(begin)
            require(end in tail, 'incomplete lifecycle proxy configuration')
            base = head + tail.partition(end)[2]
        sections = []
        profile = json.loads((self.prefix / 'config/profile.json').read_text())
        address = next(item['address'] for item in profile['nodes'] if item['name'] == self.node)
        for name, value in components.items():
            if not value.get('container') or not value.get('operator_config'): continue
            source = json.loads(Path(value['operator_config']).read_text())['admin']['listen'].rsplit(':', 1)[1]
            target = json.loads(Path(value['config']).read_text())['admin']['listen'].rsplit(':', 1)[1]
            sections.append('listen lifecycle-' + name + '\n  bind ' + address + ':' + source
                            + '\n  mode http\n  server current ' + address + ':' + target + ' check\n')
        changed = base + (begin + '\n'.join(sections) + end if sections else '')
        if changed == original: return
        proxy_id = run('docker', 'compose', '--file', str(self.base), 'ps', '--quiet', 'haproxy')
        require(proxy_id, 'administrative proxy is not running')
        path.write_text(changed)
        try: run('docker', 'exec', proxy_id, '/usr/local/sbin/haproxy', '-c', '-f', '/etc/psim-platform/haproxy/secrets/haproxy.cfg')
        except BaseException:
            path.write_text(original); raise
        run('docker', 'kill', '--signal', 'USR2', proxy_id)

    def stream_handoff(self, name, candidate, previous, state, operation, version):
        label = 'psim-release-' + self.node + '-' + name + '-' + candidate['release'][:12]
        found = subprocess.run(['docker', 'inspect', label], text=True, capture_output=True)
        if found.returncode == 0:
            value = json.loads(found.stdout)[0]
            require(value['Config']['Labels'].get('psim.lifecycle.prefix') == str(self.prefix)
                    and not value['State']['Running'], 'stream replacement name is already in use')
            run('docker', 'rm', label)
        active = state['components'] | {name: candidate}
        candidate['container'] = self.execute(active, 'run', '--detach', '--no-deps', '--name', label,
            '--label', 'psim.lifecycle.prefix=' + str(self.prefix), name).splitlines()[-1]
        atomic(self.state_file, state)
        self.stream_groups(name, candidate['container'])
        self.readiness(name, version, component=candidate)
        state['components'][name] = candidate; atomic(self.state_file, state)
        run('docker', 'stop', '--timeout', '40', previous['container'])
        self.stream_admin(state['components'])

    def replace(self, release, name, rollback=False):
        identity, target, manifest = self.stage(release)
        state = self.state()
        require(name in state['components'] and name in manifest['components'], 'unknown installed component')
        previous = dict(state['components'][name])
        if previous['release'] == identity:
            self.readiness(name, manifest['version']); return
        component = manifest['components'][name]
        if rollback:
            require(state['contract_floor'] is None, 'rollback is forbidden after contract migration')
            candidates = [entry for entry in state['operations'] if entry['component'] == name and entry['previous']['release'] == identity]
            require(candidates, 'rollback target is not a retained previous release')
            candidate = candidates[-1]['previous']
        else:
            require(component['deb']['version'], 'component package version is missing')
            current = json.loads(Path(previous.get('operator_config', previous['config'])).read_text())
            operations = manifest.get('config_migrations', {}).get(name, [])
            profile = json.loads((self.prefix / 'config/profile.json').read_text())
            infrastructure = [item for item in profile['nodes'] if 'infrastructure' in item['roles']]
            context = {'redis_host': infrastructure[0]['name'], 'redis_primary_name': profile['name'],
                       'redis_sentinels': [item['name'] + ':26379' for item in infrastructure] if profile['topology'] == 'cluster' else []}
            transformed = migrate_config(current, previous['config_format'], manifest['config_format'], operations, context)
            configuration = target / 'config' / (name + '.json')
            if configuration.exists():
                require(json.loads(configuration.read_text()) == transformed, 'staged configuration differs from current operator settings')
            else:
                atomic(configuration, transformed, 0o644)
            candidate = {'release': identity, 'image': component['image']['id'], 'config': str(configuration),
                         'config_format': manifest['config_format'], 'runtime': previous.get('runtime', 'containers')}
        artifact = target / 'artifacts' / component['image']['file']
        verify(target / 'artifacts', self.directory / 'trusted.pub.pem', selected_artifacts=(component['image']['file'],))
        run('docker', 'image', 'load', '--input', str(artifact))
        inspected = json.loads(run('docker', 'image', 'inspect', component['image']['tag']))[0]
        require(inspected['Id'] in component['image'].get('digests', [component['image']['id']]), 'loaded image differs from signed OCI identity')
        candidate['image'] = inspected['Id']
        require(inspected['Architecture'] == 'amd64' and inspected['Os'] == 'linux'
                and inspected['Config']['User'] not in ('', '0', '0:0', 'root'), 'unsupported or privileged component image')
        overlap = name in ('normalizer', 'correlation-engine') and candidate.get('runtime', 'containers') == 'containers'
        if overlap:
            previous['container'] = self.container(name, previous)
            self.stream_config(name, candidate, previous)
        active = state['components'] | {name: candidate}
        self.execute(active, 'run', '--rm', '--no-deps', '-T', name, '--check-config', '--config', '/etc/psim-platform/services/' + name + '.json')
        operation = {'component': name, 'previous': previous, 'target': candidate, 'status': 'pending'}
        state['operations'].append(operation); atomic(self.state_file, state)
        try:
            if candidate.get('runtime') == 'systemd':
                self.native_packages(target, manifest)
                run('systemctl', 'stop', 'psim-' + name + '.service')
                self.native_start(name, candidate, manifest)
            elif overlap:
                self.stream_handoff(name, candidate, previous, state, operation, manifest['version'])
            else:
                self.execute(state['components'], 'stop', '--timeout', '40', name)
                self.execute(active, 'up', '-d', '--no-deps', '--pull', 'never', name)
            state['components'][name] = candidate; atomic(self.state_file, state)
            self.readiness(name, manifest['version'])
            operation['status'] = 'complete'; atomic(self.state_file, state)
        except BaseException as error:
            try:
                diagnostics = (run('journalctl', '--unit', 'psim-' + name + '.service', '--no-pager', '--lines', '120')
                               if candidate.get('runtime') == 'systemd' else run('docker', 'logs', '--tail', '120', candidate['container'])
                               if overlap and candidate.get('container') else self.execute(active, 'logs', '--no-color', '--tail', '120', name))
            except Exception as diagnostic_error:
                diagnostics = 'process diagnostics unavailable: ' + type(diagnostic_error).__name__
            failure = self.directory / ('failure-' + name + '.log')
            failure.write_text(type(error).__name__ + ': ' + str(error) + '\n' + diagnostics)
            failure.chmod(0o600)
            if candidate.get('runtime') == 'systemd':
                previous_root = self.directory / 'releases' / previous['release']
                previous_manifest = verify(previous_root / 'artifacts', self.directory / 'trusted.pub.pem')
                self.native_packages(previous_root, previous_manifest)
                run('systemctl', 'stop', 'psim-' + name + '.service')
                self.native_start(name, previous, previous_manifest)
            elif overlap:
                if candidate.get('container'): run('docker', 'rm', '--force', candidate['container'])
                state['components'][name] = previous
                self.stream_admin(state['components'])
                run('docker', 'start', previous['container'])
            else:
                self.execute(state['components'] | {name: previous}, 'up', '-d', '--no-deps', '--pull', 'never', name)
            state['components'][name] = previous
            operation['status'] = 'reverted'; atomic(self.state_file, state)
            self.readiness(name)
            raise


def proxy(prefix, node, command):
    base = Path(prefix) / 'config' / node / 'compose.json'
    container = run('docker', 'compose', '--file', str(base), 'ps', '--quiet', 'haproxy')
    require(container, 'deployment proxy is not running')
    pid = json.loads(run('docker', 'inspect', container))[0]['State']['Pid']
    endpoint = '/proc/' + str(pid) + '/root/tmp/psim-haproxy.sock'
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5); connection.connect(endpoint)
        connection.sendall((command + '\n').encode()); connection.shutdown(socket.SHUT_WR)
        chunks = []
        while block := connection.recv(65536): chunks.append(block)
    answer = b''.join(chunks).decode()
    require(not any(value in answer.lower() for value in ('unknown command', 'no such', 'permission denied', 'can\'t find')), 'proxy runtime command failed')
    return answer


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['init', 'stage', 'replace', 'rollback', 'status', 'proxy', 'ready', 'fence-contract', 'resume', 'record-runtime', 'park-streams'])
    parser.add_argument('--prefix', required=True); parser.add_argument('--node', required=True)
    parser.add_argument('--release'); parser.add_argument('--trusted-key'); parser.add_argument('--component'); parser.add_argument('--command')
    args = parser.parse_args()
    if args.action == 'proxy': print(proxy(args.prefix, args.node, args.command)); raise SystemExit(0)
    node = Node(args.prefix, args.node)
    if args.action == 'init': node.initialize(args.release, args.trusted_key)
    else:
        require(node.directory.is_dir() and not node.directory.is_symlink(), 'invalid lifecycle directory')
        with (node.directory / 'operation.lock').open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if args.action == 'stage': node.stage(args.release)
            elif args.action in ('replace', 'rollback'): node.replace(args.release, args.component, args.action == 'rollback')
            elif args.action == 'ready': node.readiness(args.component)
            elif args.action == 'fence-contract': node.fence_contract(int(args.command))
            elif args.action == 'resume': node.resume(wait=args.command != 'start-only')
            elif args.action == 'record-runtime': node.record_runtime(args.command)
            elif args.action == 'park-streams': node.park_streams()
            else: print(json.dumps(node.state(), indent=2))
