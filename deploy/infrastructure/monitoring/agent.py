"""Read-only node diagnostics, bounded protocol probes and operational state publication."""
import concurrent.futures
import http.server
import json
import math
import os
from pathlib import Path
import socket
import ssl
import threading
import time
import urllib.parse
import urllib.request


def sample(metric_name, value, **labels):
    number = float(value)
    if not math.isfinite(number):
        raise ValueError('non-finite metric')
    def quoted(value):
        return '"' + str(value).replace('\\', '\\\\').replace('\n', '\\n').replace('"', '\\"') + '"'
    suffix = '{' + ','.join(key + '=' + quoted(value) for key, value in sorted(labels.items())) + '}' if labels else ''
    return metric_name + suffix + ' ' + str(number) + '\n'


def tls_context(config, purpose='tls', server=False):
    options = config[purpose]
    context = ssl.create_default_context(ssl.Purpose.CLIENT_AUTH if server else ssl.Purpose.SERVER_AUTH, cafile=options['ca'])
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(options['cert'], options['key'])
    return context


def request(url, context=None, headers=None, data=None):
    # Bypass proxy environment variables for internal probes and bound response size.
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), urllib.request.HTTPSHandler(context=context))
    with opener.open(urllib.request.Request(url, headers=headers or {}, data=data), timeout=3) as response:
        body = response.read(4 * 1024 * 1024 + 1)
        if len(body) > 4 * 1024 * 1024:
            raise ValueError('probe response too large')
        return body


def resp(stream):
    line = stream.readline(65537)
    if not line.endswith(b'\r\n') or len(line) > 65536:
        raise ValueError('invalid RESP line')
    kind, body = line[:1], line[1:-2]
    if kind == b'-': raise ValueError('RESP error')
    if kind == b'+': return body.decode()
    if kind == b':': return int(body)
    if kind == b'$':
        size = int(body)
        if size == -1: return None
        if not 0 <= size <= 1024 * 1024: raise ValueError('invalid RESP length')
        value = stream.read(size + 2)
        if len(value) != size + 2 or not value.endswith(b'\r\n'): raise ValueError('truncated RESP value')
        return value[:-2].decode()
    if kind == b'*':
        size = int(body)
        if not 0 <= size <= 10000: raise ValueError('invalid RESP array')
        return [resp(stream) for _ in range(size)]
    raise ValueError('unsupported RESP type')


def redis_command(stream, *args):
    parts = [str(arg).encode() for arg in args]
    stream.write(b'*' + str(len(parts)).encode() + b'\r\n' + b''.join(b'$' + str(len(part)).encode() + b'\r\n' + part + b'\r\n' for part in parts))
    stream.flush()
    return resp(stream)


