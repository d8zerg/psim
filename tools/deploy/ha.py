"""Qualify PostgreSQL RTO and RPO by cutting power to the actual primary VM."""
import json
from pathlib import Path
import subprocess
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gateway
import lab
import local
from material import verify_bundle
from model import require


def records(path):
    result = []
    for line in path.read_text().splitlines():
        try:
            value = json.loads(line)
            if 'ack' in value: result.append(value)
        except json.JSONDecodeError: pass
    return result


def qualify():
    state = lab.read_state()
    require(state and state['topology'] == 'cluster' and state['status'] == 'ready', 'PostgreSQL qualification requires the ready owned cluster')
    bundle, holder = lab.paths('cluster'); profile = verify_bundle(bundle)
    gateway.prepare(bundle, profile, holder)
    runtime = gateway.command(bundle, profile, holder)
    runtime[-1] = 'psim-platform/postgres:' + profile.image_version
    name = 'psim-platform-postgres-failover-probe'
    require(not local.vm.capture(['docker', 'container', 'ls', '--all', '--filter', 'name=^/' + name + '$', '--format', '{{.ID}}']),
            'PostgreSQL qualification container name is already in use')
    writer = runtime.copy(); writer[2:2] = ['--name', name, '--label', 'psim.qualification=' + state['material_id']]
    output = local.ROOT / 'build/deployment/postgres-power-loss.log'
    process = None; primary = None; cut = None; recovered = None
    try:
        with output.open('w') as log:
            process = subprocess.Popen(writer + ['/repo/tools/deploy/pg_qualification.py', 'run'], stdout=log, stderr=subprocess.STDOUT, text=True)
            deadline = time.monotonic() + 30
            while len(records(output)) < 5:
                require(process.poll() is None and time.monotonic() < deadline, 'PostgreSQL writer did not confirm initial commits')
                time.sleep(0.1)
            before = records(output)
            primary = next(node for node in profile.select('infrastructure') if node.address == before[-1]['primary'])
            cut = time.monotonic()
            local.vm.control(local.VM_NAME, 'kill', primary.name)
            deadline = cut + 60
            while recovered is None:
                recovered = next((row for row in records(output) if row['monotonic'] >= cut and row['primary'] != primary.address), None)
                require(process.poll() is None and time.monotonic() < deadline, 'PostgreSQL did not resume confirmed writes within 60 seconds')
                time.sleep(0.1)
            while len([row for row in records(output) if row['primary'] == recovered['primary']]) < 5:
                require(process.poll() is None and time.monotonic() < deadline + 5, 'new primary did not sustain confirmed writes')
                time.sleep(0.1)
            local.vm.run(runtime + ['-c', "from pathlib import Path;Path('/case/pg-stop').touch()"])
            require(process.wait(timeout=10) == 0, 'PostgreSQL writer failed')
        reconciled = local.vm.capture(runtime + ['/repo/tools/deploy/pg_qualification.py', 'verify'])
        result = {'case': 'postgres-primary-vm-power-loss', 'failed': primary.name,
                  'promoted': next(node.name for node in profile.nodes if node.address == recovered['primary']),
                  'write_recovery_seconds': round(recovered['monotonic'] - cut, 3), **json.loads(reconciled),
                  'scope': 'three KVM guests on one physical host'}
        (local.ROOT / 'build/deployment/postgres-power-loss.json').write_text(json.dumps(result, indent=2) + '\n')
        print(json.dumps(result), flush=True)
    finally:
        if process is not None and process.poll() is None:
            metadata = json.loads(local.vm.capture(['docker', 'inspect', name]))[0]
            require(metadata['Config']['Labels'].get('psim.qualification') == state['material_id'], 'foreign PostgreSQL qualification container')
            local.vm.run(['docker', 'rm', '--force', metadata['Id']], capture_output=True)
            process.wait(timeout=10)
        if primary is not None:
            local.vm.control(local.VM_NAME, 'start', primary.name)
            local.vm.wait(local.VM_NAME)
            lab.wait_operation(bundle, 'core', 180)
            lab.wait_operation(bundle, 'applications', 180)


if __name__ == '__main__':
    with lab.exclusive(): qualify()
