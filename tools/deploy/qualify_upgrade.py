"""Qualify signed mixed-version replacement and rollback with continuous SDK and public floor reads."""
import argparse
import json
import os
from pathlib import Path
import secrets
import subprocess
import sys
import threading
import time

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gateway
import lab
import local
import storage
from delivery import require
from upgrade import Coordinator


def bootstrap(coordinator):
    needs_credentials = any(local.execute(node.name, ['sudo', 'test', '-f', coordinator.profile.prefix + '/material/'
        + node.name + '/api-gateway/valkey.secret'], capture_output=True, check=False).returncode != 0
        for node in coordinator.profile.select('application'))
    path = lab.CACHE / 'upgrade-credentials.json'
    require(not path.is_symlink(), 'unsafe upgrade credential cache')
    if not path.exists():
        descriptor = os.open(path, os.O_CREAT | os.O_EXCL | os.O_WRONLY, 0o600)
        with os.fdopen(descriptor, 'w') as stream:
            json.dump({name: secrets.token_urlsafe(32) for name in ('valkey', 'sentinel')}, stream)
    require(path.stat().st_uid == os.getuid() and path.stat().st_mode & 0o777 == 0o600, 'foreign upgrade credential cache')
    program = Path(__file__).with_name('upgrade_bootstrap.py')
    destination = '/home/psim/.cache/psim-delivery/upgrade_bootstrap.py'
    for node in coordinator.profile.nodes:
        local.vm.copy_to(local.VM_NAME, node.name, program, destination)
        if needs_credentials:
            local.execute(node.name, ['sudo', 'python3', destination, 'credentials', '--prefix', coordinator.profile.prefix, '--node', node.name],
                          capture_output=True, input=path.read_text())
        if node in coordinator.profile.select('edge'):
            local.execute(node.name, ['sudo', 'python3', destination, 'proxy', '--prefix', coordinator.profile.prefix, '--node', node.name], capture_output=True)
    node = coordinator.profile.select('infrastructure')[0]
    if needs_credentials:
        local.execute(node.name, ['sudo', 'python3', destination, 'acl-load', '--prefix', coordinator.profile.prefix, '--node', node.name], capture_output=True)