def probe(target, config):
    started = time.monotonic()
    labels = {'component': target['component'], 'target': target['host'] + ':' + str(target['port'])}
    lines = ''
    success = 0
    try:
        kind = target['kind']
        purpose = target.get('tls', 'tls')
        context = tls_context(config, purpose) if purpose else None
        if kind == 'http':
            body = request(('https' if context else 'http') + '://' + labels['target'] + target.get('path', '/'), context)
            if target.get('json_key'):
                if not json.loads(body).get(target['json_key']): raise ValueError('unhealthy response')
        elif kind == 'prometheus':
            from prometheus_client.parser import text_string_to_metric_families
            body = request(('https' if context else 'http') + '://' + labels['target'] + target.get('path', '/metrics'), context)
            found = set()
            for family in text_string_to_metric_families(body.decode()):
                for value in family.samples:
                    mapping = target['metrics'].get(value.name)
                    if mapping:
                        dimensions = {key: text for key, text in value.labels.items() if key in ('proxy', 'server', 'area', 'id', 'outcome', 'status', 'method', 'uri')}
                        lines += sample(mapping, value.value, **labels, **dimensions)
                        found.add(value.name)
            if not found: raise ValueError('expected metrics absent')
        elif kind == 'redis':
            with socket.create_connection((target['host'], target['port']), timeout=3) as raw:
                with context.wrap_socket(raw, server_hostname=target['host']) as connection:
                    stream = connection.makefile('rwb')
                    password = Path(config['secrets'] + '/' + target['component'] + '.secret').read_text().strip()
                    redis_command(stream, 'AUTH', 'monitoring', password)
                    if redis_command(stream, 'PING') != 'PONG': raise ValueError('invalid PING')
                    if target['component'] == 'valkey':
                        info = redis_command(stream, 'INFO', 'replication')
                        fields = dict(line.split(':', 1) for line in info.splitlines() if ':' in line)
                        lines += sample('psim_valkey_primary', fields['role'] == 'master', **labels)
                        lines += sample('psim_valkey_connected_replicas', fields.get('connected_slaves', 0), **labels)
                    else:
                        masters = redis_command(stream, 'SENTINEL', 'masters')
                        lines += sample('psim_sentinel_masters', len(masters), **labels)
        elif kind == 'keeper':
            with socket.create_connection((target['host'], target['port']), timeout=3) as raw:
                with context.wrap_socket(raw, server_hostname=target['host']) as connection:
                    connection.sendall(b'mntr')
                    body = b''
                    while True:
                        part = connection.recv(8192)
                        if not part: break
                        body += part
                        if len(body) > 65536: raise ValueError('keeper response too large')
                    fields = dict(line.split('\t', 1) for line in body.decode().splitlines() if '\t' in line)
                    lines += sample('psim_keeper_outstanding_requests', fields['zk_outstanding_requests'], **labels)
        elif kind == 'postgres':
            import psycopg2
            with psycopg2.connect(host=target['host'], port=target['port'], dbname='psim', user='monitoring',
                    password=Path(config['secrets'] + '/postgres.secret').read_text().strip(), connect_timeout=3,
                    sslmode='verify-full', sslrootcert=config['postgres_tls']['ca'], sslcert=config['postgres_tls']['cert'],
                    sslkey=config['postgres_tls']['key'], options='-c statement_timeout=3000 -c default_transaction_read_only=on') as connection:
                with connection.cursor() as cursor:
                    cursor.execute("SELECT NOT pg_is_in_recovery(),(SELECT count(*) FROM pg_stat_activity), CASE WHEN pg_is_in_recovery() AND pg_last_wal_receive_lsn() IS DISTINCT FROM pg_last_wal_replay_lsn() THEN COALESCE(EXTRACT(epoch FROM now()-pg_last_xact_replay_timestamp()),0) ELSE 0 END")
                    primary, connections, lag = cursor.fetchone()
                    lines += sample('psim_postgres_primary', primary, **labels)
                    lines += sample('psim_postgres_connections', connections, **labels)
                    lines += sample('psim_postgres_replay_lag_seconds', lag, **labels)
                    if primary and target.get('business'):
                        cursor.execute('SELECT metric,labels,value FROM monitoring.business_metrics()')
                        rows = cursor.fetchall()
                        for name, dimensions, value in rows:
                            lines += sample(name, value, **dimensions)
                        lines += sample('psim_collection_success', 1, collector='business', **labels)
        elif kind == 'clickhouse':
            password = Path(config['secrets'] + '/clickhouse.secret').read_text().strip()
            queries = {
                'parts': "SELECT table,partition,count() AS value FROM system.parts WHERE active GROUP BY table,partition FORMAT JSON",
                'replicas': "SELECT table,queue_size,is_readonly FROM system.replicas FORMAT JSON",
            }
            for name, query in queries.items():
                body = request('https://' + labels['target'] + '/', context,
                    {'X-ClickHouse-User': 'monitoring', 'X-ClickHouse-Key': password}, query.encode())
                rows = json.loads(body)['data']
                for value in rows:
                    dimensions = {**labels, 'table': value['table']}
                    if name == 'parts':
                        lines += sample('psim_clickhouse_active_parts', value['value'], partition=value['partition'], **dimensions)
                    else:
                        lines += sample('psim_clickhouse_replication_queue', value['queue_size'], **dimensions)
                        lines += sample('psim_clickhouse_replica_readonly', value['is_readonly'], **dimensions)
        elif kind == 'tcp':
            with socket.create_connection((target['host'], target['port']), timeout=3) as raw:
                if context:
                    with context.wrap_socket(raw, server_hostname=target['host']): pass
        else:
            raise ValueError('unknown probe kind')
        success = 1
    except Exception:
        # Error payloads and SQL exceptions can contain credentials; publish only bounded status.
        lines = ''
    lines += sample('psim_probe_success', success, **labels)
    lines += sample('psim_probe_duration_seconds', time.monotonic() - started, **labels)
    if target['kind'] in ('postgres', 'clickhouse', 'redis', 'keeper', 'prometheus'):
        lines += sample('psim_collection_success', success, collector=target['kind'], **labels)
    return lines


