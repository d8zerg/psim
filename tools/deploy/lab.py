"""Create, resume and remove the local VM deployment using an owned factory record."""
import argparse
from contextlib import contextmanager
import fcntl
import hashlib
import json
import os
from pathlib import Path
import shutil
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bootstrap
import edge
import local
from material import verify_bundle
from model import Profile, SERVICES, require
from render import render

ROOT = local.ROOT
CACHE = local.CACHE
STATE = CACHE / 'lab-state.json'


@contextmanager
def exclusive():
    CACHE.mkdir(parents=True, exist_ok=True, mode=0o700)
    require(not CACHE.is_symlink() and CACHE.stat().st_uid == os.getuid() and CACHE.stat().st_mode & 0o777 == 0o700,
            'unsafe deployment cache')
    path = CACHE / 'lab.lock'
    require(not path.is_symlink(), 'unsafe deployment operation lock')
    with path.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_state():
    if not STATE.exists(): return None
    require(not STATE.is_symlink() and STATE.stat().st_uid == os.getuid() and STATE.stat().st_mode & 0o777 == 0o600,
            'foreign or unsafe deployment factory record')
    state = json.loads(STATE.read_text())
    require(state['format'] == 1 and state['uid'] == os.getuid() and state['root'] == str(ROOT) and state['topology'] in ('single', 'cluster'),
            'deployment factory identity mismatch')
    return state