def qualify(args):
    state = lab.read_state()
    require(state is not None and state['status'] == 'ready' and state['topology'] == 'cluster', 'upgrade qualification requires the owned ready cluster')
    require(state.get('runtime', 'containers') == args.runtime, 'select the requested application runtime before qualification')
    bundle, holder = lab.paths('cluster')
    coordinator = Coordinator(bundle, args.release, args.baseline, args.trusted_key)
    bootstrap(coordinator)
    coordinator.prepare()
    state['lifecycle'] = {'baseline': str(Path(args.baseline).resolve()), 'candidate': str(Path(args.release).resolve()), 'status': 'prepared'}
    lab.save_state(state)
    if not args.reuse_case: gateway.prepare(bundle, coordinator.profile, holder)
    runtime = gateway.command(bundle, coordinator.profile, holder)
    if args.reuse_case:
        local.vm.run(runtime + ['-c', 'import json,pathlib; p=pathlib.Path("/case"); s=json.loads((p/"scenario.json").read_text()); ids=json.loads((p/"identities.json").read_text()); assert s["connector"]==ids[19] and s.get("load_devices")==10000; assert not (p/"rolling-ledger.txt").exists() or (p/"rolling-ledger.txt").stat().st_size==0, "existing case already published events"'])
    local.vm.run(runtime + ['/repo/tools/deploy/qualification_setup.py', 'token'])
    probe = runtime.copy(); probe[probe.index('--entrypoint') + 1] = '/repo/build/debian13/release/services/connector-gateway/psim-gateway-probe'
    if not args.reuse_case:
        local.vm.run(probe + ['enroll'])
        print(storage.scenario(runtime, 'setup'), flush=True)
        if args.rate >= 50000: print(storage.scenario(runtime, 'setup-load'), flush=True)
    local.vm.run(runtime + ['-c', 'import json,pathlib; p=pathlib.Path("/case/config.json"); v=json.loads(p.read_text()); v["rolling_rate"]=' + str(args.rate) + '; v["rolling_seconds"]=7200; v["rolling_site"]=json.loads(pathlib.Path("/case/scenario.json").read_text())["site"]; p.chmod(0o600); p.write_text(json.dumps(v)); p.chmod(0o400); pathlib.Path("/case/rolling.stop").unlink(missing_ok=True); pathlib.Path("/case/rolling-watch.stop").unlink(missing_ok=True)'])
    output = local.BUILD; output.mkdir(exist_ok=True)
    log = output / ('upgrade-' + args.runtime + '-publisher.log')
    monitor = runtime.copy()
    monitor[monitor.index('--network') + 1] = 'host'
    monitor_log = output / ('upgrade-' + args.runtime + '-availability.log')
    done = threading.Event()
    requests = []
    def read_floor():
        with monitor_log.open('w') as file:
            while not done.is_set():
                started = time.monotonic()
                result = subprocess.run(monitor + ['/repo/tools/deploy/scenario.py', 'read'], text=True, capture_output=True)
                requests.append({'seconds': round(time.monotonic() - started, 3), 'ok': result.returncode == 0})
                if result.returncode: file.write(result.stdout + result.stderr); file.flush()
                done.wait(0.2)
    worker = threading.Thread(target=read_floor)
    upgraded, rolled_back = [], []
    watch_log = output / ('upgrade-' + args.runtime + '-watch.log')
    def await_marker(path, process, marker, timeout=90):
        deadline = time.monotonic() + timeout
        while marker not in path.read_text():
            require(process.poll() is None and time.monotonic() < deadline, 'qualification process did not report ' + marker + ': ' + str(path))
            time.sleep(0.1)
    with log.open('w') as file, watch_log.open('w') as observations:
        watcher = subprocess.Popen(probe + ['watch-rolling'], stdout=observations, stderr=subprocess.STDOUT, text=True)
        try: await_marker(watch_log, watcher, 'READY')
        except BaseException:
            watcher.terminate(); watcher.wait(timeout=30); raise
        process = subprocess.Popen(probe + ['rolling'], stdout=file, stderr=subprocess.STDOUT, text=True)
        try:
            await_marker(log, process, 'READY')
            await_marker(watch_log, watcher, 'FLOW')
            if args.rate >= 50000:
                time.sleep(20)
                require(process.poll() is None, 'publisher stopped during LOAD-50K warmup')
                progress = json.loads(local.vm.run(runtime + ['-c', 'from pathlib import Path; print(Path("/case/rolling-progress.json").read_text())'], capture_output=True).stdout)
                (output / ('upgrade-' + args.runtime + '-warmup.json')).write_text(json.dumps(progress, indent=2) + '\n')
                require(progress['acknowledged_per_second'] >= 50000, 'LOAD-50K warmup did not reach 50000 acknowledged events/s: ' + json.dumps(progress))
            worker.start()
            started = time.monotonic()
            upgraded = coordinator.apply()
            require(process.poll() is None, 'publisher stopped before the upgrade completed')
            rolled_back = coordinator.apply(rollback=True)
            require(process.poll() is None, 'publisher stopped before rollback completed')
            duration = time.monotonic() - started
        except BaseException:
            print('Restoring the baseline after an interrupted qualification', flush=True)
            coordinator.apply(rollback=True)
            raise
        finally:
            done.set()
            if worker.ident: worker.join(timeout=45)
            local.vm.run(runtime + ['-c', 'from pathlib import Path; Path("/case/rolling.stop").touch()'])
            try:
                try: status = process.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    process.terminate(); process.wait(timeout=30); raise
            finally:
                time.sleep(5)
                local.vm.run(runtime + ['-c', 'from pathlib import Path; Path("/case/rolling-watch.stop").touch()'])
                try: watch_status = watcher.wait(timeout=30)
                except subprocess.TimeoutExpired:
                    watcher.terminate(); watcher.wait(timeout=15); raise
    statistics = json.loads(local.vm.run(runtime + ['-c', 'import json,pathlib; print(json.dumps({name:json.loads((pathlib.Path("/case")/(name+".json")).read_text()) for name in ("rolling-result","rolling-watch-result")}))'], capture_output=True).stdout)
    publisher, observed = statistics['rolling-result'], statistics['rolling-watch-result']
    report = {'case': 'signed-upgrade-and-rollback', 'runtime': args.runtime, 'baseline': coordinator.old['version'],
              'candidate': coordinator.new['version'], 'target_events_per_second': args.rate,
              'replaced': upgraded, 'rolled_back': rolled_back, 'seconds': round(duration, 3), 'floor_requests': requests,
              'publisher_log': str(log.relative_to(local.ROOT)), 'raw_and_normalized_reconciliation': status == 0,
              'publisher': publisher, 'observation': observed,
              'load50k_qualified': publisher.get('background_events_per_second', 0) >= 50000
                                   and publisher.get('virtual_devices') == 10000 and publisher.get('sites') == 10,
              'slo_qualified': observed['maximum_signal_gap_ms'] <= 5000 and observed['incident_p99_ms'] <= 1000
                               and observed.get('maximum_window_incident_p99_ms', observed['incident_p99_ms']) <= 1000}
    (output / ('upgrade-' + args.runtime + '-qualification.json')).write_text(json.dumps(report, indent=2) + '\n')
    state = lab.read_state(); state['lifecycle']['status'] = 'rolled-back'; lab.save_state(state)
    require(status == watch_status == 0, 'publication, observation or event reconciliation failed')
    require(requests and all(item['ok'] for item in requests), 'public floor reads failed during rolling replacement: ' + str(monitor_log))
    require(observed['signals'] == observed['incidents'] == publisher.get('signals_expected', publisher['published'])
            and observed['duplicates'] == observed['pending'] == 0,
            'signal and incident reconciliation failed')
    require(report['slo_qualified'], 'rolling update exceeded signal-gap or incident-latency SLO')
    if args.rate >= 50000: require(report['load50k_qualified'], 'measured publication rate does not qualify LOAD-50K')
    print(storage.scenario(runtime, 'verify'), flush=True)
    print('Signed serial replacement and rollback completed under continuous publication', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--release', required=True); parser.add_argument('--baseline', required=True); parser.add_argument('--trusted-key', required=True)
    parser.add_argument('--rate', type=int, default=50000); parser.add_argument('--runtime', choices=['containers', 'systemd'], default='containers')
    parser.add_argument('--reuse-case', action='store_true')
    args = parser.parse_args()
    with lab.exclusive(): qualify(args)