def kafka_metrics(config):
    from confluent_kafka import ConsumerGroupTopicPartitions, TopicPartition
    from confluent_kafka.admin import AdminClient, OffsetSpec
    deadline = time.monotonic() + 15

    def remaining():
        seconds = deadline - time.monotonic()
        if seconds <= 0: raise TimeoutError('Kafka diagnostics budget exceeded')
        return min(3, seconds)

    options = config['kafka']
    client = AdminClient({'bootstrap.servers': options['brokers'], 'security.protocol': 'SSL',
        'ssl.ca.location': config['tls']['ca'], 'ssl.certificate.location': config['tls']['cert'],
        'ssl.key.location': config['tls']['key'], 'socket.timeout.ms': 3000})
    metadata = client.list_topics(timeout=remaining())
    topics = {name: topic for name, topic in metadata.topics.items() if name.startswith('psim.')}
    if any(topic.error for topic in topics.values()): raise ValueError('topic metadata incomplete')
    partitions = [part for topic in topics.values() for part in topic.partitions.values()]
    lines = sample('psim_kafka_under_replicated_partitions', sum(len(p.isrs) < len(p.replicas) for p in partitions))
    lines += sample('psim_kafka_offline_partitions', sum(p.leader < 0 for p in partitions))
    requests = {TopicPartition(name, part): OffsetSpec.latest() for name, topic in topics.items() for part in topic.partitions}
    offsets = { (part.topic, part.partition): future.result(timeout=remaining()).offset
                for part, future in client.list_offsets(requests, request_timeout=remaining()).items()}
    for name in topics:
        if name.startswith('psim.dlq.'):
            lines += sample('psim_kafka_dlq_end_offset', sum(value for (topic, _), value in offsets.items() if topic == name), topic=name)
    listing = client.list_consumer_groups(request_timeout=remaining()).result(timeout=remaining())
    if listing.errors: raise ValueError('consumer group listing incomplete')
    for group in listing.valid:
        if not group.group_id.startswith(('psim-', 'console-', 'correlation-', 'gateway-', 'realtime', 'catalog-', 'incident-facts-', 'rule-replay-')): continue
        result = client.list_consumer_group_offsets([ConsumerGroupTopicPartitions(group.group_id)], request_timeout=remaining())[group.group_id].result(timeout=remaining())
        for partition in result.topic_partitions:
            if partition.error: raise ValueError('consumer offset unavailable')
            key = (partition.topic, partition.partition)
            if partition.offset >= 0 and key in offsets:
                lines += sample('psim_kafka_group_lag', max(0, offsets[key] - partition.offset), group=group.group_id,
                                topic=partition.topic, partition=partition.partition)
    return lines


