"""Verify private floor objects and independent SeaweedFS process failures on the owned cluster."""
import json
from pathlib import Path
import re
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gateway
import lab
import local
from material import verify_bundle
from model import require


def scenario(runtime, action):
    command = runtime.copy()
    if action != 'anonymous': command[command.index('--network') + 1] = 'host'
    result = subprocess.run(command + ['/repo/tools/deploy/scenario.py', action], capture_output=True, text=True)
    require(result.returncode == 0, 'public scenario failed: ' + result.stdout[-500:] + result.stderr[-1500:])
    return result.stdout.strip()


def shell(runtime, profile, holder, text):
    command = runtime.copy()
    command[command.index('--entrypoint') + 1] = '/usr/bin/weed'
    command[-1] = json.loads((local.ROOT / 'deploy/topologies/images.json').read_text())['seaweed']
    command[2:2] = ['--interactive', '--mount', 'type=bind,src=' + str(holder / 'material/qualification/seaweed-client.toml')
                    + ',dst=/etc/seaweedfs/security.toml,readonly']
    result = local.vm.run(command + ['shell', '-master=' + ','.join(node.name + ':9333' for node in profile.select('infrastructure'))],
                          input=text, capture_output=True)
    require('error:' not in (result.stdout + result.stderr).lower(), 'SeaweedFS administrative command failed')
    return result.stdout


def volumes(text):
    result = {}; node = rack = None
    for line in text.splitlines():
        if match := re.match(r'\s+Rack (\S+) hdd', line): rack = match[1]
        if match := re.match(r'\s+DataNode (\S+) hdd', line): node = match[1]
        if match := re.match(r'\s+volume Id:(\d+),.*ReplicaPlacement:(\d+), Collection:([^,]*),', line):
            require(node is not None and rack is not None, 'volume topology is missing its node or rack')
            require(match[2] == '010', 'cluster volume has an unexpected replication placement')
            result.setdefault(match[1], []).append({'node': node, 'rack': rack, 'collection': match[3]})
    require(result and any(copy['collection'] == 'psim-plans' for copies in result.values() for copy in copies),
            'floor object volumes are absent from the topology')
    for copies in result.values():
        require(len({copy['node'] for copy in copies}) >= 2 and len({copy['rack'] for copy in copies}) >= 2,
                'volume replicas are not restored on distinct racks')
    return result


def run():
    state = lab.read_state()
    require(state is not None and state['topology'] == 'cluster' and state['status'] == 'ready',
            'storage qualification requires the ready owned cluster')
    bundle, holder = lab.paths('cluster'); profile = verify_bundle(bundle)
    require(lab.digest(bundle / 'manifest.json') == state['manifest'], 'storage qualification bundle differs from the installed cluster')
    gateway.prepare(bundle, profile, holder)
    runtime = gateway.command(bundle, profile, holder)
    print(scenario(runtime, 'floor'), flush=True)
    print(scenario(runtime, 'anonymous'), flush=True)
    before = volumes(shell(runtime, profile, holder, 'volume.list\n'))
    checks = []
    for service in ('seaweed-volume', 'seaweed-filer', 'seaweed-s3'):
        for node in profile.nodes:
            if service not in json.loads((bundle / node.name / 'compose.json').read_text())['services']: continue
            try:
                local.compose(profile, node.name, ['stop', service])
                # Allow the front proxy to observe its configured health-check threshold.
                time.sleep(3)
                started = time.monotonic()
                print(node.name + ' ' + service + ': ' + scenario(runtime, 'read'), flush=True)
                checks.append({'node': node.name, 'service': service, 'read_seconds': round(time.monotonic() - started, 3)})
            finally:
                local.compose(profile, node.name, ['up', '-d', service])
                lab.wait_operation(bundle, 'core')
    shell(runtime, profile, holder, 'volume.fix.replication\n')
    after = volumes(shell(runtime, profile, holder, 'volume.list\n'))
    require(before.keys() == after.keys(), 'a volume disappeared during process recovery')
    print(scenario(runtime, 'upload'), flush=True)
    print(scenario(runtime, 'anonymous'), flush=True)
    report = {'case': 'private-floor-storage-process-failures', 'checks': checks, 'volume_count': len(after),
              'replicas_on_distinct_racks': True, 'anonymous_existing_object_status': 403, 'new_version_after_recovery': True}
    (local.ROOT / 'build/deployment/storage-qualification.json').write_text(json.dumps(report, indent=2) + '\n')
    print('Private storage recovery and volume replica placement verified', flush=True)


if __name__ == '__main__':
    with lab.exclusive(): run()
