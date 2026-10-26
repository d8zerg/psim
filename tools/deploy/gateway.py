"""Run the gateway load probe against the owned VM cluster with verified TLS."""
import argparse
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import lab
import local
from material import verify_bundle
from model import SERVICES, require


def command(bundle, profile, holder):
    image = 'psim-platform/toolchain:debian13-' + __import__('hashlib').sha256((local.ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    return ['docker', 'run', '--rm', '--network', 'container:psim-platform-vm', '--read-only', '--cap-drop', 'ALL',
            '--security-opt', 'no-new-privileges:true', '--user', str(profile.uid('deployment-admin')) + ':' + str(profile.uid('deployment-admin')),
            '--tmpfs', '/tmp:rw,nosuid,nodev,size=64m,mode=1777',
            '--env', 'LD_PRELOAD=', '--env', 'PSIM_GATEWAY_PROBE_CONFIG=/case/config.json',
            '--mount', 'type=bind,src=' + str(local.ROOT) + ',dst=/repo,readonly',
            '--mount', 'type=bind,src=' + str(holder / 'material/qualification') + ',dst=/case',
            '--mount', 'type=bind,src=' + str(bundle / profile.nodes[0].name / 'hosts') + ',dst=/etc/hosts,readonly',
            '--workdir', '/repo', '--entrypoint', 'python3', image]


def prepare(bundle, profile, holder):
    local.vm.run(['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
        '--cap-drop', 'ALL', '--cap-add', 'CHOWN', '--cap-add', 'DAC_OVERRIDE', '--security-opt', 'no-new-privileges:true',
        '--env', 'LD_PRELOAD=', '--tmpfs', '/tmp:rw,nosuid,size=32m,mode=1777',
        '--mount', 'type=bind,src=' + str(local.ROOT / 'tools/deploy') + ',dst=/code,readonly',
        '--mount', 'type=bind,src=' + str(bundle) + ',dst=/bundle,readonly',
        '--mount', 'type=bind,src=' + str(holder) + ',dst=/private', '--entrypoint', 'python3',
        'psim-platform/postgres:' + profile.version, '/code/qualification_setup.py', 'prepare', '--bundle', '/bundle', '--material', '/private/material'])


def bench():
    state = lab.read_state()
    require(state is not None and state['topology'] == 'cluster' and state['status'] == 'ready', 'gateway benchmark requires the ready owned cluster')
    require(state.get('runtime', 'containers') == 'containers', 'gateway benchmark requires container-managed application replicas')
    bundle, holder = lab.paths('cluster'); profile = verify_bundle(bundle)
    require(lab.digest(bundle / 'manifest.json') == state['manifest'], 'qualification bundle differs from the installed cluster')
    prepare(bundle, profile, holder)
    runtime = command(bundle, profile, holder)
    local.vm.run(runtime + ['/repo/tools/deploy/qualification_setup.py', 'token'])
    index = runtime.index('--entrypoint')
    runtime[index + 1] = '/repo/build/debian13/release/services/connector-gateway/psim-gateway-probe'
    report = local.ROOT / 'build/deployment/gateway-benchmark.log'
    print('Input qualification boundary: two gateways -> durable Kafka acknowledgements; other application services stopped', flush=True)
    stopped = []
    downstream = sorted(set(SERVICES) - {'connector-gateway'})
    try:
        for node in profile.select('application'):
            stopped.append(node)
            local.compose(profile, node.name, ['stop', *downstream])
        with report.open('w') as output:
            result = subprocess.run(runtime + ['bench'], stdout=output, stderr=subprocess.STDOUT, timeout=240, text=True)
    finally:
        for node in stopped: local.compose(profile, node.name, ['up', '-d', *downstream])
        lab.wait_operation(bundle, 'applications')
    print(report.read_text(), end='')
    require(result.returncode == 0, 'VM gateway benchmark or durable-event reconciliation failed')


def sessions(profile, node):
    program = "import urllib.request;data=urllib.request.urlopen('http://" + node.address + ":19100/metrics',timeout=3).read().decode();print(next(line.split()[1] for line in data.splitlines() if line.startswith('psim_gateway_sessions ')))"
    return int(float(local.execute(profile.select('infrastructure')[-1].name, ['python3', '-c', program], capture_output=True).stdout.strip()))


def failover():
    state = lab.read_state()
    require(state is not None and state['topology'] == 'cluster' and state['status'] == 'ready', 'gateway failover requires the ready owned cluster')
    require(state.get('runtime', 'containers') == 'containers', 'gateway failover requires container-managed application replicas')
    bundle, holder = lab.paths('cluster'); profile = verify_bundle(bundle)
    runtime = command(bundle, profile, holder)
    prepare(bundle, profile, holder)
    local.vm.run(runtime + ['/repo/tools/deploy/qualification_setup.py', 'token'])
    runtime[runtime.index('--entrypoint') + 1] = '/repo/build/debian13/release/services/connector-gateway/psim-gateway-probe'
    stopped = []; active = None; process = None
    probe_name = 'psim-platform-gateway-failover-probe'
    require(not local.vm.capture(['docker', 'container', 'ls', '--all', '--filter', 'name=^/' + probe_name + '$', '--format', '{{.ID}}']),
            'gateway qualification container name is already in use')
    runtime[2:2] = ['--name', probe_name, '--label', 'psim.qualification=' + state['material_id']]
    report = local.ROOT / 'build/deployment/gateway-failover.log'
    try:
        for node in profile.select('application'):
            require(sessions(profile, node) == 0, 'another connector transport is active')
            local.compose(profile, node.name, ['stop', 'normalizer']); stopped.append(node)
        with report.open('w') as output:
            process = subprocess.Popen(runtime + ['failover'], stdout=output, stderr=subprocess.STDOUT, text=True)
            deadline = time.monotonic() + 30
            while 'READY' not in report.read_text():
                require(process.poll() is None and time.monotonic() < deadline, 'SDK did not establish its initial session')
                time.sleep(0.05)
            owners = [node for node in profile.select('application') if sessions(profile, node) == 1]
            require(len(owners) == 1, 'cannot identify the active gateway instance')
            active = owners[0]
            container = profile.name + '-' + active.name + '-connector-gateway-1'
            local.execute(active.name, ['sudo', 'docker', 'update', '--restart=no', container], capture_output=True)
            local.execute(active.name, ['sudo', 'docker', 'kill', '--signal', 'KILL', container], capture_output=True)
            require(process.wait(timeout=120) == 0, 'SDK failover or Kafka reconciliation failed')
        print(report.read_text(), end='')
        (local.ROOT / 'build/deployment/gateway-failover.json').write_text(json.dumps({'case': 'gateway-process-loss',
            'killed': active.name, 'survivor': next(node.name for node in profile.select('application') if node != active),
            'events': 1000, 'reconciled': True, 'automatic_restart_disabled_during_fault': True}, indent=2) + '\n')
    finally:
        if process is not None and process.poll() is None:
            identities = local.vm.capture(['docker', 'container', 'ls', '--all', '--filter', 'name=^/' + probe_name + '$', '--format', '{{.ID}}']).splitlines()
            for identity in identities:
                metadata = json.loads(local.vm.capture(['docker', 'inspect', identity]))[0]
                require(metadata['Config']['Labels'].get('psim.qualification') == state['material_id'], 'foreign gateway qualification container')
                local.vm.run(['docker', 'rm', '--force', identity], capture_output=True)
            process.terminate(); process.wait(timeout=10)
        if active:
            container = profile.name + '-' + active.name + '-connector-gateway-1'
            local.execute(active.name, ['sudo', 'docker', 'update', '--restart=unless-stopped', container], capture_output=True)
            local.compose(profile, active.name, ['up', '-d', 'connector-gateway'])
        for node in stopped: local.compose(profile, node.name, ['up', '-d', 'normalizer'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=['bench', 'failover']); args = parser.parse_args()
    with lab.exclusive():
        if args.action == 'bench': bench()
        else: failover()
