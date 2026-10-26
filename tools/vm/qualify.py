"""Exercise real KVM guests in an isolated namespace, then remove only that namespace."""
import json
from pathlib import Path
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vm


def qualify():
    value = vm.profile(vm.ROOT / 'deploy/vm/local.json')
    name = 'psim-platform-vm-test'
    value['name'] = name
    for index, node in enumerate(value['nodes']):
        node['ssh_port'] = 2311 + index
    value['forwards'] = [{'name': 'ssh-check', 'host_port': 2344, 'target_address': value['nodes'][2]['address'], 'target_port': 22}]
    vm.require(not vm.directory(name).exists() and vm.container(name) is None, 'qualification namespace already exists; inspect it before removal')
    report = {'profile': value, 'checks': [], 'result': 'failed'}
    output = vm.ROOT / 'build/vm/qualification.json'
    output.parent.mkdir(parents=True, exist_ok=True)

    def execute(node, command):
        return vm.ssh(name, node, command, capture_output=True).stdout.strip()

    def passed(check):
        report['checks'].append(check)
        print('PASS: ' + check, flush=True)

    try:
        vm.up(value)
        guests = {}
        probe = "import json,os,pathlib;print(json.dumps({'cpus':os.cpu_count(),'virtualization':os.popen('systemd-detect-virt').read().strip(),'watchdog':pathlib.Path('/sys/class/watchdog/watchdog0/identity').read_text().strip(),'boot_id':pathlib.Path('/proc/sys/kernel/random/boot_id').read_text().strip(),'disk_bytes':int(pathlib.Path('/sys/block/vda/size').read_text())*512,'memory_mib':int(pathlib.Path('/proc/meminfo').read_text().splitlines()[0].split()[1])//1024}))"
        for node in value['nodes']:
            actual = json.loads(execute(node['name'], ['python3', '-c', probe]))
            vm.require(actual['cpus'] == node['cpus'] and actual['virtualization'] == 'kvm', 'unexpected guest CPU or hypervisor')
            vm.require(node['memory_mib'] - 512 <= actual['memory_mib'] <= node['memory_mib'], 'unexpected guest RAM')
            vm.require('i6300' in actual['watchdog'].lower(), 'hardware watchdog is unavailable')
            vm.require(actual['disk_bytes'] == node['disk_gib'] * 1024**3, 'unexpected guest disk capacity')
            guests[node['name']] = actual
        vm.require(len({node['boot_id'] for node in guests.values()}) == 3, 'guest boot identities must differ')
        report['guests'] = guests
        passed('three independent KVM guests with the declared CPU, RAM and disk capacity')
        vm.up(value)
        for node in value['nodes']:
            vm.require(json.loads(execute(node['name'], ['python3', '-c', probe]))['boot_id'] == guests[node['name']]['boot_id'], 'repeated up rebooted a running guest')
        passed('repeated up preserves the existing guests')

        token = uuid.uuid4().hex
        execute('node-1', ['sh', '-c', 'printf %s ' + token + ' > /home/psim/persistence-check; sync'])
        server = "import socket;s=socket.socket();s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1);s.bind(('0.0.0.0',32000));s.listen(2);s.settimeout(90);\nfor _ in range(2):\n c,_=s.accept();c.sendall(c.recv(1024));c.close()"
        execute('node-3', ['sudo', 'systemd-run', '--unit', 'psim-peer-check', '--property', 'RuntimeMaxSec=100s', '--collect', 'python3', '-c', server])
        client = "import socket,time\nfor attempt in range(50):\n try:\n  s=socket.create_connection(('10.91.0.13',32000),1);s.sendall(b'peer-proof');assert s.recv(1024)==b'peer-proof';break\n except ConnectionRefusedError:time.sleep(.1)\nelse:raise RuntimeError('peer unavailable')"
        execute('node-2', ['python3', '-c', client])
        passed('TCP communication between guest nodes')
        connection, _ = vm.ssh_connection(name, 'node-3', 'ssh')
        connection[connection.index('--network') + 1] = 'host'
        forwarded = vm.run(connection + ['-p', '2344', 'psim@127.0.0.1', 'printf forwarded'], capture_output=True)
        vm.require(forwarded.stdout == 'forwarded', 'published guest port is unavailable on localhost')
        passed('localhost port forwarding reaches the SSH-pinned guest')
        vm.control(name, 'kill', 'node-1')
        deadline = time.monotonic() + 10
        while any(n['name'] == 'node-1' and n['running'] for n in vm.control(name, 'status')['nodes']) and time.monotonic() < deadline:
            time.sleep(.2)
        state = {n['name']: n['running'] for n in vm.control(name, 'status')['nodes']}
        vm.require(state == {'node-1': False, 'node-2': True, 'node-3': True}, 'power loss affected another VM')
        execute('node-2', ['python3', '-c', client])
        passed('abrupt loss of one VM preserves peer operation and networking')
        vm.control(name, 'start', 'node-1')
        vm.wait(name)
        vm.require(execute('node-1', ['cat', '/home/psim/persistence-check']) == token, 'guest data lost after power loss')
        passed('guest disk survives abrupt power loss and restart')
        vm.down(name)
        vm.up(value)
        vm.require(execute('node-1', ['cat', '/home/psim/persistence-check']) == token, 'guest data lost after down/up')
        passed('clean down/up preserves guest data and SSH identities')
        report['result'] = 'passed'
    finally:
        if vm.directory(name).exists():
            if (vm.directory(name) / 'manifest.json').exists():
                vm.destroy(name)
            else:
                vm.directory(name).rmdir()
        vm.require(not vm.directory(name).exists() and vm.container(name) is None, 'qualification cleanup incomplete')
        passed('destroy removes only the qualification namespace')
        output.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    qualify()
