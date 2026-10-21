"""Measure committed signal throughput and latency on three live instances."""
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/correlation'))
from check import Check, TASK, uid  # noqa: E402


def run(project, count, rate):
    if count < 100000 or rate < 50000:
        raise ValueError('benchmark requires at least 100000 events and an offered rate of 50000')
    check = Check(project, 'release')
    reader = None
    try:
        check.compose('up', '-d', '--wait', '--wait-timeout', '180', 'kafka-1', 'registry', 'otel-collector')
        check.run([sys.executable, '-I', 'tools/env/envctl.py', 'topics', project, 'single'])
        check.run([TASK, 'contracts:registry:register', 'PROJECT=' + project])
        check.config({'single': {}, 'correlationScope': 'CORRELATION_SCOPE_ZONE',
                      'filter': {'types': ['intrusion.*'], 'condition': 'event.severity >= 3'},
                      'output': {'incidentTypeId': check.rule_id, 'priority': 'PRIORITY_P2', 'summaryTemplate': '${event.zone_id}'}})
        check.compose('up', '-d', '--force-recreate', *check.apps)
        check.probe.start_probe()
        check.ready(check.apps)
        def stable():
            for group, members in [('psim-correlation-router', 3), ('psim-correlation-engine', 12)]:
                state = check.compose('exec', '-T', 'kafka-1', '/opt/kafka/bin/kafka-consumer-groups.sh',
                                      '--bootstrap-server', 'kafka-1:19092', '--describe', '--group', group, '--state')
                if not any('Stable' in row.split() and row.split()[-1] == str(members) for row in state.splitlines()):
                    return False
            return True
        check.wait(stable, 60)
        binary = 'build/debian13/release/tools/correlation/psim-correlation-probe'
        seed = uid()
        def measure(path, spec):
            nonlocal reader
            ready = Path(str(path) + '.ready')
            ready.unlink(missing_ok=True)
            path.write_text(json.dumps(spec))
            relative = str(path.relative_to(ROOT))
            reader = subprocess.Popen(check.docker(binary, 'bench-read', relative), cwd=ROOT, env=check.env, stdout=check.log, stderr=check.log)
            check.wait(lambda: ready.exists(), 30)
            check.run(check.docker(binary, 'bench-emit', relative))
            assert reader.wait(timeout=130) == 0, 'committed signal reconciliation failed'
            return json.loads(Path(str(path) + '.result.json').read_text())
        warmup = 512000
        measure(check.output / 'warmup.json', {'count': warmup, 'rate': rate, 'seed': uid(), 'zone_seed': seed,
                                              'site': check.site, 'tenant': check.tenant})
        check.wait(stable, 60)
        path = check.output / 'benchmark.json'
        report = measure(path, {'count': count, 'rate': rate, 'seed': seed, 'sequence_offset': warmup // 1024,
                                'site': check.site, 'tenant': check.tenant})
        relative = str(path.relative_to(ROOT))
        report['warmup_events'] = warmup
        for service in check.apps:
            status, body = check.probe.http('GET', 'http://' + service + ':9100/metrics')
            if status == 200:
                (check.output / (service + '.load.metrics')).write_text(body)
        check.compose('kill', '-s', 'SIGKILL', *check.apps)
        restore_started = time.monotonic()
        check.compose('up', '-d', *check.apps)
        check.ready(check.apps)
        report['cold_restore_seconds'] = time.monotonic() - restore_started
        report['state_keys'] = 1024
        check.run(check.docker(binary, 'bench-replay', relative))
        def drained():
            check.run(check.docker(binary, 'bench-drained', relative))
            return True
        check.wait(drained, 60)
        check.run(check.docker(binary, 'bench-verify', relative))
        report['duplicate_signals_after_restore'] = 0
        report['passed'] = report['events_per_second'] >= 50000 and report['p99_ms'] <= 100 and report['cold_restore_seconds'] <= 30
        report['instances'] = 3
        report['shards_per_instance'] = 4
        report['engine_workers_per_instance'] = 4
        report['blocking_threads_per_instance'] = 8
        report['offered_rate'] = rate
        (check.output / 'benchmark-report.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))
        assert report['passed'], 'live correlation performance target failed'
    finally:
        if reader is not None and reader.poll() is None:
            reader.terminate()
            reader.wait(timeout=10)
        for service in check.apps:
            try:
                status, body = check.probe.http('GET', 'http://' + service + ':9100/metrics')
                if status == 200:
                    (check.output / (service + '.metrics')).write_text(body)
            except (RuntimeError, OSError):
                pass
        check.compose('logs', '--no-color', *check.apps)
        check.probe.stop_probe()
        if check.owned:
            check.compose('--profile', 'correlation-test', '--profile', 'event-flow', 'down', '-v', '--remove-orphans', '--timeout', '10')
        check.log.close()


if __name__ == '__main__':
    run(sys.argv[1], int(sys.argv[2]), int(sys.argv[3]))