def backup_metrics(path, expected):
    valid = 0
    updated = 0
    entries = {}
    try:
        document = json.loads(path.read_text())
        updated = float(document['updated_at_seconds'])
        if not 0 < updated <= time.time() + 60: raise ValueError('invalid status timestamp')
        entries = document['backups']
        for name in expected:
            value = entries[name]
            stamp = float(value['last_success_seconds']); size = float(value['size_bytes'])
            if not math.isfinite(stamp) or not math.isfinite(size) or not 0 <= stamp <= updated or size < 0:
                raise ValueError('invalid backup record')
        valid = 1
    except (OSError, ValueError, TypeError, KeyError):
        entries = {}; updated = 0
    lines = sample('psim_backup_status_valid', valid) + sample('psim_backup_status_updated_seconds', updated)
    for name in expected:
        value = entries.get(name, {})
        lines += sample('psim_backup_last_success_seconds', value.get('last_success_seconds', 0), backup=name)
        lines += sample('psim_backup_size_bytes', value.get('size_bytes', 0), backup=name)
    return lines


class Agent:
    def __init__(self, config):
        self.config = config
        self.lock = threading.Lock()
        self.snapshot = ''
        self.updated = 0
        self.cpu = None

    def collect(self):
        config = self.config
        lines = sample('psim_monitoring_info', 1)
        for name, path in config.get('volumes', {}).items():
            try:
                value = os.statvfs(path)
                for field, number in [('size_bytes', value.f_blocks * value.f_frsize), ('available_bytes', value.f_bavail * value.f_frsize),
                        ('files', value.f_files), ('files_free', value.f_favail)]:
                    lines += sample('psim_filesystem_' + field, number, volume=name)
                lines += sample('psim_collection_success', 1, collector='filesystem', component=name)
            except OSError:
                lines += sample('psim_collection_success', 0, collector='filesystem', component=name)
        proc = Path(config.get('proc', '/host/proc'))
        try:
            memory = {line.split(':')[0]: int(line.split()[1]) * 1024 for line in (proc / 'meminfo').read_text().splitlines()}
            lines += sample('psim_host_memory_total_bytes', memory['MemTotal'])
            lines += sample('psim_host_memory_available_bytes', memory['MemAvailable'])
            cpu = list(map(int, (proc / 'stat').read_text().splitlines()[0].split()[1:]))
            current = (sum(cpu[:8]), cpu[3] + cpu[4])
            if self.cpu and current[0] > self.cpu[0]:
                lines += sample('psim_host_cpu_utilization', 1 - (current[1] - self.cpu[1]) / (current[0] - self.cpu[0]))
            self.cpu = current
            lines += sample('psim_collection_success', 1, collector='host', component='host')
        except (OSError, ValueError, KeyError):
            lines += sample('psim_collection_success', 0, collector='host', component='host')
        with concurrent.futures.ThreadPoolExecutor(max_workers=8) as executor:
            for value in executor.map(lambda target: probe(target, config), config.get('targets', [])):
                lines += value
        if config.get('kafka'):
            try:
                lines += kafka_metrics(config)
                lines += sample('psim_collection_success', 1, collector='kafka', component='kafka')
            except Exception:
                lines += sample('psim_collection_success', 0, collector='kafka', component='kafka')
        for entry in config.get('certificates', []):
            try:
                from cryptography import x509
                certificates = x509.load_pem_x509_certificates(Path(entry['path']).read_bytes())
                expiry = min(certificate.not_valid_after_utc.timestamp() for certificate in certificates)
                lines += sample('psim_certificate_expiry_seconds', expiry,
                                owner=entry['owner'], certificate=entry['name'])
                lines += sample('psim_collection_success', 1, collector='certificate', component=entry['owner'], certificate=entry['name'])
            except (OSError, ValueError):
                lines += sample('psim_collection_success', 0, collector='certificate', component=entry['owner'], certificate=entry['name'])
        if config.get('backups'):
            lines += backup_metrics(Path(config['state']) / 'backups.json', config['backups'])
        lines += sample('psim_collection_timestamp_seconds', time.time())
        with self.lock:
            self.snapshot = lines
            self.updated = time.monotonic()

    def run(self):
        while True:
            try: self.collect()
            except Exception: pass
            time.sleep(15)


