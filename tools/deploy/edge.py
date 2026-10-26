"""Configure native VRRP on the deployment's edge VMs."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local
from material import verify_bundle


def configure(bundle):
    profile = verify_bundle(bundle)
    if profile.topology != 'cluster': return
    package = json.loads((local.ROOT / 'deploy/topologies/packages.json').read_text())['keepalived']
    folder = local.ROOT / 'build/deployment/units'
    folder.mkdir(parents=True, exist_ok=True)
    for node in profile.select('edge'):
        print('Configuring native VRRP: ' + node.name, flush=True)
        local.execute(node.name, ['sudo', 'apt-get', 'install', '--yes', '--no-install-recommends', 'keepalived=' + package])
        filename = folder / ('psim-keepalived-' + node.name + '.service')
        filename.write_text(f'''[Unit]
Description=Open-Source PSIM Platform VRRP
Wants=network-online.target
After=network-online.target docker.service

[Service]
Type=simple
RuntimeDirectory=psim-keepalived
ExecStart=/usr/sbin/keepalived --dont-fork --log-console --vrrp --use-file {profile.prefix}/config/{node.name}/keepalived.conf --pid /run/psim-keepalived/keepalived.pid --vrrp_pid /run/psim-keepalived/vrrp.pid --checkers_pid /run/psim-keepalived/checkers.pid --bfd_pid /run/psim-keepalived/bfd.pid
Restart=on-failure
RestartSec=1s
TimeoutStopSec=10s
NoNewPrivileges=true
CapabilityBoundingSet=CAP_NET_ADMIN CAP_NET_RAW CAP_NET_BIND_SERVICE
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
RestrictSUIDSGID=true

[Install]
WantedBy=multi-user.target
''')
        destination = '/home/psim/.cache/psim-delivery/' + filename.name
        local.vm.copy_to('psim-platform-vm', node.name, filename, destination)
        config = Path(bundle) / node.name / 'keepalived.conf'
        staged_config = '/home/psim/.cache/psim-delivery/keepalived.conf'
        local.vm.copy_to('psim-platform-vm', node.name, config, staged_config)
        local.execute(node.name, ['sudo', 'keepalived', '--config-test', '--use-file', staged_config])
        local.execute(node.name, ['sudo', 'install', '-m', '0644', staged_config, profile.prefix + '/config/' + node.name + '/keepalived.conf'])
        local.execute(node.name, ['sudo', 'install', '-m', '0644', destination, '/etc/systemd/system/psim-keepalived.service'])
        local.execute(node.name, ['sudo', 'systemctl', 'daemon-reload'])
        local.execute(node.name, ['sudo', 'systemctl', 'enable', '--now', 'psim-keepalived.service'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('--bundle', required=True)
    configure(parser.parse_args().bundle)
