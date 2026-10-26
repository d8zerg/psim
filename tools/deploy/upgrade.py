"""Coordinate serial application replacement with retained releases and proxy draining."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery import digest, require, transition, verify
from material import verify_bundle
from model import SERVICES
import local

ORDER = ('audit-service', 'notification-service', 'command-service', 'response-engine', 'projection-service',
         'incident-service', 'event-history', 'correlation-engine', 'normalizer', 'rule-management',
         'resource-catalog', 'api-gateway', 'connector-gateway')


class Coordinator:
    def __init__(self, bundle, release, baseline, trusted):
        self.bundle, self.release, self.baseline, self.trusted = map(lambda value: Path(value).resolve(), (bundle, release, baseline, trusted))
        self.profile = verify_bundle(self.bundle)
        self.old = verify(self.baseline, self.trusted)
        self.new = verify(self.release, self.trusted)
        require(set(self.new['components']) == set(SERVICES), 'release component set differs from the installation')
        transition(self.old, self.new,
            json.loads((self.baseline / self.old['contracts']).read_text()),
            json.loads((self.release / self.new['contracts']).read_text()))
        self.stage_root = '/home/psim/.cache/psim-delivery/lifecycle'
        self.slots = {'baseline': 'baseline-' + digest(self.baseline / 'SHA256SUMS'),
                      'candidate': 'candidate-' + digest(self.release / 'SHA256SUMS')}

    def agent(self, node, action, release=None, component=None, command=None):
        arguments = ['sudo', 'python3', self.stage_root + '/node_release.py', action,
                     '--prefix', self.profile.prefix, '--node', node.name]
        if release: arguments += ['--release', self.stage_root + '/' + self.slots[release]]
        if action == 'init': arguments += ['--trusted-key', self.stage_root + '/trusted.pub.pem']
        if component: arguments += ['--component', component]
        if command: arguments += ['--command', command]
        try:
            return local.execute(node.name, arguments, capture_output=True).stdout
        except subprocess.CalledProcessError as error:
            raise RuntimeError('node release action failed: ' + node.name + '/' + action + '\n' + error.stderr[-2500:]) from error

    def prepare(self):
        # Every artifact is verified on the controller before transfer and again by the node agent.
        for node in self.profile.select('application'):
            print('Staging verified releases on ' + node.name, flush=True)
            local.execute(node.name, ['install', '-d', '-m', '0700', self.stage_root])
            for source in [Path(__file__), Path(__file__).with_name('node_release.py'), Path(__file__).with_name('delivery.py'),
                           Path(__file__).parents[1] / 'version.py']:
                local.vm.copy_to(local.VM_NAME, node.name, source, self.stage_root + '/' + source.name)
            local.vm.copy_to(local.VM_NAME, node.name, self.trusted, self.stage_root + '/trusted.pub.pem')
            for label, directory in [('baseline', self.baseline), ('candidate', self.release)]:
                destination = self.stage_root + '/' + self.slots[label]
                local.execute(node.name, ['install', '-d', '-m', '0700', destination])
                check = local.execute(node.name, ['python3', self.stage_root + '/delivery.py', '--release', destination,
                    '--trusted-key', self.stage_root + '/trusted.pub.pem'], check=False, capture_output=True)
                if check.returncode == 0:
                    remote = local.execute(node.name, ['sha256sum', destination + '/SHA256SUMS'], capture_output=True).stdout.split()[0]
                    if remote == digest(directory / 'SHA256SUMS'):
                        print('Verified transfer cache: ' + node.name + '/' + label, flush=True)
                        continue
                for source in sorted(directory.iterdir()):
                    local.vm.copy_to(local.VM_NAME, node.name, source, destination + '/' + source.name)
            self.agent(node, 'init', 'baseline')
            self.agent(node, 'stage', 'candidate')
        for node in self.profile.select('edge'):
            if node in self.profile.select('application'): continue
            local.execute(node.name, ['install', '-d', '-m', '0700', self.stage_root])
            for name in ('node_release.py', 'delivery.py'):
                local.vm.copy_to(local.VM_NAME, node.name, Path(__file__).with_name(name), self.stage_root + '/' + name)
            local.vm.copy_to(local.VM_NAME, node.name, Path(__file__).parents[1] / 'version.py', self.stage_root + '/version.py')
        import lab
        state = lab.read_state()
        if state and state['name'] == self.profile.name:
            state['lifecycle'] = {'baseline': str(self.baseline), 'candidate': str(self.release), 'status': 'prepared'}
            lab.save_state(state)

    def migrate(self, contract=False):
        release = self.baseline if contract else self.release
        manifest = self.old if contract else self.new
        node = self.profile.select('infrastructure')[0]
        destination = self.stage_root + '/migrations'
        local.execute(node.name, ['install', '-d', '-m', '0700', destination])
        files = ['release.json', *[entry['file'] for entry in manifest.get('migrations', [])]]
        for name in files:
            local.vm.copy_to(local.VM_NAME, node.name, release / name, destination + '/' + name)
        local.vm.copy_to(local.VM_NAME, node.name, Path(__file__).with_name('release_migrations.py'), self.stage_root + '/release_migrations.py')
        command = ['sudo', 'docker', 'run', '--rm', '--read-only', '--network', 'host', '--cap-drop', 'ALL',
            '--security-opt', 'no-new-privileges:true', '--user', str(self.profile.uid('deployment-admin')) + ':' + str(self.profile.uid('deployment-admin')),
            '--env', 'LD_PRELOAD=', '--tmpfs', '/tmp:rw,nosuid,size=64m,mode=1777',
            '--mount', 'type=bind,src=' + self.profile.prefix + '/material/' + node.name + '/deployment-admin,dst=/operator,readonly',
            '--mount', 'type=bind,src=' + destination + ',dst=/release,readonly',
            '--mount', 'type=bind,src=' + self.stage_root + '/release_migrations.py,dst=/runner.py,readonly',
            '--mount', 'type=bind,src=' + self.profile.prefix + '/config/' + node.name + '/hosts,dst=/etc/hosts,readonly',
            '--entrypoint', 'python3', 'psim-platform/postgres:' + self.profile.image_version, '/runner.py', '--release', '/release']
        if contract: command += ['--phase', 'contract', '--minimum-epoch', str(self.new['schema_epoch'])]
        # Public SQL plans contain no credentials; operator credentials remain in their existing role directory.
        local.execute(node.name, ['chmod', '-R', 'a+rX', destination])
        print(local.execute(node.name, command, capture_output=True).stdout, flush=True)
        analytical = [entry for entry in manifest.get('migrations', []) if entry['engine'] == 'clickhouse']
        if analytical:
            from delivery import digest
            tools = manifest['migration_tools']
            archive = release / tools['file']
            local.vm.copy_to(local.VM_NAME, node.name, archive, self.stage_root + '/' + archive.name)
            load = '''import hashlib,json,pathlib,subprocess,sys
p=pathlib.Path(sys.argv[1]); assert hashlib.file_digest(p.open('rb'),'sha256').hexdigest()==sys.argv[2]
subprocess.run(['docker','image','load','--input',str(p)],check=True,stdout=subprocess.DEVNULL)
value=json.loads(subprocess.check_output(['docker','image','inspect',sys.argv[3]],text=True))[0]['Id']
assert value in json.loads(sys.argv[4]); print(value)
'''
            identity = local.execute(node.name, ['sudo', 'python3', '-c', load, self.stage_root + '/' + archive.name,
                digest(archive), tools['tag'], json.dumps(tools['digests'])], capture_output=True).stdout.strip()
            base = command[:command.index('--entrypoint')]
            settings = {'PSIM_PG_HOST': 'postgres', 'PSIM_PG_PORT': '15432', 'PSIM_PG_USER': 'postgres',
                'PSIM_PG_PASSWORD_FILE': '/operator/postgres.secret', 'PSIM_PG_SSLROOTCERT': '/operator/ca.pem',
                'PSIM_PG_SSLCERT': '/operator/postgres.pem', 'PSIM_PG_SSLKEY': '/operator/postgres.key',
                'PSIM_CH_HOST': 'clickhouse', 'PSIM_CH_USER': 'deployment-admin', 'PSIM_CH_PASSWORD_FILE': '/operator/clickhouse.secret',
                'PSIM_CH_CA': '/operator/ca.pem', 'PSIM_CH_CERT': '/operator/identity.pem', 'PSIM_CH_KEY': '/operator/identity.key'}
            for key, value in settings.items(): base += ['--env', key + '=' + value]
            for entry in analytical:
                local.execute(node.name, base + ['--entrypoint', '/usr/bin/psim-ch-migrate', identity,
                    'contract' if contract else 'expand', '/release/' + entry['file']], capture_output=True)

    def contract(self):
        from delivery import digest
        require(self.new.get('schema_epoch', 0) > self.old.get('schema_epoch', 0), 'contract requires a later schema epoch')
        for node in self.profile.select('application'):
            state = json.loads(self.agent(node, 'status'))
            require(all(value['release'] == digest(self.release / 'SHA256SUMS') for value in state['components'].values()),
                    'old readers are still installed; contract is forbidden')
            for name in state['components']: self.agent(node, 'ready', component=name)
        # Persist the rollback barrier on every node before the first destructive statement.
        for node in self.profile.select('application'):
            self.agent(node, 'fence-contract', command=str(self.new['schema_epoch']))
        self.migrate(contract=True)

    def route(self, name, index, enabled):
        if name in ('normalizer', 'correlation-engine'): return
        targets = [(name, 'endpoint-' + str(index + 1))]
        if name == 'api-gateway': targets.append(('public-api', 'api-' + str(index + 1)))
        if name == 'connector-gateway': targets.append(('public-connectors', 'gateway-' + str(index + 1)))
        for node in self.profile.select('edge'):
            for backend, server in targets:
                # Public listeners exist only on edge nodes. Service backends exist on every proxy.
                self.agent(node, 'proxy', command='set server ' + backend + '/' + server + ' state ' + ('ready' if enabled else 'drain'))
        if not enabled: time.sleep(2)

    def apply(self, rollback=False):
        require(self.profile.topology == 'cluster', 'serial no-downtime replacement requires at least two application nodes')
        if not rollback: self.migrate()
        changed = []
        installed = {node.name: json.loads(self.agent(node, 'status'))['components']
                     for node in self.profile.select('application')}
        target = digest((self.baseline if rollback else self.release) / 'SHA256SUMS')
        names = list(reversed(ORDER)) if rollback else ORDER
        for name in names:
            for index, node in enumerate(self.profile.select('application')):
                if installed[node.name][name]['release'] == target:
                    self.agent(node, 'rollback' if rollback else 'replace', 'baseline' if rollback else 'candidate', name)
                    continue
                # A healthy peer is mandatory before removing capacity from this component.
                peer = self.profile.select('application')[(index + 1) % len(self.profile.select('application'))]
                self.agent(peer, 'ready', component=name)
                self.route(name, index, False)
                try:
                    print(('Rolling back ' if rollback else 'Updating ') + node.name + '/' + name, flush=True)
                    self.agent(node, 'rollback' if rollback else 'replace', 'baseline' if rollback else 'candidate', name)
                    changed.append({'node': node.name, 'component': name})
                finally:
                    self.agent(node, 'ready', component=name)
                    self.route(name, index, True)
        import lab
        state = lab.read_state()
        if state and state.get('lifecycle') and state['name'] == self.profile.name:
            state['lifecycle']['status'] = 'rolled-back' if rollback else 'updated'
            lab.save_state(state)
        return changed


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['preflight', 'prepare', 'apply', 'rollback', 'contract'])
    parser.add_argument('--bundle', required=True); parser.add_argument('--release', required=True)
    parser.add_argument('--baseline', required=True); parser.add_argument('--trusted-key', required=True)
    args = parser.parse_args()
    coordinator = Coordinator(args.bundle, args.release, args.baseline, args.trusted_key)
    if args.action == 'preflight': print('Signed release and additive contracts verified')
    else:
        import lab
        with lab.exclusive():
            if args.action == 'prepare': coordinator.prepare()
            elif args.action == 'contract': coordinator.contract()
            else: print(json.dumps(coordinator.apply(args.action == 'rollback'), indent=2))


if __name__ == '__main__':
    main()