def handler(agent):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, *args): pass

        def permitted(self, owners):
            certificate = self.connection.getpeercert()
            names = [value for group in certificate.get('subject', ()) for key, value in group if key == 'commonName']
            if len(names) == 1 and names[0] in owners: return True
            self.send_error(403); return False

        def do_GET(self):
            path = urllib.parse.urlsplit(self.path).path
            allowed = {'prometheus', 'deployment-admin'}
            if path.startswith('/operations/runbooks/'): allowed.add('haproxy')
            if not self.permitted(allowed): return
            if path == '/metrics':
                if time.monotonic() - agent.updated > 90: self.send_error(503); return
                with agent.lock: body = agent.snapshot
                body = (body + sample('psim_host_time_seconds', time.time())).encode(); content = 'text/plain; version=0.0.4; charset=utf-8'
            elif path.startswith('/service-metrics/'):
                name = path.removeprefix('/service-metrics/')
                port = agent.config.get('service_metrics', {}).get(name)
                if port is None: self.send_error(404); return
                try:
                    body = request('http://' + agent.config['node_address'] + ':' + str(port) + '/metrics')
                except Exception: self.send_error(502); return
                content = 'text/plain; version=0.0.4; charset=utf-8'
            elif path == '/health/ready':
                with agent.lock: ready = bool(agent.snapshot) and time.monotonic() - agent.updated <= 90
                if not ready: self.send_error(503); return
                body = b'ready\n'; content = 'text/plain'
            elif path.startswith('/operations/runbooks/'):
                name = path.removeprefix('/operations/runbooks/')
                directory = Path(agent.config['runbooks'])
                if not name:
                    body = (directory / 'index.html').read_bytes()
                elif name.isascii() and name.isalnum() and (directory / (name + '.html')).is_file():
                    body = (directory / (name + '.html')).read_bytes()
                else: self.send_error(404); return
                content = 'text/html; charset=utf-8'
            else: self.send_error(404); return
            self.send_response(200); self.send_header('Content-Type', content); self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store'); self.send_header('X-Content-Type-Options', 'nosniff')
            self.end_headers(); self.wfile.write(body)

        def do_POST(self):
            if not self.permitted({'alertmanager'}): return
            if self.path != '/alerts': self.send_error(404); return
            try:
                length = int(self.headers.get('Content-Length', 0))
                if not 0 < length <= 1024 * 1024: raise ValueError('invalid notification size')
                document = json.loads(self.rfile.read(length))
                if not isinstance(document.get('alerts'), list): raise ValueError('invalid notification')
                path = Path(agent.config['state']) / 'alerts.jsonl'
                with agent.lock:
                    if path.exists() and path.stat().st_size > 16 * 1024 * 1024:
                        path.replace(path.with_suffix('.previous.jsonl'))
                    with path.open('a') as output:
                        output.write(json.dumps(document, ensure_ascii=True) + '\n'); output.flush(); os.fsync(output.fileno())
            except (ValueError, OSError): self.send_error(400); return
            self.send_response(200); self.end_headers()
    return Handler


def main():
    import argparse
    parser = argparse.ArgumentParser(); parser.add_argument('--config', required=True); args = parser.parse_args()
    agent = Agent(json.loads(Path(args.config).read_text()))
    threading.Thread(target=agent.run, daemon=True).start()
    context = tls_context(agent.config, server=True)
    context.verify_mode = ssl.CERT_REQUIRED

    class Server(http.server.ThreadingHTTPServer):
        daemon_threads = True

        def get_request(self):
            connection, address = self.socket.accept()
            connection.settimeout(5)
            try:
                return context.wrap_socket(connection, server_side=True), address
            except Exception:
                connection.close(); raise

    server = Server((agent.config['listen'], agent.config.get('port', 19443)), handler(agent))
    server.serve_forever()


if __name__ == '__main__': main()
