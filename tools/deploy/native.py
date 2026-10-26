"""Switch PSIM application services between containers and Debian/systemd on owned guests."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootstrap
import local
from material import verify_bundle
from model import SERVICES, require


def packages(profile):
    folder = local.ROOT / 'build/debian13/release/packages'
    return [folder / ('psim-' + name + '_' + profile.version + '_amd64.deb')
            for name in sorted({'taxonomy', *{binary for binary, _, _ in SERVICES.values()}})]


def install(profile, node):
    stage = '/home/psim/.cache/psim-delivery/native'
    local.execute(node.name, ['install', '-d', '-m', '0700', stage])
    archives = packages(profile)
    for archive in archives:
        require(archive.is_file(), 'build the release Debian packages first')
        local.vm.copy_to('psim-platform-vm', node.name, archive, stage + '/' + archive.name)
    program = '''import hashlib,json,pathlib,subprocess,sys
prefix=pathlib.Path(sys.argv[1]); node=sys.argv[2]; name=sys.argv[3]; archives=json.loads(sys.argv[4])
s=json.loads((prefix/'installation.json').read_text())
assert s['node']==node and s['name']==name
for filename,checksum in archives.items():
 p=pathlib.Path(filename); assert hashlib.sha256(p.read_bytes()).hexdigest()==checksum
subprocess.run(['dpkg','--install',*archives],check=True)
base=pathlib.Path('/etc/psim-platform')
if base.exists():
 assert not base.is_symlink() and json.loads((base/'installation.json').read_text())==s
else:
 base.mkdir(mode=0o755); (base/'installation.json').write_text(json.dumps(s))
(base/'services').mkdir(exist_ok=True)
for source in sorted((prefix/'config'/node/'systemd').glob('psim-*.service')):
 service=source.name.removeprefix('psim-').removesuffix('.service')
 (base/service/'secrets').mkdir(parents=True,exist_ok=True)
 target=pathlib.Path('/etc/systemd/system')/source.name
 assert not target.is_symlink() and (not target.exists() or target.read_bytes()==source.read_bytes())
 target.write_bytes(source.read_bytes()); target.chmod(0o644)
subprocess.run(['systemctl','daemon-reload'],check=True)
'''
    expected = {stage + '/' + archive.name: hashlib.sha256(archive.read_bytes()).hexdigest() for archive in archives}
    local.execute(node.name, ['sudo', 'python3', '-c', program, profile.prefix, node.name, profile.name, json.dumps(expected)])
    local.execute(node.name, ['rm', *expected])


def units(profile, action):
    for node in profile.select('application'):
        local.execute(node.name, ['sudo', 'systemctl', action, *['psim-' + name + '.service' for name in SERVICES]])


def switch(runtime):
    import lab
    state = lab.read_state()
    require(state is not None and state['status'] == 'ready', 'runtime switching requires a ready owned deployment')
    bundle, _ = lab.paths(state['topology']); profile = verify_bundle(bundle)
    require(lab.digest(bundle / 'manifest.json') == state['manifest'], 'runtime bundle identity mismatch')
    if state.get('runtime', 'containers') == runtime:
        if runtime == 'systemd': units(profile, 'start')
        else: local.start(bundle, set(SERVICES))
        lab.wait_operation(bundle, 'applications')
        print('Application runtime already ready: ' + runtime)
        return
    if runtime == 'systemd':
        expected = {path.name: hashlib.sha256(path.read_bytes()).hexdigest() for path in packages(profile)}
        require(not state.get('packages') or state['packages'] == expected, 'Debian packages changed; recreate the lab')
        for node in profile.select('application'): install(profile, node)
        for node in profile.select('application'): local.compose(profile, node.name, ['stop', *SERVICES])
        state['packages'] = expected
    else:
        if state.get('runtime') == 'systemd':
            units(profile, 'stop'); units(profile, 'disable')
        local.start(bundle, set(SERVICES))
    try:
        if runtime == 'systemd':
            units(profile, 'enable'); units(profile, 'start')
        lab.wait_operation(bundle, 'applications')
    except Exception:
        if runtime == 'systemd':
            units(profile, 'stop'); units(profile, 'disable')
            local.start(bundle, set(SERVICES))
        raise
    state['runtime'] = runtime; lab.save_state(state)
    print('Application runtime ready: ' + runtime)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('runtime', choices=['containers', 'systemd']); args = parser.parse_args()
    import lab
    with lab.exclusive(): switch(args.runtime)
