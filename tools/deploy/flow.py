"""Exercise a fresh connector, floor and incident through the owned deployment's public APIs."""
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gateway
import lab
import local
import storage
from material import verify_bundle
from model import require


def run():
    state = lab.read_state()
    require(state is not None and state['status'] == 'ready', 'end-to-end qualification requires a ready owned deployment')
    bundle, holder = lab.paths(state['topology']); profile = verify_bundle(bundle)
    require(lab.digest(bundle / 'manifest.json') == state['manifest'], 'end-to-end bundle differs from the installed deployment')
    gateway.prepare(bundle, profile, holder)
    runtime = gateway.command(bundle, profile, holder)
    local.vm.run(runtime + ['/repo/tools/deploy/qualification_setup.py', 'token'])
    probe = runtime.copy(); probe[probe.index('--entrypoint') + 1] = '/repo/build/debian13/release/services/connector-gateway/psim-gateway-probe'
    local.vm.run(probe + ['enroll'])
    print(storage.scenario(runtime, 'setup'), flush=True)
    # Let every service observe the committed catalog and configuration records.
    time.sleep(2)
    local.vm.run(probe + ['smoke'])
    print(storage.scenario(runtime, 'verify'), flush=True)
    print(storage.scenario(runtime, 'anonymous'), flush=True)
    report = {'case': 'fresh-installation-end-to-end', 'topology': state['topology'], 'runtime': state.get('runtime', 'containers'),
              'manifest': state['manifest'], 'images': state['images'],
              'packages': state.get('packages', {}) if state.get('runtime') == 'systemd' else {},
              'sdk_acknowledged': 1, 'normalized_history': True, 'incident': True,
              'console_projection': True, 'floor_checksum': True, 'anonymous_object_status': 403}
    (local.ROOT / ('build/deployment/flow-' + state['topology'] + '-' + state.get('runtime', 'containers') + '.json')).write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    with lab.exclusive(): run()
