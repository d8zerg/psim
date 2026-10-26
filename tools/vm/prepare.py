"""Create private NoCloud media and copy-on-write disks in the managed state directory."""
import json
import os
from pathlib import Path
import subprocess
import sys


def run(*args):
    subprocess.run(args, check=True, stdout=subprocess.DEVNULL)


def main():
    state = Path('/state')
    profile = json.loads((state / 'manifest.json').read_text())
    if not (state / 'id_ed25519').exists():
        run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-C', 'psim-platform-vm-access', '-f', str(state / 'id_ed25519'))
    public = (state / 'id_ed25519.pub').read_text().strip()
    known = []
    ssh_config = []
    for index, node in enumerate(profile['nodes'], 1):
        directory = state / node['name']
        directory.mkdir(exist_ok=True, mode=0o700)
        host_key = directory / 'ssh_host_ed25519_key'
        if not host_key.exists():
            run('ssh-keygen', '-q', '-t', 'ed25519', '-N', '', '-C', node['name'], '-f', str(host_key))
        host_public = host_key.with_suffix('.pub').read_text().strip()
        known.append(node['name'] + ' ' + host_public)
        ssh_config.append(f'''Host {node['name']}
  HostName 127.0.0.1
  Port {node['ssh_port']}
  User psim
  IdentityFile {profile['host_state']}/id_ed25519
  UserKnownHostsFile {profile['host_state']}/known_hosts
  HostKeyAlias {node['name']}
  StrictHostKeyChecking yes
  IdentitiesOnly yes
''')
        user = {'hostname': node['name'], 'manage_etc_hosts': True, 'disable_root': True, 'ssh_pwauth': False,
                'ssh_deletekeys': False, 'ssh_genkeytypes': ['ed25519'],
                'ssh_keys': {'ed25519_private': host_key.read_text(), 'ed25519_public': host_public},
                'users': [{'name': 'psim', 'groups': ['sudo'], 'sudo': ['ALL=(ALL) NOPASSWD:ALL'],
                           'shell': '/bin/bash', 'lock_passwd': True, 'ssh_authorized_keys': [public]}],
                'package_update': False, 'package_upgrade': False,
                'write_files': [{'path': '/etc/ssh/sshd_config.d/20-psim.conf', 'permissions': '0644',
                                 'content': 'PasswordAuthentication no\nPermitRootLogin no\n'}],
                'runcmd': [['systemctl', 'enable', '--now', 'ssh']]}
        network = {'version': 2, 'ethernets': {'psim0': {'match': {'macaddress': node['mac']}, 'set-name': 'psim0',
                   'dhcp4': False, 'dhcp6': False, 'addresses': [node['address'] + '/24'],
                   'routes': [{'to': 'default', 'via': profile['gateway']}],
                   'nameservers': {'addresses': [profile['gateway']]}}}}
        for name, value in [('user-data', '#cloud-config\n' + json.dumps(user)),
                            ('meta-data', json.dumps({'instance-id': profile['instance'] + '-' + node['name'], 'local-hostname': node['name']})),
                            ('network-config', json.dumps(network))]:
            path = directory / name
            path.write_text(value + '\n')
            path.chmod(0o600)
        seed = directory / 'seed.iso'
        if not seed.exists():
            run('cloud-localds', '--network-config=' + str(directory / 'network-config'), str(seed), str(directory / 'user-data'), str(directory / 'meta-data'))
            seed.chmod(0o600)
        disk = directory / 'disk.qcow2'
        if not disk.exists():
            run('qemu-img', 'create', '-f', 'qcow2', '-F', 'qcow2', '-b', '/images/base.qcow2', str(disk), str(node['disk_gib']) + 'G')
            disk.chmod(0o600)
    (state / 'known_hosts').write_text('\n'.join(known) + '\n')
    (state / 'ssh_config').write_text('\n'.join(ssh_config))
    for name in ('known_hosts', 'ssh_config', 'id_ed25519'):
        (state / name).chmod(0o600)


if __name__ == '__main__':
    os.umask(0o077)
    main()
