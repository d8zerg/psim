#!/usr/bin/env python3
"""Orchestrate the owned local KVM lab through the native Docker Engine."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import socket
import subprocess
import sys
import time
import urllib.request
import uuid

ROOT = Path(__file__).resolve().parents[2]
CACHE = ROOT / '.cache/vm'
MANAGED = 'psim.vm.managed'


def run(args, **kwargs):
    return subprocess.run(args, check=True, text=True, **kwargs)


def capture(args):
    return subprocess.check_output(args, text=True).strip()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def profile(path):
    value = json.loads(Path(path).read_text())
    required = {'format', 'name', 'network', 'gateway', 'nodes'}
    require(isinstance(value, dict) and required <= set(value) <= required | {'forwards'}, 'invalid VM profile fields')
    require(type(value['format']) is int and value['format'] == 1 and isinstance(value['name'], str) and re.fullmatch(r'[a-z][a-z0-9-]{0,31}', value['name']), 'invalid VM namespace')
    network = ipaddress.IPv4Network(value['network'])
    private = any(network.subnet_of(ipaddress.IPv4Network(block)) for block in ('10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16'))
    require(network.prefixlen == 24 and private, 'VM network must be a private /24')
    gateway = ipaddress.IPv4Address(value['gateway'])
    require(gateway in network and gateway not in (network.network_address, network.broadcast_address), 'invalid VM gateway')
    nodes = value['nodes']
    require(isinstance(nodes, list) and 1 <= len(nodes) <= 6, 'VM profile must contain one to six nodes')
    for index, node in enumerate(nodes, 1):
        require(isinstance(node, dict) and set(node) == {'name', 'address', 'cpus', 'memory_mib', 'disk_gib', 'ssh_port'}, 'invalid VM node fields')
        require(isinstance(node['name'], str) and re.fullmatch(r'[a-z][a-z0-9-]{0,23}', node['name']), 'invalid VM node name')
        address = ipaddress.IPv4Address(node['address'])
        require(address in network and address not in (network.network_address, network.broadcast_address, gateway), 'invalid VM address')
        for key, lower, upper in [('cpus', 1, 64), ('memory_mib', 1024, 131072), ('disk_gib', 10, 1024), ('ssh_port', 1024, 65535)]:
            require(type(node[key]) is int and lower <= node[key] <= upper, 'invalid VM resource: ' + key)
    for key in ('name', 'address', 'ssh_port'):
        require(len({node[key] for node in nodes}) == len(nodes), 'duplicate VM ' + key)
    forwards = value.get('forwards', [])
    require(isinstance(forwards, list) and len(forwards) <= 16, 'invalid VM port forwards')
    for item in forwards:
        require(isinstance(item, dict) and set(item) == {'name', 'host_port', 'target_address', 'target_port'}, 'invalid VM port forward fields')
        require(isinstance(item['name'], str) and re.fullmatch(r'[a-z][a-z0-9-]{0,23}', item['name']), 'invalid VM port forward name')
        address = ipaddress.IPv4Address(item['target_address'])
        require(address in network and address not in (gateway, network.network_address, network.broadcast_address), 'invalid VM port forward target')
        for key, lower in [('host_port', 1024), ('target_port', 1)]:
            require(type(item[key]) is int and lower <= item[key] <= 65535, 'invalid VM port forward port')
    ports = [n['ssh_port'] for n in nodes] + [f['host_port'] for f in forwards]
    require(len(ports) == len(set(ports)), 'duplicate published VM port')
    require(len(forwards) == len({f['name'] for f in forwards}), 'duplicate VM port forward name')
    return value


def profile_digest(value):
    return hashlib.sha256(json.dumps({key: item for key, item in value.items() if key != 'forwards'}, sort_keys=True).encode()).hexdigest()


def write_manifest(state, value):
    temporary = state / ('manifest-' + uuid.uuid4().hex + '.tmp')
    try:
        with open(temporary, 'w', opener=lambda path, flags: os.open(path, flags, 0o600)) as stream:
            stream.write(json.dumps(value, indent=2) + '\n'); stream.flush(); os.fsync(stream.fileno())
        temporary.replace(state / 'manifest.json')
        descriptor = os.open(state, os.O_RDONLY | os.O_DIRECTORY)
        try: os.fsync(descriptor)
        finally: os.close(descriptor)
    finally:
        temporary.unlink(missing_ok=True)


def directory(name):
    require(isinstance(name, str) and re.fullmatch(r'[a-z][a-z0-9-]{0,31}', name), 'invalid VM namespace')
    CACHE.mkdir(parents=True, exist_ok=True)
    result = CACHE / name
    require(not result.is_symlink(), 'managed state cannot be a symlink')
    return result


def manifest(name):
    path = directory(name)
    value = json.loads((path / 'manifest.json').read_text())
    require(value.get('managed') == 1 and value.get('name') == name and value.get('uid') == os.getuid(), 'VM state ownership mismatch')
    require(value.get('host_state') == str(path), 'VM state location mismatch')
    return value


def container(name):
    identities = capture(['docker', 'container', 'ls', '--all', '--no-trunc', '--filter', 'name=^/' + name + '$', '--format', '{{.ID}}']).splitlines()
    if not identities:
        return None
    require(len(identities) == 1, 'ambiguous VM container identity')
    value = json.loads(capture(['docker', 'inspect', identities[0]]))[0]
    require(value['Config'].get('Labels', {}).get(MANAGED) == name, 'container name belongs to another resource')
    return value


def image_tag():
    digest = hashlib.sha256(f'{os.getuid()}:{os.getgid()}'.encode())
    for name in ('Dockerfile', 'controller.py', 'prepare.py'):
        digest.update((ROOT / 'tools/vm' / name).read_bytes())
    return 'psim-platform/vm-tools:' + digest.hexdigest()[:12]


def image():
    tag = image_tag()
    if subprocess.run(['docker', 'image', 'inspect', tag], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode:
        text = (ROOT / 'Taskfile.yml').read_text()
        base = re.search(r'^  TOOLCHAIN_BASE: (.+)$', text, re.MULTILINE).group(1)
        run(['docker', 'build', '--build-arg', 'BASE=' + base, '--build-arg', 'VM_UID=' + str(os.getuid()), '--build-arg', 'VM_GID=' + str(os.getgid()), '-t', tag, str(ROOT / 'tools/vm')])
    return tag


def base_image():
    spec = json.loads((ROOT / 'deploy/vm/image.json').read_text())
    require(re.fullmatch('[0-9a-f]{128}', spec['sha512']), 'invalid VM image digest')
    require(spec['url'].startswith('https://cloud.debian.org/images/cloud/trixie/') and spec['url'].endswith('.qcow2'), 'invalid VM image source')
    target = CACHE / 'images' / (spec['sha512'] + '.qcow2')
    target.parent.mkdir(parents=True, exist_ok=True)
    with (target.parent / 'download.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not target.exists():
            temporary = target.with_suffix('.part')
            try:
                with urllib.request.urlopen(spec['url'], timeout=60) as response, temporary.open('wb') as output:
                    written = 0
                    while block := response.read(1024 * 1024):
                        written += len(block)
                        require(written <= spec['size_bytes'], 'VM image exceeds its pinned size')
                        output.write(block)
                    require(written == spec['size_bytes'], 'VM image size mismatch')
                with temporary.open('rb') as stream:
                    require(hashlib.file_digest(stream, 'sha512').hexdigest() == spec['sha512'], 'VM image checksum mismatch')
                temporary.rename(target); target.chmod(0o444)
            finally:
                temporary.unlink(missing_ok=True)
        with target.open('rb') as stream:
            require(hashlib.file_digest(stream, 'sha512').hexdigest() == spec['sha512'], 'cached VM image checksum mismatch')
    return target


def doctor(value=None):
    with open('/dev/kvm', 'rb+', buffering=0) as device:
        require(fcntl.ioctl(device, 0xAE00, 0) == 12, 'KVM API unavailable')
    engine = json.loads(capture(['docker', 'info', '--format', '{{json .}}']))
    require(engine['OSType'] == 'linux' and 'Docker Desktop' not in engine.get('OperatingSystem', ''), 'native Linux Docker Engine required')
    memory = {line.split(':')[0]: int(line.split()[1]) for line in Path('/proc/meminfo').read_text().splitlines() if ':' in line and len(line.split()) > 1}
    result = {'cpus': os.cpu_count(), 'memory_available_mib': memory['MemAvailable'] // 1024,
              'disk_available_gib': shutil.disk_usage(CACHE).free // 1024**3, 'kvm': True}
    if value:
        total_memory = sum(node['memory_mib'] for node in value['nodes'])
        total_disk = sum(node['disk_gib'] for node in value['nodes'])
        require(sum(node['cpus'] for node in value['nodes']) <= result['cpus'], 'VM CPU allocation exceeds the host')
        require(total_memory + 8192 <= result['memory_available_mib'], 'insufficient available RAM for VMs and host reserve')
        reservations = capture(['docker', 'ps', '--filter', 'label=' + MANAGED, '--format', '{{.ID}}']).splitlines()
        reserved_memory = 0
        reserved_disk = 0
        for identity in reservations:
            running = json.loads(capture(['docker', 'inspect', identity]))[0]
            labels = running['Config']['Labels']
            reserved_memory += int(labels.get('psim.vm.memory-mib', '0'))
            reserved_disk += int(labels.get('psim.vm.disk-gib', '0'))
        require(total_memory + reserved_memory + 8192 <= memory['MemTotal'] // 1024, 'combined VM reservations exceed host RAM')
        require(total_disk + reserved_disk + 20 <= result['disk_available_gib'], 'combined VM disk reservations exceed available space')
        require(total_disk + 20 <= result['disk_available_gib'], 'insufficient disk space for VM disks and host reserve')
        for node in value['nodes'] + [{'ssh_port': item['host_port']} for item in value.get('forwards', [])]:
            with socket.socket() as probe:
                probe.bind(('127.0.0.1', node['ssh_port']))
    return result


def prepare(value, tag, base):
    state = directory(value['name'])
    desired = profile_digest(value)
    bootstrap_hash = hashlib.sha256((ROOT / 'tools/vm/prepare.py').read_bytes()).hexdigest()
    if state.exists():
        saved = manifest(value['name'])
        require(saved.get('bootstrap_hash') == bootstrap_hash, 'VM bootstrap changed; destroy the owned lab before recreating it')
        require(saved['profile_hash'] == desired, 'VM profile changed; destroy the owned lab before recreating it')
        require(saved['base_sha512'] == base.stem, 'VM backing image changed; recreate the lab')
        if saved.get('forwards', []) != value.get('forwards', []):
            saved['forwards'] = value.get('forwards', [])
            write_manifest(state, saved)
    else:
        state.mkdir(mode=0o700)
        saved = json.loads(json.dumps(value)) | {'managed': 1, 'uid': os.getuid(), 'gid': os.getgid(), 'host_state': str(state),
                         'instance': str(uuid.uuid4()), 'profile_hash': desired, 'bootstrap_hash': bootstrap_hash, 'base_sha512': base.stem}
        for index, node in enumerate(saved['nodes'], 1):
            node['mac'] = '52:54:00:91:00:' + format(index + 16, '02x')
        write_manifest(state, saved)
    run(['docker', 'run', '--rm', '--user', f'{os.getuid()}:{os.getgid()}', '--volume', str(state) + ':/state',
         '--volume', str(base) + ':/images/base.qcow2:ro', '--entrypoint', 'python3', tag, '/opt/psim-platform-vm/prepare.py'])
    return saved


def control(name, action, node=None):
    args = ['docker', 'exec', '--user', f'{os.getuid()}:{os.getgid()}', name, 'python3', '/opt/psim-platform-vm/controller.py', action]
    if node: args.append(node)
    result = json.loads(capture(args))
    require(result['ok'], 'VM control failed')
    return result


def ssh(name, node, command, check=True, capture_output=False, input=None):
    args, address = ssh_connection(name, node, 'ssh')
    if input is not None: args[2:2] = ['--interactive']
    return subprocess.run(args + ['psim@' + address, shlex.join(command)], check=check, capture_output=capture_output, text=True, input=input)


def copy_to(name, node, source, destination):
    source = Path(source).resolve(strict=True)
    require(source.is_file(), 'VM upload source must be a regular file')
    require(re.fullmatch(r'/home/psim/[a-zA-Z0-9_./+~-]+', destination) and '..' not in Path(destination).parts, 'invalid VM upload destination')
    args, address = ssh_connection(name, node, 'scp')
    insert = args.index('--entrypoint')
    args[insert:insert] = ['--volume', str(source) + ':/payload:ro']
    run(args + ['/payload', 'psim@' + address + ':' + destination])


def ssh_connection(name, node, program):
    saved = manifest(name)
    selected = next((n for n in saved['nodes'] if n['name'] == node), None)
    require(selected is not None, 'unknown managed VM')
    current = container(name)
    require(current is not None and current['State']['Running'], 'VM lab is stopped')
    args = ['docker', 'run', '--rm', '--network', 'container:' + name, '--user', f'{os.getuid()}:{os.getgid()}',
            '--volume', str(directory(name)) + ':/state:ro', '--entrypoint', program, current['Image'],
            '-o', 'BatchMode=yes', '-o', 'ConnectTimeout=3', '-o', 'ConnectionAttempts=1', '-o', 'StrictHostKeyChecking=yes',
            '-o', 'HostKeyAlias=' + node, '-o', 'UserKnownHostsFile=/state/known_hosts', '-o', 'IdentitiesOnly=yes',
            '-i', '/state/id_ed25519']
    return args, selected['address']


def wait(name, timeout=300):
    saved = manifest(name)
    pending = {node['name'] for node in saved['nodes']}
    deadline = time.monotonic() + timeout
    while pending and time.monotonic() < deadline:
        with ThreadPoolExecutor(max_workers=len(pending)) as pool:
            results = list(pool.map(lambda node: (node, ssh(name, node, ['test', '-f', '/var/lib/cloud/instance/boot-finished'], False, True)), sorted(pending)))
        for node, result in results:
            if result.returncode == 0:
                check = ssh(name, node, ['sudo', 'cloud-init', 'status', '--wait'], False, True)
                require(check.returncode == 0, node + ': cloud-init failed; inspect its console log')
                ssh(name, node, ['sudo', 'sync'], capture_output=True)
                pending.remove(node); print('VM ready: ' + node, flush=True)
        if pending: time.sleep(2)
    require(not pending, 'VM readiness timed out: ' + ', '.join(sorted(pending)))


def up(value):
    existing = container(value['name'])
    if existing and existing['State']['Running']:
        saved = manifest(value['name'])
        expected = profile_digest(value)
        require(saved['profile_hash'] == expected, 'running VM profile differs from the requested profile')
        require(saved.get('forwards', []) == value.get('forwards', []), 'published ports changed; stop the lab before applying them')
        for node in value['nodes']: control(value['name'], 'start', node['name'])
        wait(value['name']); return
    directory(value['name'])
    print(json.dumps(doctor(value)), flush=True)
    tag = image(); base = base_image(); saved = prepare(value, tag, base)
    if existing: run(['docker', 'rm', existing['Id']], stdout=subprocess.DEVNULL)
    args = ['docker', 'run', '-d', '--name', value['name'], '--label', MANAGED + '=' + value['name'],
            '--label', 'psim.vm.memory-mib=' + str(sum(n['memory_mib'] for n in value['nodes'])),
            '--label', 'psim.vm.disk-gib=' + str(sum(n['disk_gib'] for n in value['nodes'])),
            '--device', '/dev/kvm', '--device', '/dev/net/tun', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges:true',
            '--sysctl', 'net.ipv4.ip_forward=1', '--volume', str(directory(value['name'])) + ':/state', '--volume', str(base) + ':/images/base.qcow2:ro']
    for capability in ('NET_ADMIN', 'NET_BIND_SERVICE', 'SETUID', 'SETGID', 'CHOWN', 'DAC_OVERRIDE', 'KILL'):
        args += ['--cap-add', capability]
    for node in value['nodes']: args += ['--publish', '127.0.0.1:' + str(node['ssh_port']) + ':' + str(node['ssh_port'])]
    for item in value.get('forwards', []): args += ['--publish', f'127.0.0.1:{item["host_port"]}:{item["host_port"]}']
    run(args + [tag], stdout=subprocess.DEVNULL)
    wait(value['name'])


def down(name):
    manifest(name)
    current = container(name)
    if current and current['State']['Running']:
        run(['docker', 'stop', '--time', '55', current['Id']], stdout=subprocess.DEVNULL)
        result = container(name)
        require(result['State']['ExitCode'] == 0, 'VM supervisor did not stop cleanly; disks are preserved')
    print('VM lab stopped; disks preserved')


def destroy(name):
    manifest(name)
    down(name)
    current = container(name)
    if current: run(['docker', 'rm', current['Id']], stdout=subprocess.DEVNULL)
    state = directory(name)
    require(state.parent.resolve() == CACHE.resolve(), 'invalid VM state location')
    shutil.rmtree(state)
    print('owned VM lab and disks removed: ' + name)


def main():
    parser = argparse.ArgumentParser(prog='psim-platform-vm')
    parser.add_argument('--profile', default=str(ROOT / 'deploy/vm/local.json'))
    actions = parser.add_subparsers(dest='action', required=True)
    for action in ('up', 'down', 'destroy', 'status', 'doctor', 'wait', 'image'):
        actions.add_parser(action)
    command = actions.add_parser('exec'); command.add_argument('--node', required=True); command.add_argument('command', nargs=argparse.REMAINDER)
    command = actions.add_parser('node'); command.add_argument('operation', choices=['start', 'stop', 'kill']); command.add_argument('--node', required=True)
    args = parser.parse_args(); value = profile(args.profile); name = value['name']
    if args.action == 'up': up(value)
    elif args.action == 'down': down(name)
    elif args.action == 'destroy': destroy(name)
    elif args.action == 'doctor': directory(name); print(json.dumps(doctor(value), indent=2))
    elif args.action == 'wait': wait(name)
    elif args.action == 'image': print(image())
    elif args.action == 'node': print(json.dumps(control(name, args.operation, args.node)))
    elif args.action == 'exec':
        command = args.command
        if command[:1] == ['--']: command = command[1:]
        if not command: command = shlex.split(os.environ.get('PSIM_VM_COMMAND', 'uname -a'))
        sys.exit(ssh(name, args.node, command).returncode)
    else:
        current = container(name)
        print(json.dumps(control(name, 'status') if current and current['State']['Running'] else {'running': False, 'disks_preserved': directory(name).exists()}, indent=2))


if __name__ == '__main__':
    try: main()
    except subprocess.CalledProcessError as error:
        print('psim-platform-vm: command failed with exit ' + str(error.returncode) + '; diagnostic files are preserved', file=sys.stderr); sys.exit(1)
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, json.JSONDecodeError) as error:
        print('psim-platform-vm: ' + str(error), file=sys.stderr); sys.exit(1)
