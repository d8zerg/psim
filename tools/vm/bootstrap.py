"""Install hash-pinned Docker packages only in the managed, SSH-pinned guest VMs."""
import argparse
import fcntl
import hashlib
import json
from pathlib import Path
import sys
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vm


def packages():
    spec = json.loads((vm.ROOT / 'deploy/vm/docker-packages.json').read_text())
    cache = vm.CACHE / 'packages'
    cache.mkdir(parents=True, exist_ok=True)
    files = []
    with (cache / 'download.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for package in spec['packages']:
            vm.require(package['url'].startswith('https://download.docker.com/linux/debian/dists/trixie/pool/stable/amd64/'), 'invalid Docker package source')
            target = cache / (package['name'] + '.deb')
            if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest() != package['sha256']:
                temporary = target.with_suffix('.part')
                try:
                    with urllib.request.urlopen(package['url'], timeout=60) as response, temporary.open('wb') as output:
                        written = 0
                        while block := response.read(1024 * 1024):
                            written += len(block)
                            vm.require(written <= package['size_bytes'], 'Docker package exceeds its pinned size')
                            output.write(block)
                        vm.require(written == package['size_bytes'], 'Docker package size mismatch')
                    vm.require(hashlib.sha256(temporary.read_bytes()).hexdigest() == package['sha256'], 'Docker package checksum mismatch')
                    temporary.replace(target)
                finally:
                    temporary.unlink(missing_ok=True)
            files.append(target)
        checksums = cache / 'packages.sha256'
        checksums.write_text(''.join(p['sha256'] + '  ' + p['name'] + '.deb\n' for p in spec['packages']))
        files.append(checksums)
    return spec, files


def bootstrap(value):
    saved = vm.manifest(value['name'])
    vm.require(saved['profile_hash'] == vm.profile_digest(value), 'VM profile differs from the running lab')
    vm.wait(value['name'])
    spec = json.loads((vm.ROOT / 'deploy/vm/docker-packages.json').read_text())
    files = None
    report = {'packages': spec['packages'], 'nodes': {}, 'result': 'failed'}
    destination = '/home/psim/.cache/psim-bootstrap'
    for node in value['nodes']:
        name = node['name']
        query = ['dpkg-query', '-W', '-f=${Package}=${Version}\n', *[p['name'] for p in spec['packages']]]
        actual = vm.ssh(value['name'], name, query, check=False, capture_output=True).stdout.strip().splitlines()
        expected = {p['name'] + '=' + p['version'] for p in spec['packages']}
        if set(actual) != expected:
            print('Installing pinned Docker packages in ' + name, flush=True)
            if files is None: _, files = packages()
            vm.ssh(value['name'], name, ['install', '-d', '-m', '0700', destination])
            for source in files + [vm.ROOT / 'tools/vm/bootstrap.sh']:
                vm.copy_to(value['name'], name, source, destination + '/' + source.name)
            vm.ssh(value['name'], name, ['sudo', 'flock', '/run/lock/psim-bootstrap.lock', 'sh', destination + '/bootstrap.sh', destination])
        actual = vm.ssh(value['name'], name, query, capture_output=True).stdout.strip().splitlines()
        vm.require(set(actual) == {p['name'] + '=' + p['version'] for p in spec['packages']}, 'installed Docker package versions differ from the pinned inventory')
        vm.ssh(value['name'], name, ['sudo', 'systemctl', 'start', 'docker'])
        engine = vm.ssh(value['name'], name, ['sudo', 'docker', 'info', '--format', '{{.OSType}}'], capture_output=True).stdout.strip()
        vm.require(engine == 'linux', 'native Linux Docker Engine is required inside the VM')
        print('Pinned native Docker packages verified: ' + name, flush=True)
        report['nodes'][name] = {'packages': actual, 'native_engine': True}
    report['result'] = 'passed'
    output = vm.ROOT / 'build/vm/bootstrap.json'
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--profile', default=str(vm.ROOT / 'deploy/vm/local.json'))
    args = parser.parse_args()
    bootstrap(vm.profile(args.profile))
