"""Exercise an owned VM cluster with warmup separated from measured SDK publication."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import threading
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/deploy'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
import gateway
import lab
import local
import storage
from material import verify_bundle
from model import SERVICES, require
from evaluate import PROFILES, evaluate


def run(profile_name, clients=100):
    require(2 <= clients <= 100 and 1000 % clients == 0, 'load profiles require independent clients dividing 1000')
    rate, duration = PROFILES[profile_name]
    output = ROOT / 'build/load' / profile_name
    output.mkdir(parents=True, exist_ok=True)
    report = {'profile': profile_name, 'passed': False, 'target_rate': rate, 'measurement_seconds': duration, 'client_count': clients}
    path = output / 'report.json'
    path.write_text(json.dumps(report, indent=2) + '\n')
    report['host'] = json.loads(subprocess.check_output(['docker', 'info', '--format',
        '{"operating_system":{{json .OperatingSystem}},"cpus":{{.NCPU}},"memory_bytes":{{.MemTotal}}}'], text=True))
    report['vm_configuration'] = json.loads((ROOT / 'deploy/vm/local.json').read_text())['nodes']
    report['revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    report['probe_sha256'] = hashlib.file_digest((ROOT / 'build/debian13/release/services/connector-gateway/psim-gateway-probe').open('rb'), 'sha256').hexdigest()
    report['source_and_observers_share_host_clock'] = True
    path.write_text(json.dumps(report, indent=2) + '\n')
    processes = []
    containers = []
    runtime = None
    stop_resources = threading.Event()
    resources_worker = None
    try:
        state = lab.read_state()
        require(state is not None and state['topology'] == 'cluster' and state['status'] == 'ready',
                'load qualification requires a ready owned cluster; run task deploy:vm:up')
        require(state.get('runtime', 'containers') == 'containers', 'RSS qualification requires container-managed applications')
        bundle, holder = lab.paths('cluster')
        profile = verify_bundle(bundle)
        require(lab.digest(bundle / 'manifest.json') == state['manifest'], 'load bundle differs from installed cluster')
        report.update(manifest=state['manifest'], images=state['images'], runtime=state.get('runtime', 'containers'))
        reusable = '''import json
from pathlib import Path
p=Path('/private/material/qualification')
try:
 c=json.loads((p/'config.json').read_text()); s=json.loads((p/'scenario.json').read_text())
 i=json.loads((p/'identities.json').read_text()); installation=json.loads((p/'installation.json').read_text())
 parent=json.loads(Path('/private/material/installation.json').read_text())
 print(c.get('load_devices') is True and s.get('load_devices')==10000 and s['connector']==i[19] and installation['id']==parent['id'])
except (OSError,ValueError,KeyError): print(False)
'''
        reuse = subprocess.check_output(['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
            '--cap-drop', 'ALL', '--cap-add', 'DAC_OVERRIDE', '--env', 'LD_PRELOAD=',
            '--mount', 'type=bind,src=' + str(holder) + ',dst=/private,readonly', '--entrypoint', 'python3',
            'psim-platform/postgres:' + profile.image_version, '-c', reusable], text=True).strip() == 'True'
        report['reused_catalog'] = reuse
        if not reuse:
            gateway.prepare(bundle, profile, holder)
        gateway.prepare(bundle, profile, holder, additional_clients=clients)
        runtime = gateway.command(bundle, profile, holder)
        probe = runtime.copy()
        probe[probe.index('--entrypoint') + 1] = '/repo/build/debian13/release/services/connector-gateway/psim-gateway-probe'

        def script(program):
            return local.vm.run(runtime + ['-c', program], capture_output=True).stdout

        def read(name):
            return json.loads(script('from pathlib import Path; print(Path("/case/' + name + '").read_text())'))

        def marker(log, process, value, timeout=90):
            deadline = time.monotonic() + timeout
            while value not in log.read_text():
                require(process.poll() is None and time.monotonic() < deadline, 'load process did not reach ' + value + ': ' + str(log))
                time.sleep(0.1)

        def start(command, name):
            container = 'psim-load-' + uuid.uuid4().hex[:12] + '-' + name
            command = command[:2] + ['--name', container] + command[2:]
            containers.append(container)
            logfile = output / (name + '.log')
            with logfile.open('w') as stream:
                process = subprocess.Popen(command, stdout=stream, stderr=subprocess.STDOUT, text=True)
            processes.append(process)
            return process, logfile

        local.vm.run(runtime + ['/repo/tools/deploy/qualification_setup.py', 'token'])
        script('import json,pathlib; p=pathlib.Path("/case/config.json"); v=json.loads(p.read_text()); '
               'v["rolling_clients"]=' + str(clients) + '; v["load_clickhouse_endpoint"]=' + repr('https://' + profile.select('infrastructure')[0].name + ':18447/') +
               '; p.chmod(0o600); p.write_text(json.dumps(v)); p.chmod(0o400)')
        local.vm.run(probe + ['enroll-load'])
        if not reuse:
            print(storage.scenario(runtime, 'setup'), flush=True)
        catalog_started = time.monotonic()
        print(storage.scenario(runtime, 'setup-load'), flush=True)
        report['catalog_setup_seconds'] = time.monotonic() - catalog_started
        report['catalog_setup_devices'] = 10000
        host_runtime = runtime.copy()
        host_runtime[host_runtime.index('--network') + 1] = 'host'
        local.vm.run(host_runtime + ['/repo/tools/load/browser_setup.py'])
        for node in profile.select('application'):
            local.vm.copy_to(local.VM_NAME, node.name, ROOT / 'tools/load/resources.py', '/home/psim/load-resources.py')
        # Every warmup output is drained and retained before starting new observers.
        for phase, seconds in [('warmup', 30), ('measurement', duration)]:
            stop_resources.clear()
            report['phase'] = phase
            script('import json,pathlib; p=pathlib.Path("/case"); c=p/"config.json"; v=json.loads(c.read_text()); '
                   'v.update(load_devices=True,rolling_epoch=int(v.get("rolling_epoch",0))+1,rolling_rate=' + str(rate) + ',rolling_seconds=' + str(seconds) + ',rolling_site=json.loads((p/"scenario.json").read_text())["site"]); '
                   'c.chmod(0o600); c.write_text(json.dumps(v)); c.chmod(0o400); '
                   '[(p/name).unlink(missing_ok=True) for name in ("rolling.stop","rolling-watch.stop","load-monitor.stop","load-browser.stop",'
                   '"rolling-progress.json","rolling-result.json","rolling-watch-result.json","load-history.json","load-browser-result.json","load-queries.jsonl")]')
            watcher, watch_log = start(probe + ['watch-rolling'], phase + '-watch')
            marker(watch_log, watcher, 'READY')
            monitor = None
            browser = None
            resources = []
            resource_errors = []
            if phase == 'measurement':
                browser_command = ['docker', 'run', '--rm', '--network', 'host', '--ipc=host',
                    '--user', str(profile.uid('deployment-admin')), '--env', 'HOME=/tmp',
                    '--volume', str(ROOT) + ':/repo:ro', '--volume', str(holder / 'material/qualification') + ':/case',
                    'mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27',
                    'node', '/repo/tools/load/browser.mjs']
                browser, browser_log = start(browser_command, 'browser')
                marker(browser_log, browser, 'READY', 180)
                monitor, monitor_log = start(host_runtime + ['/repo/tools/load/monitor.py'], 'queries')
                marker(monitor_log, monitor, 'READY')
                resource_started = time.monotonic()

                def sample_resources():
                    while not stop_resources.is_set():
                        row = {'seconds': time.monotonic() - resource_started, 'rss_bytes': {}, 'lag': 0}
                        try:
                            for node in profile.select('application'):
                                value = json.loads(local.execute(node.name, ['sudo', 'python3', '/home/psim/load-resources.py', node.address, *SERVICES], capture_output=True).stdout)
                                row['rss_bytes'].update({node.name + '/' + key: count for key, count in value['rss_bytes'].items()})
                                row['lag'] += value['lag']
                            resources.append(row)
                        except Exception as error:
                            resource_errors.append(str(error))
                        stop_resources.wait(10)
                resources_worker = threading.Thread(target=sample_resources)
                resources_worker.start()
            publisher, publisher_log = start(probe + ['rolling'], phase + '-publisher')
            marker(publisher_log, publisher, 'READY', 150)
            print('load: ' + profile_name + ' ' + phase + ', ' + str(seconds) + ' seconds', flush=True)
            deadline = time.monotonic() + seconds + 1800
            measured_end = None
            while publisher.poll() is None:
                require(time.monotonic() < deadline, 'publisher or reconciliation exceeded deadline')
                require(watcher.poll() is None and (browser is None or browser.poll() is None), 'load observer stopped')
                require(measured_end is not None or monitor is None or monitor.poll() is None, 'query observer stopped')
                if measured_end is None and 'MEASURED' in publisher_log.read_text():
                    measured_end = time.monotonic()
                    stop_resources.set()
                    script('from pathlib import Path; Path("/case/load-monitor.stop").touch()')
                time.sleep(1)
            require(publisher.returncode == 0, 'SDK publication or raw/normalized reconciliation failed: ' + str(publisher_log))
            script('from pathlib import Path; Path("/case/load-monitor.stop").touch()')
            if monitor is not None:
                require(monitor.wait(timeout=100) == 0, 'query observer failed')
            time.sleep(5)
            script('from pathlib import Path; Path("/case/load-browser.stop").touch()')
            if browser is not None:
                require(browser.wait(timeout=30) == 0, 'browser observer failed')
            script('from pathlib import Path; Path("/case/rolling-watch.stop").touch()')
            require(watcher.wait(timeout=30) == 0, 'signal observer failed')
            publisher_result, observed = read('rolling-result.json'), read('rolling-watch-result.json')
            local.vm.run(runtime + ['/repo/tools/load/history.py'])
            history = read('load-history.json')
            result = {'publisher': publisher_result, 'observation': observed, 'history': history}
            (output / (phase + '.json')).write_text(json.dumps(result, indent=2) + '\n')
            report[phase] = result
            if phase == 'measurement':
                queries = [json.loads(line) for line in script('from pathlib import Path; print(Path("/case/load-queries.jsonl").read_text())').splitlines() if line]
                queries = [row for row in queries if row['started_ms'] >= publisher_result['first_event_ms'] and row['finished_ms'] <= publisher_result['last_event_ms']]
                (output / 'queries.json').write_text(json.dumps(queries, indent=2) + '\n')
                # Reconciliation time is an upper bound on raw/normalized backlog recovery.
                recovery = time.monotonic() - measured_end if measured_end else seconds + 1800
                resources_worker.join(timeout=120)
                require(not resources_worker.is_alive(), 'resource sampler did not stop')
                report['resources'] = resources
                report['resource_errors'] = resource_errors
                browser_result = read('load-browser-result.json')
                (output / 'browser.json').write_text(json.dumps(browser_result, indent=2) + '\n')
                report.update(evaluate(profile_name, publisher_result, observed, queries, history['reconciled'], recovery, browser_result, resources, clients))
                if resource_errors:
                    report['passed'] = False
        require(report['passed'], 'load qualification has unmet criteria; see ' + str(path))
    except BaseException as error:
        report['error'] = str(error)
        raise
    finally:
        path.write_text(json.dumps(report, indent=2) + '\n')
        stop_resources.set()
        if resources_worker is not None:
            resources_worker.join(timeout=120)
        if runtime is not None:
            try:
                subprocess.run(runtime + ['-c', 'from pathlib import Path; [(Path("/case")/name).touch() for name in ("rolling.stop","rolling-watch.stop","load-monitor.stop","load-browser.stop")]'], capture_output=True, timeout=30)
                for name in ('rolling-progress.json', 'rolling-result.json', 'rolling-watch-result.json', 'load-history.json'):
                    value = subprocess.run(runtime + ['-c', 'from pathlib import Path; p=Path("/case/' + name + '"); print(p.read_text() if p.exists() else "null")'], capture_output=True, text=True, timeout=30)
                    if value.returncode == 0:
                        (output / ('last-' + name)).write_text(json.dumps(json.loads(value.stdout), indent=2) + '\n')
            except (OSError, ValueError, subprocess.TimeoutExpired) as error:
                report['cleanup_error'] = str(error)
        for container in containers:
            subprocess.run(['docker', 'rm', '-f', container], capture_output=True, timeout=30)
        for process in processes:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
        path.write_text(json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('profile', choices=PROFILES)
    parser.add_argument('--clients', type=int, default=100)
    args = parser.parse_args()
    with lab.exclusive():
        run(args.profile, args.clients)
