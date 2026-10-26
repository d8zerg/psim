"""Manage QEMU children and a VM-only bridge inside the container network namespace."""
import json
import os
from pathlib import Path
import select
import signal
import socket
import subprocess
import sys
import time

STATE = Path('/state')
CONTROL = STATE / 'control.sock'


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def qmp(node, command):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(3)
        connection.connect(str(STATE / node / 'qmp.sock'))
        stream = connection.makefile('rwb')
        json.loads(stream.readline())
        for message in ('qmp_capabilities', command):
            stream.write(json.dumps({'execute': message}).encode() + b'\n'); stream.flush()
            while True:
                response = json.loads(stream.readline())
                if 'return' in response:
                    break
                if 'error' in response:
                    raise RuntimeError('QMP rejected the request')


class Controller:
    def __init__(self):
        self.profile = json.loads((STATE / 'manifest.json').read_text())
        self.children = {}
        self.stopping = False
        self.dns = None

    def network(self):
        profile = self.profile
        run('ip', 'link', 'add', 'psim0', 'type', 'bridge')
        run('ip', 'address', 'add', profile['gateway'] + '/24', 'dev', 'psim0')
        run('ip', 'link', 'set', 'psim0', 'up')
        run('iptables', '-t', 'nat', '-A', 'POSTROUTING', '-s', profile['network'], '-o', 'eth0', '-j', 'MASQUERADE')
        run('iptables', '-t', 'nat', '-A', 'POSTROUTING', '-d', profile['network'], '-m', 'conntrack', '--ctstate', 'DNAT', '-j', 'SNAT', '--to-source', profile['gateway'])
        for index, node in enumerate(profile['nodes'], 1):
            tap = 'tap' + str(index)
            run('ip', 'tuntap', 'add', tap, 'mode', 'tap', 'user', str(profile['uid']))
            run('ip', 'link', 'set', tap, 'master', 'psim0')
            run('ip', 'link', 'set', tap, 'up')
            run('iptables', '-t', 'nat', '-A', 'PREROUTING', '-p', 'tcp', '--dport', str(node['ssh_port']), '-j', 'DNAT', '--to-destination', node['address'] + ':22')
        for item in profile.get('forwards', []):
            run('iptables', '-t', 'nat', '-A', 'PREROUTING', '-p', 'tcp', '--dport', str(item['host_port']), '-j', 'DNAT', '--to-destination', item['target_address'] + ':' + str(item['target_port']))
        upstream = next((line.split()[1] for line in Path('/etc/resolv.conf').read_text().splitlines() if line.startswith('nameserver ')), '127.0.0.11')
        args = ['dnsmasq', '--keep-in-foreground', '--no-hosts', '--no-resolv', '--bind-interfaces', '--listen-address=' + profile['gateway'],
                '--user=nobody', '--group=nogroup', '--server=' + upstream, '--pid-file=/tmp/dnsmasq.pid']
        for node in profile['nodes']:
            args.append('--host-record=' + node['name'] + ',' + node['address'])
        self.dns = subprocess.Popen(args)

    def start(self, name):
        node = next((n for n in self.profile['nodes'] if n['name'] == name), None)
        if node is None:
            raise ValueError('unknown managed VM')
        if name in self.children and self.children[name].poll() is None:
            return
        directory = STATE / name
        (directory / 'qmp.sock').unlink(missing_ok=True)
        index = self.profile['nodes'].index(node) + 1
        args = ['qemu-system-x86_64', '-name', self.profile['name'] + '-' + name, '-machine', 'q35,accel=kvm', '-cpu', 'host',
                '-smp', str(node['cpus']), '-m', str(node['memory_mib']), '-display', 'none', '-nodefaults', '-no-user-config',
                '-rtc', 'base=utc,clock=host', '-qmp', 'unix:' + str(directory / 'qmp.sock') + ',server=on,wait=off',
                '-device', 'VGA', '-device', 'i6300esb', '-watchdog-action', 'reset', '-serial', 'file:' + str(directory / 'console.log'),
                '-drive', 'file=' + str(directory / 'disk.qcow2') + ',if=virtio,format=qcow2,cache=none',
                '-drive', 'file=' + str(directory / 'seed.iso') + ',if=virtio,media=cdrom,readonly=on',
                '-netdev', 'tap,id=net0,ifname=tap' + str(index) + ',script=no,downscript=no',
                '-device', 'virtio-net-pci,netdev=net0,mac=' + node['mac']]
        log = (directory / 'qemu.log').open('ab')
        self.children[name] = subprocess.Popen(args, user=self.profile['uid'], group=self.profile['gid'],
            extra_groups=[os.stat('/dev/kvm').st_gid], stdout=log, stderr=log)
        log.close()

    def stop(self, name, force=False):
        child = self.children.get(name)
        if child is not None and child.poll() is None:
            qmp(name, 'quit' if force else 'system_powerdown')

    def status(self):
        return {'nodes': [{'name': node['name'], 'running': node['name'] in self.children and self.children[node['name']].poll() is None,
                          'exit_code': self.children[node['name']].poll() if node['name'] in self.children else None} for node in self.profile['nodes']]}

    def serve(self):
        self.network()
        for node in self.profile['nodes']:
            self.start(node['name'])
        CONTROL.unlink(missing_ok=True)
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as server:
            server.bind(str(CONTROL)); CONTROL.chmod(0o600); os.chown(CONTROL, self.profile['uid'], self.profile['gid'])
            server.listen(8)
            while not self.stopping:
                if self.dns.poll() is not None:
                    raise RuntimeError('VM DNS process exited')
                if not select.select([server], [], [], 1)[0]:
                    continue
                connection, _ = server.accept()
                with connection:
                    connection.settimeout(3)
                    try:
                        raw = connection.recv(4096)
                        request = json.loads(raw)
                        action = request.get('action')
                        if action == 'start': self.start(request['node'])
                        elif action in ('stop', 'kill'): self.stop(request['node'], action == 'kill')
                        elif action == 'shutdown': self.stopping = True
                        elif action != 'status': raise ValueError('unsupported VM action')
                        response = {'ok': True, **self.status()}
                    except (ValueError, OSError, RuntimeError, KeyError, json.JSONDecodeError):
                        response = {'ok': False, 'error': 'VM control request failed'}
                    connection.sendall(json.dumps(response).encode())
        for node in self.profile['nodes']:
            try: self.stop(node['name'])
            except (OSError, RuntimeError): pass
        deadline = time.monotonic() + 45
        while any(child.poll() is None for child in self.children.values()) and time.monotonic() < deadline:
            time.sleep(0.2)
        for child in self.children.values():
            if child.poll() is None: child.kill()
            child.wait()
        self.dns.terminate(); self.dns.wait(timeout=5)
        CONTROL.unlink(missing_ok=True)


def request(action, node=None):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5); connection.connect(str(CONTROL))
        connection.sendall(json.dumps({'action': action, 'node': node}).encode())
        return json.loads(connection.recv(65536))


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'daemon':
        controller = Controller()
        signal.signal(signal.SIGTERM, lambda *_: setattr(controller, 'stopping', True))
        signal.signal(signal.SIGINT, lambda *_: setattr(controller, 'stopping', True))
        controller.serve()
    else:
        print(json.dumps(request(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else None)))
