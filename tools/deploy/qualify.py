"""Qualify role failover on the owned local VM deployment."""
import argparse
import json
from pathlib import Path
import sys
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local
from material import verify_bundle
from model import SERVICES, require


def writer(profile, observer, node):
    port = 19100 + list(SERVICES).index('projection-service')
    program = '''import urllib.request
with urllib.request.urlopen(%r, timeout=3) as response:
 for line in response.read().decode().splitlines():
  if line.startswith('psim_console_writer_active '):
   print(int(float(line.split()[1]))); break
 else: raise RuntimeError('writer metric missing')
''' % ('http://' + node.address + ':' + str(port) + '/metrics')
    return int(local.execute(observer.name, ['python3', '-c', program], capture_output=True).stdout.strip())


def console_failover(bundle, output):
    import lab
    state = lab.read_state()
    require(state is not None and state['status'] == 'ready' and state.get('runtime', 'containers') == 'containers',
            'console process qualification requires ready container-managed application replicas')
    require(lab.digest(Path(bundle) / 'manifest.json') == state['manifest'], 'console qualification bundle differs from the installed cluster')
    profile = verify_bundle(bundle)
    nodes = profile.select('application')
    require(profile.topology == 'cluster' and len(nodes) == 2, 'console qualification requires two application nodes')
    observer = profile.select('infrastructure')[-1]
    roles = [writer(profile, observer, node) for node in nodes]
    require(sum(roles) == 1, 'exactly one active console writer is required')
    active = nodes[roles.index(1)]
    standby = nodes[roles.index(0)]
    started = time.monotonic()
    local.compose(profile, active.name, ['pause', 'projection-service'])
    try:
        deadline = started + 40
        while writer(profile, observer, standby) != 1:
            require(time.monotonic() < deadline, 'standby did not acquire the expired console writer session')
            time.sleep(0.25)
        elapsed = time.monotonic() - started
    finally:
        local.compose(profile, active.name, ['unpause', 'projection-service'])
    deadline = time.monotonic() + 30
    while [writer(profile, observer, node) for node in nodes] != [int(node == standby) for node in nodes]:
        require(time.monotonic() < deadline, 'resumed writer did not yield to the promoted standby')
        time.sleep(0.25)
    result = {'case': 'console-writer-session-expiry', 'topology': profile.name,
              'suspended': active.name, 'promoted': standby.name, 'takeover_seconds': round(elapsed, 3),
              'resumed_writer_fenced': True, 'scope': 'writer-role; business data continuity is qualified separately'}
    Path(output).parent.mkdir(parents=True, exist_ok=True)
    Path(output).write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('case', choices=['console-failover'])
    args = parser.parse_args()
    import lab
    with lab.exclusive(): console_failover(args.bundle, args.output)