def save_state(state):
    descriptor, name = tempfile.mkstemp(prefix='.lab-', dir=CACHE)
    temporary = Path(name)
    try:
        with os.fdopen(descriptor, 'w') as stream:
            json.dump(state, stream, indent=2); stream.write('\n'); stream.flush(); os.fsync(stream.fileno())
        temporary.replace(STATE)
        descriptor = os.open(CACHE, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(descriptor)
        finally: os.close(descriptor)
    finally:
        temporary.unlink(missing_ok=True)


def paths(topology):
    require(topology in ('single', 'cluster'), 'invalid lab topology')
    return local.BUILD / ('lab-' + topology), CACHE / ('lab-' + topology)


def private_paths(profile, bundle, holder):
    require(not holder.is_symlink() and (not holder.exists() or
            (holder.stat().st_uid == os.getuid() and holder.stat().st_mode & 0o777 == 0o700)),
            'unsafe private lab directory')
    if holder.exists(): require({path.name for path in holder.iterdir()} <= {'material'}, 'foreign files in private lab directory')
    payloads = local.BUILD / 'payloads' / bundle.name
    if payloads.exists():
        require(not payloads.is_symlink() and {path.name for path in payloads.iterdir()} <= {node.name + '.tar.gz' for node in profile.nodes}, 'foreign node payload files')
    return payloads


def wait_operation(bundle, operation, timeout=180):
    print('Waiting for deployment operation: ' + operation, flush=True)
    deadline = time.monotonic() + timeout
    while True:
        result = bootstrap.bootstrap(bundle, operation, check=False, capture_output=True)
        if result.returncode == 0:
            print(result.stdout.strip(), flush=True)
            return
        if time.monotonic() >= deadline:
            raise RuntimeError('deployment operation timed out: ' + operation + '\n' + result.stdout[-1000:] + result.stderr[-1000:])
        time.sleep(2)


def material_identity(profile, holder):
    program = "import json; print(json.dumps(json.load(open('/private/material/installation.json'))))"
    value = json.loads(local.vm.capture(['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
        '--cap-drop', 'ALL', '--cap-add', 'DAC_OVERRIDE', '--security-opt', 'no-new-privileges:true', '--env', 'LD_PRELOAD=',
        '--mount', 'type=bind,src=' + str(holder) + ',dst=/private,readonly', '--entrypoint', 'python3',
        'psim-platform/postgres:' + profile.image_version, '-c', program]))
    require(value['lab'] is True and value['complete'] is True and value['name'] == profile.name,
            'private lab material is incomplete or belongs to another installation')
    return value


def public_ca(profile, holder, topology, create=True):
    path = local.BUILD / ('lab-' + topology + '-ca.pem')
    certificate = local.vm.capture(['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
        '--cap-drop', 'ALL', '--cap-add', 'DAC_OVERRIDE', '--security-opt', 'no-new-privileges:true', '--env', 'LD_PRELOAD=',
        '--mount', 'type=bind,src=' + str(holder) + ',dst=/private,readonly', '--entrypoint', 'python3',
        'psim-platform/postgres:' + profile.image_version, '-c', "print(open('/private/material/authority/platform.pem').read(),end='')"]) + '\n'
    require(certificate.startswith('-----BEGIN CERTIFICATE-----\n') and 'PRIVATE KEY' not in certificate, 'invalid public lab trust')
    require(not path.is_symlink() and (not path.exists() or path.read_text() == certificate), 'public lab trust file belongs to another installation')
    if create: path.write_text(certificate)
    return path


def up(topology):
    bundle, holder = paths(topology)
    state = read_state()
    require(state is None or state['topology'] == topology, 'another lab topology is installed; destroy it before changing topology')
    if state:
        profile = verify_bundle(bundle)
        checksum = digest(bundle / 'manifest.json')
    else:
        value = json.loads((ROOT / ('deploy/topologies/local-vm.json' if topology == 'cluster' else 'deploy/topologies/local-single.json')).read_text())
        value['version'] = local.vm.capture(['python3', '-I', str(ROOT / 'tools/version.py'), '--field', 'version'])
        profile = Profile.parse(value)
        bundle.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.lab-render-', dir=bundle.parent) as temporary:
            candidate = Path(temporary) / 'bundle'
            render(profile, candidate)
            checksum = digest(candidate / 'manifest.json')
            if bundle.exists():
                verify_bundle(bundle)
                require(digest(bundle / 'manifest.json') == checksum, 'unowned lab configuration differs from the requested installation')
            else: shutil.copytree(candidate, bundle)
    references = sorted({image for node in profile.nodes for image in local.images_for(bundle, node.name)})
    images = {image: local.vm.capture(['docker', 'image', 'inspect', '--format', '{{.Id}}', image]) for image in references}
    if state:
        require(state['manifest'] == checksum and state['images'] == images, 'lab images or configuration changed; recreate the lab')
    else:
        require(not holder.exists(), 'private lab directory exists without a factory record')
        state = {'format': 1, 'uid': os.getuid(), 'root': str(ROOT), 'topology': topology,
                 'name': profile.name, 'manifest': checksum, 'images': images, 'status': 'preparing'}
        save_state(state)
    holder.mkdir(mode=0o700, exist_ok=True)
    private_paths(profile, bundle, holder)
    material = holder / 'material'
    if not material.exists():
        require(not state.get('material_id'), 'private material is missing; recreate the lab')
        local.vm.run(['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
            '--cap-drop', 'ALL', '--cap-add', 'CHOWN', '--cap-add', 'DAC_OVERRIDE', '--security-opt', 'no-new-privileges:true',
            '--env', 'LD_PRELOAD=', '--tmpfs', '/tmp:rw,nosuid,size=32m,mode=1777',
            '--mount', 'type=bind,src=' + str(ROOT / 'tools/deploy') + ',dst=/repo/tools/deploy,readonly',
            '--mount', 'type=bind,src=' + str(ROOT / 'deploy/local/config/keycloak') + ',dst=/repo/deploy/local/config/keycloak,readonly',
            '--mount', 'type=bind,src=' + str(ROOT / 'deploy/observability') + ',dst=/repo/deploy/observability,readonly',
            '--mount', 'type=bind,src=' + str(bundle) + ',dst=/bundle,readonly',
            '--mount', 'type=bind,src=' + str(holder) + ',dst=/private', '--entrypoint', 'python3',
            'psim-platform/postgres:' + profile.image_version, '/repo/tools/deploy/material.py', '--bundle', '/bundle', '--output', '/private/material', '--lab-issue'])
    identity = material_identity(profile, holder)
    require(identity['public_manifest_sha256'] == state['manifest'], 'private material belongs to another public bundle')
    require(not state.get('material_id') or state['material_id'] == identity['id'], 'private material identity changed; recreate the lab')
    state['material_id'] = identity['id']; save_state(state)
    ca = public_ca(profile, holder, topology)
    state['ca_sha256'] = digest(ca); save_state(state)
    manager = local.vm.container(local.VM_NAME)
    cold_start = manager is None or not manager['State']['Running']
    local.prepare(bundle, material)
    require(images == {image: local.vm.capture(['docker', 'image', 'inspect', '--format', '{{.Id}}', image]) for image in references},
            'application images changed during delivery; recreate the lab')
    infrastructure = {name for node in profile.nodes for name in json.loads((bundle / node.name / 'compose.json').read_text())['services']} - set(SERVICES)
    deferred = {'registry', 'keycloak', 'seaweed-filer', 'seaweed-s3'} & infrastructure
    if cold_start or state['status'] != 'ready':
        for node in profile.nodes:
            names = sorted(deferred & json.loads((bundle / node.name / 'compose.json').read_text())['services'].keys())
            if names: local.compose(profile, node.name, ['stop', *names])
    local.start(bundle, infrastructure - deferred)
    edge.configure(bundle)
    wait_operation(bundle, 'core')
    # Initialize database clients only after the writable primary and synchronous replicas are ready.
    local.start(bundle, deferred)
    if state['status'] != 'ready': bootstrap.bootstrap(bundle, 'kafka')
    wait_operation(bundle, 'schemas')
    wait_operation(bundle, 'bucket')
    wait_operation(bundle, 'identity')
    if state.get('lifecycle'):
        for node in profile.select('application'):
            destination = '/home/psim/.cache/psim-delivery/lifecycle/'
            for source in (ROOT / 'tools/deploy/node_release.py', ROOT / 'tools/deploy/delivery.py', ROOT / 'tools/version.py'):
                local.vm.copy_to(local.VM_NAME, node.name, source, destination + source.name)
            local.execute(node.name, ['sudo', 'python3', destination + 'node_release.py', 'resume', '--prefix', profile.prefix, '--node', node.name, '--command', 'start-only'])
    elif state.get('runtime') == 'systemd':
        import native
        require(state['packages'] == {path.name: digest(path) for path in native.packages(profile)}, 'native package artifacts changed; recreate the lab')
        native.units(profile, 'start')
    else:
        local.start(bundle, set(SERVICES))
    wait_operation(bundle, 'applications')
    state['status'] = 'ready'; save_state(state)
    print('Local VM deployment ready: ' + profile.origin)


def destroy():
    state = read_state()
    if state is None:
        local.vm.destroy(local.VM_NAME)
        return
    bundle, holder = paths(state['topology'])
    require(not bundle.is_symlink(), 'unsafe deployment bundle directory')
    profile = verify_bundle(bundle)
    require(digest(bundle / 'manifest.json') == state['manifest'] and profile.name == state['name'], 'lab bundle identity mismatch')
    payloads = private_paths(profile, bundle, holder)
    cleanup = '''import json,shutil,sys
from pathlib import Path
p=Path('/private/material')
if p.exists():
 assert not p.is_symlink()
 s=json.loads((p/'installation.json').read_text())
 assert s['lab'] is True and s['name']==sys.argv[1] and s['public_manifest_sha256']==sys.argv[2]
 if sys.argv[4]: assert s['id']==sys.argv[4]
 if sys.argv[3]=='remove': shutil.rmtree(p)
'''
    command = ['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0', '--env', 'LD_PRELOAD=',
        '--mount', 'type=bind,src=' + str(holder) + ',dst=/private', '--entrypoint', 'python3',
        'psim-platform/postgres:' + profile.image_version, '-c', cleanup, profile.name, state['manifest']]
    ca = local.BUILD / ('lab-' + state['topology'] + '-ca.pem')
    if ca.exists() or ca.is_symlink():
        require(not ca.is_symlink() and ca.stat().st_uid == os.getuid(), 'unsafe public lab trust file')
        if state.get('ca_sha256'): require(digest(ca) == state['ca_sha256'], 'public lab trust belongs to another installation')
        else: public_ca(profile, holder, state['topology'], create=False)
    if holder.exists(): local.vm.run(command + ['verify', state.get('material_id', '')])
    else: require(not state.get('material_id'), 'private material disappeared from an installed lab')
    if local.vm.directory(local.VM_NAME).exists(): local.vm.destroy(local.VM_NAME)
    if holder.exists():
        local.vm.run(command + ['remove', state.get('material_id', '')])
        holder.rmdir()
    if payloads.exists():
        shutil.rmtree(payloads)
    shutil.rmtree(bundle)
    ca.unlink(missing_ok=True)
    STATE.unlink()
    print('Owned VM deployment, private material and node payloads removed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['up', 'down', 'destroy', 'status'])
    parser.add_argument('--topology', choices=['single', 'cluster'], default='cluster')
    args = parser.parse_args()
    with exclusive():
        if args.action == 'up': up(args.topology)
        elif args.action == 'down': local.vm.down(local.VM_NAME)
        elif args.action == 'destroy': destroy()
        else: print(json.dumps(read_state(), indent=2))
