"""Exercise provisioned Grafana, OTLP storage and alert delivery in an isolated Docker Compose project."""
import argparse
import base64
import datetime
import hashlib
import json
import os
from pathlib import Path
import shutil
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/deploy'))


def prepare(directory, prefix):
    from material import Material
    from model import Profile
    from render import render
    document = json.loads((ROOT / 'deploy/topologies/single.json').read_text())
    document.update(name='psim-observability-smoke', prefix=prefix, public_name='127.0.0.1', public_port=19444)
    document['nodes'][0].update(name='psim-obs', address='127.0.0.1')
    (directory / 'profile.json').write_text(json.dumps(document))
    render(Profile.load(directory / 'profile.json'), directory / 'bundle')
    Material(directory / 'bundle', directory / 'private').create_lab()
    runtime = directory / 'runtime'
    shutil.copytree(directory / 'bundle', runtime / 'config')
    shutil.copytree(directory / 'private/files', runtime / 'material', copy_function=shutil.copy2)
    inventory = json.loads((directory / 'bundle/material.json').read_text())['files']
    for relative, entry in inventory.items():
        path = runtime / 'material' / relative
        os.chown(path, entry['uid'], entry['uid']); os.chown(path.parent, entry['uid'], entry['uid'])
    compose = json.loads((directory / 'bundle/psim-obs/compose.json').read_text())
    selected = {'monitoring', 'otel', 'prometheus', 'alertmanager', 'grafana', 'loki', 'tempo', 'kafka', 'valkey', 'keeper', 'clickhouse', 'postgres'}
    compose['services'] = {name: value for name, value in compose['services'].items() if name in selected}
    postgres = compose['services']['postgres']
    postgres.update(image='psim-platform/operations-test:local', entrypoint=['/usr/local/bin/docker-entrypoint.sh'],
                    command=['postgres', '-c', 'config_file=/etc/psim-platform/postgres/secrets/postgresql.conf'])
    postgres['environment'].update(PGDATA='/var/lib/postgresql/data', POSTGRES_HOST_AUTH_METHOD='trust', POSTGRES_DB='psim')
    for name in ('passwd', 'group'):
        postgres['volumes'].append(prefix + '/material/psim-obs/postgres/' + name + ':/etc/' + name + ':ro')
    for service in compose['services'].values():
        for mount in service['volumes']:
            source = mount.split(':', 1)[0]
            if source.startswith(prefix + '/data/'):
                path = runtime / source.removeprefix(prefix + '/')
                path.mkdir(parents=True, exist_ok=True)
                if not mount.endswith(':ro'):
                    uid = int(service['user'].split(':')[0]); os.chown(path, uid, uid)
    config = runtime / 'material/psim-obs/observability/collector.json'
    collector = json.loads(config.read_text())
    collector['receivers']['otlp']['protocols']['http'] = {'endpoint': '127.0.0.1:14319'}
    config.write_text(json.dumps(collector)); config.chmod(0o400)
    record = {'ts': datetime.datetime.now(datetime.timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z'),
              'level': 'INFO', 'service': 'native-log-test', 'instance': 'native-fixture', 'msg': 'native-log-smoke',
              'trace_id': 'a' * 32, 'span_id': 'b' * 16}
    (runtime / 'data/psim-obs/service-logs/native.jsonl').write_text(json.dumps(record) + '\n')
    (directory / 'compose.json').write_text(json.dumps(compose))


def kafka_setup(directory):
    from confluent_kafka import Consumer, Producer
    from confluent_kafka.admin import AdminClient, AclBinding, AclOperation, AclPermissionType, NewTopic, ResourcePatternType, ResourceType
    base = directory / 'runtime/material/psim-obs/deployment-admin'
    options = {'bootstrap.servers': '127.0.0.1:9092', 'security.protocol': 'SSL', 'ssl.ca.location': str(base / 'ca.pem'),
               'ssl.certificate.location': str(base / 'identity.pem'), 'ssl.key.location': str(base / 'identity.key'), 'socket.timeout.ms': 3000}
    client = AdminClient(options)
    for attempt in range(40):
        try:
            client.list_topics(timeout=3); break
        except Exception:
            if attempt == 39: raise
            time.sleep(1)
    bindings = [AclBinding(kind, name, pattern, 'User:monitoring', '*', AclOperation.DESCRIBE, AclPermissionType.ALLOW)
        for kind, name, pattern in [(ResourceType.BROKER, 'kafka-cluster', ResourcePatternType.LITERAL),
            (ResourceType.TOPIC, 'psim.', ResourcePatternType.PREFIXED), (ResourceType.GROUP, '*', ResourcePatternType.LITERAL)]]
    for future in client.create_acls(bindings, request_timeout=10).values(): future.result(15)
    topic = 'psim.observability.v1'; dlq = 'psim.dlq.observability.v1'
    for future in client.create_topics([NewTopic(name, num_partitions=1, replication_factor=1) for name in (topic, dlq)]).values(): future.result(15)
    producer = Producer(options)
    for index in range(5): producer.produce(topic, value=str(index).encode())
    producer.produce(dlq, value=b'invalid-fixture')
    assert producer.flush(15) == 0
    consumer = Consumer({**options, 'group.id': 'psim-observability-test', 'auto.offset.reset': 'earliest', 'enable.auto.commit': False})
    try:
        consumer.subscribe([topic]); messages = []; deadline = time.monotonic() + 30
        while len(messages) < 3 and time.monotonic() < deadline:
            message = consumer.poll(1)
            if message is not None and not message.error(): messages.append(message)
        assert len(messages) == 3
        consumer.commit(message=messages[-1], asynchronous=False)
    finally: consumer.close()


def verify(directory):
    base = directory / 'runtime/material/psim-obs'
    ca = base / 'monitoring/ca.pem'
    context = ssl.create_default_context(cafile=str(ca))
    context.load_cert_chain(base / 'prometheus/identity.pem', base / 'prometheus/identity.key')
    password = (base / 'grafana/admin.secret').read_text().strip()
    auth = 'Basic ' + base64.b64encode(('recovery-admin:' + password).encode()).decode()

    def http(url, body=None, headers=None):
        with urllib.request.urlopen(urllib.request.Request(url, data=None if body is None else json.dumps(body).encode(),
                headers={'Content-Type': 'application/json', **(headers or {})}), context=context, timeout=5) as response:
            return response.read()

    def until(operation, timeout=120):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            try:
                result = operation()
                if result: return result
            except Exception: pass
            time.sleep(2)
        raise AssertionError('observability operation did not become ready')

    until(lambda: http('https://127.0.0.1:19443/health/ready'))
    until(lambda: http('https://127.0.0.1:19444/monitoring/api/health', headers={'Authorization': auth}))
    health = json.loads(http('https://127.0.0.1:19444/monitoring/api/datasources/uid/prometheus/health', headers={'Authorization': auth}))
    assert health.get('status') == 'OK', health
    result = json.loads(http('https://127.0.0.1:19444/monitoring/api/ds/query', {
        'queries': [{'refId': 'A', 'datasource': {'type': 'prometheus', 'uid': 'prometheus'},
                     'expr': 'vector(1)', 'instant': True, 'range': False, 'intervalMs': 15000, 'maxDataPoints': 100}],
        'from': str(int(time.time() * 1000) - 60000), 'to': str(int(time.time() * 1000))}, {'Authorization': auth}))
    assert not result['results']['A'].get('error'), result
    dashboard_paths = list((ROOT / 'deploy/observability/dashboards').glob('*.json'))
    dashboard_paths += list((ROOT / 'deploy/observability/ru/dashboards').glob('*.json'))
    for path in dashboard_paths:
        uid = json.loads(path.read_text())['uid']
        result = json.loads(http('https://127.0.0.1:19444/monitoring/api/dashboards/uid/' + uid, headers={'Authorization': auth}))
        assert result['dashboard']['uid'] == uid
    def protocols_ready():
        metrics = http('https://127.0.0.1:19443/metrics').decode()
        (ROOT / 'build/observability/last-metrics.txt').write_text(metrics)
        expected = ['psim_postgres_primary', 'psim_valkey_primary', 'psim_keeper_outstanding_requests',
                    'psim_kafka_group_lag{group="psim-observability-test",partition="0",topic="psim.observability.v1"} 2.0',
                    'psim_kafka_dlq_end_offset{topic="psim.dlq.observability.v1"} 1.0',
                    'collector="clickhouse",component="clickhouse",target="psim-obs:18447"} 1.0']
        return metrics if all(name in metrics for name in expected) else None
    metrics = until(protocols_ready)
    assert 'collector="clickhouse",component="clickhouse",target="psim-obs:18447"} 1.0' in metrics
    assert 'psim_backup_status_valid 0.0' in metrics
    assert 'psim_certificate_expiry_seconds' in metrics
    try:
        # A valid server certificate alone does not authorize an unauthenticated scrape.
        client = ssl.create_default_context(cafile=str(ca))
        urllib.request.urlopen('https://127.0.0.1:19443/metrics', context=client, timeout=3)
        raise AssertionError('metrics accepted missing client certificate')
    except (ssl.SSLError, urllib.error.URLError): pass
    denied = ssl.create_default_context(cafile=str(ca))
    denied.load_cert_chain(base / 'incident-service/identity.pem', base / 'incident-service/identity.key')
    try:
        urllib.request.urlopen('https://127.0.0.1:19443/metrics', context=denied, timeout=3)
        raise AssertionError('metrics accepted an application identity')
    except urllib.error.HTTPError as error:
        assert error.code == 403
    trace = 'a' * 32; span = 'b' * 16; now = time.time_ns()
    attributes = [{'key': 'service.name', 'value': {'stringValue': 'observability-test'}}]
    http('http://127.0.0.1:14319/v1/traces', {'resourceSpans': [{'resource': {'attributes': attributes}, 'scopeSpans': [{'spans': [
        {'traceId': trace, 'spanId': span, 'name': 'observability.smoke', 'startTimeUnixNano': str(now), 'endTimeUnixNano': str(now + 1000000)}]}]}]})
    http('http://127.0.0.1:14319/v1/logs', {'resourceLogs': [{'resource': {'attributes': attributes}, 'scopeLogs': [{'logRecords': [
        {'timeUnixNano': str(now), 'severityText': 'INFO', 'traceId': trace, 'spanId': span, 'body': {'stringValue': 'observability-smoke'}}]}]}]})
    until(lambda: http('http://127.0.0.1:13200/api/traces/' + trace))
    query = urllib.parse.quote('{service_name="observability-test"}')
    logs = until(lambda: (result if (result := json.loads(http('http://127.0.0.1:13100/loki/api/v1/query_range?query=' + query)))['data']['result'] else None))
    assert 'observability-smoke' in json.dumps(logs)
    for service, marker in [('native-log-test', 'native-log-smoke'), ('container-log-test', 'container-log-smoke')]:
        selector = urllib.parse.quote('{service_name="' + service + '"} | trace_id="' + trace + '"')
        found = until(lambda: (value if (value := json.loads(http('http://127.0.0.1:13100/loki/api/v1/query_range?query=' + selector)))['data']['result'] else None))
        assert marker in json.dumps(found)
    until(lambda: any(entry.get('labels', {}).get('alertname') == 'PsimServiceUnavailable' for entry in
        json.loads(http('http://127.0.0.1:19090/api/v1/alerts'))['data']['alerts']))
    journal = directory / 'runtime/data/psim-obs/monitoring/alerts.jsonl'
    until(lambda: journal.is_file() and 'PsimServiceUnavailable' in journal.read_text())
    events = [json.loads(line) for line in journal.read_text().splitlines()]
    assert any(a['labels']['alertname'] == 'PsimWatchdog' for entry in events for a in entry['alerts'])
    for entry in events:
        for alert in entry['alerts']:
            url = alert['annotations']['runbook_url']
            path = urllib.parse.urlsplit(url).path
            assert 'Diagnostics' in http('https://127.0.0.1:19443' + path).decode()
            assert 'Диагностика' in http('https://127.0.0.1:19443' + path + 'Ru').decode()
    (directory / 'result.json').write_text(json.dumps({'dashboards': len(dashboard_paths), 'mutual_tls': True,
        'infrastructure_protocols': ['kafka', 'postgres', 'valkey', 'keeper', 'clickhouse'],
        'kafka_lag': 2, 'dlq_end_offset': 1, 'logs': True, 'native_logs': True, 'container_logs': True, 'traces': True,
        'prometheus_to_alertmanager_to_journal': True, 'runbook_links': True}, indent=2) + '\n')
    print('Fourteen EN/RU dashboards, verified TLS, OTLP logs/traces and real alert delivery passed', flush=True)


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--prepare'); parser.add_argument('--prefix'); parser.add_argument('--verify'); parser.add_argument('--kafka'); args = parser.parse_args()
    if args.prepare: prepare(Path(args.prepare), args.prefix); return
    if args.kafka: kafka_setup(Path(args.kafka)); return
    if args.verify: verify(Path(args.verify)); return
    output = ROOT / 'build/observability'; output.mkdir(parents=True, exist_ok=True)
    (output / 'smoke-result.json').unlink(missing_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='smoke-', dir=output))
    relative = directory.relative_to(ROOT)
    image = 'psim-platform/toolchain:debian13-' + hashlib.sha256((ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    docker = ['docker', 'run', '--rm', '--user', '0:0', '--volume', str(ROOT) + ':/repo', '--workdir', '/repo']
    compose = ['docker', 'compose', '-f', str(directory / 'compose.json')]
    try:
        subprocess.run(docker + [image, 'python3', '-I', 'tools/observability/smoke.py', '--prepare', '/repo/' + str(relative),
            '--prefix', str(directory / 'runtime')], check=True)
        subprocess.run(compose + ['up', '-d'], check=True)
        for attempt in range(60):
            ready = subprocess.run(compose + ['exec', '-T', 'postgres', 'pg_isready', '--host=127.0.0.1', '--username=postgres', '--dbname=psim'], capture_output=True)
            if ready.returncode == 0: break
            time.sleep(1)
        subprocess.run(compose + ['exec', '-T', 'postgres', 'psql', '--host=/tmp', '--username=postgres', '--dbname=postgres',
            '--set', 'ON_ERROR_STOP=1', '--file=/etc/psim-platform/postgres/secrets/bootstrap.sql'], check=True, stdout=subprocess.DEVNULL)
        subprocess.run(docker + ['--network', 'host', '--add-host', 'psim-obs:127.0.0.1', '--entrypoint', 'python3',
            'psim-platform/operations-test:local', '-I', 'tools/observability/smoke.py', '--kafka', '/repo/' + str(relative)], check=True)
        line = json.dumps({'service': 'container-log-test', 'instance': 'container-fixture', 'level': 'INFO',
            'msg': 'container-log-smoke', 'trace_id': 'a' * 32, 'span_id': 'b' * 16})
        subprocess.run(['docker', 'run', '--rm', '--log-driver', 'fluentd', '--log-opt', 'fluentd-address=127.0.0.1:24224',
            '--log-opt', 'fluentd-async=true', '--log-opt', 'tag=container-log-test', image, 'python3', '-c',
            'import sys; print(sys.argv[1],flush=True)', line], check=True)
        subprocess.run(docker + ['--network', 'host', image, 'python3', '-I', 'tools/observability/smoke.py', '--verify', '/repo/' + str(relative)], check=True)
        browser = 'mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27'
        subprocess.run(docker + ['--network', 'host', browser, 'node', 'tools/observability/browser.mjs', '/repo/' + str(relative)], check=True)
        shutil.copy2(directory / 'result.json', output / 'smoke-result.json')
    finally:
        if (directory / 'compose.json').is_file():
            with (output / 'smoke-containers.log').open('w') as stream:
                subprocess.run(compose + ['logs', '--no-color', '--tail=60'], stdout=stream, stderr=subprocess.STDOUT)
            subprocess.run(compose + ['down', '--volumes', '--remove-orphans'], check=True)
        # Remove private test identities even after a failed run.
        subprocess.run(docker + [image, 'python3', '-c', 'import shutil,sys; shutil.rmtree(sys.argv[1])', '/repo/' + str(relative)], check=True)


if __name__ == '__main__': main()
