"""Wire the static observability package to the deployment inventory and private identities."""
import json
from pathlib import Path

from infrastructure import data, protected
from model import SERVICES
from render import CONFIG, ROOT

PACKAGE = ROOT / 'deploy/observability'


def identity(bundle, node, owner, **kwargs):
    ca, cert, key = bundle.tls(node, owner, **kwargs)
    return {'ca': ca, 'cert': cert, 'key': key}


def mount_package(bundle, node):
    for path in sorted(PACKAGE.rglob('*')):
        if path.is_file(): bundle.write(node.name + '/observability/' + str(path.relative_to(PACKAGE)), path.read_text())
    return bundle.profile.prefix + '/config/' + node.name + '/observability:' + CONFIG + '/observability:ro'


def render(bundle):
    profile = bundle.profile
    central = profile.select('observability')[0]
    for node in profile.nodes:
        package = mount_package(bundle, node)
        agent(bundle, node, central, package)
        collector(bundle, node, central)
    stack(bundle, central)


def agent(bundle, node, central, package):
    profile = bundle.profile
    tls = identity(bundle, node, 'monitoring', dns=['127.0.0.1'])
    config = {'listen': '0.0.0.0', 'node_address': node.address, 'port': 19443, 'tls': tls, 'secrets': CONFIG + '/monitoring/secrets',
        'state': '/var/lib/monitoring', 'runbooks': CONFIG + '/observability/runbooks',
        'targets': [], 'service_metrics': {}, 'volumes': {}, 'certificates': []}
    targets = config['targets']
    # Every node probes infrastructure; a network partition remains visible from both sides.
    for target in profile.select('kafka'):
        targets.append({'component': 'kafka', 'kind': 'tcp', 'host': target.name, 'port': 9092})
    for target in profile.select('infrastructure'):
        targets.append({'component': 'postgres', 'kind': 'tcp', 'host': target.name, 'port': 5432, 'tls': None})
    if 'application' in node.roles:
        config['service_metrics'] = {name: 19100 + index for index, name in enumerate(SERVICES)}
    if 'infrastructure' in node.roles:
        config['postgres_tls'] = identity(bundle, node, 'monitoring', purpose='postgres', common_name='monitoring')
        for name in ('postgres', 'valkey'):
            bundle.credential(node, 'monitoring', name + '.secret', secret_id='monitoring/' + name + '.secret')
        # Full local protocol probes replace their weaker reachability counterparts.
        targets[:] = [target for target in targets if not (target['host'] == node.name and target['component'] == 'postgres')]
        targets.extend([{'component': 'postgres', 'kind': 'postgres', 'host': node.name, 'port': 5432, 'business': True},
                        {'component': 'valkey', 'kind': 'redis', 'host': node.name, 'port': 6379}])
        if profile.topology == 'cluster':
            config['etcd_tls'] = identity(bundle, node, 'monitoring', purpose='etcd', authority='etcd')
            targets.append({'component': 'etcd', 'kind': 'http', 'host': node.name, 'port': 2379, 'path': '/health', 'tls': 'etcd_tls', 'json_key': 'health'})
            targets.append({'component': 'etcd-metrics', 'kind': 'prometheus', 'host': node.name, 'port': 2379, 'tls': 'etcd_tls',
                'metrics': {'etcd_server_has_leader': 'psim_etcd_has_leader', 'etcd_mvcc_db_total_size_in_bytes': 'psim_etcd_db_size_bytes',
                    'etcd_server_proposals_pending': 'psim_etcd_proposals_pending'}})
            targets.append({'component': 'patroni', 'kind': 'http', 'host': node.name, 'port': 8008, 'path': '/patroni'})
            bundle.credential(node, 'monitoring', 'sentinel.secret', secret_id='monitoring/sentinel.secret')
            targets.append({'component': 'sentinel', 'kind': 'redis', 'host': node.name, 'port': 26379})
        config['keeper_tls'] = identity(bundle, node, 'monitoring', purpose='keeper', authority='keeper')
        targets.append({'component': 'keeper', 'kind': 'keeper', 'host': node.name, 'port': 9281, 'tls': 'keeper_tls'})
        config['seaweed_tls'] = identity(bundle, node, 'monitoring', purpose='seaweed', authority='seaweed')
        for name, port, path in [('seaweed-master', 9333, '/cluster/status'), ('seaweed-volume', 18080, '/status')]:
            targets.append({'component': name, 'kind': 'http', 'host': node.name, 'port': port, 'path': path, 'tls': 'seaweed_tls'})
        if node in profile.select('infrastructure')[:2]:
            bundle.credential(node, 'monitoring', 'clickhouse.secret', secret_id='monitoring/clickhouse.secret')
            targets.extend([
                {'component': 'clickhouse', 'kind': 'clickhouse', 'host': node.name, 'port': 18447},
                {'component': 'seaweed-filer', 'kind': 'http', 'host': node.name, 'port': 18888, 'path': '/healthz', 'tls': 'seaweed_tls'},
                {'component': 'seaweed-s3', 'kind': 'tcp', 'host': node.name, 'port': 18333},
            ])
    if node in profile.select('support'):
        targets.extend([
            {'component': 'keycloak', 'kind': 'http', 'host': node.name, 'port': 19000, 'path': '/identity/health/ready'},
            {'component': 'keycloak-metrics', 'kind': 'prometheus', 'host': node.name, 'port': 19000, 'path': '/identity/metrics',
             'metrics': {'http_server_requests_seconds_count': 'psim_keycloak_requests_total', 'jvm_memory_used_bytes': 'psim_keycloak_memory_used_bytes'}},
        ])
    if 'edge' in node.roles:
        targets.append({'component': 'haproxy', 'kind': 'prometheus', 'host': node.name, 'port': 19301, 'path': '/metrics', 'tls': None,
            'metrics': {'haproxy_server_status': 'psim_haproxy_server_status', 'haproxy_server_current_sessions': 'psim_haproxy_sessions',
                'haproxy_backend_connection_errors_total': 'psim_haproxy_connection_errors_total'}})
    if node == central:
        config['backups'] = ['postgres', 'clickhouse', 'kafka-config', 'product-config', 'plans', 'identity-pki']
        config['kafka'] = {'brokers': ','.join(n.name + ':9092' for n in profile.select('kafka'))}
        for component, port, path in [('prometheus', 19090, '/-/ready'), ('alertmanager', 19093, '/-/ready'),
                                      ('loki', 13100, '/ready'), ('tempo', 13200, '/ready')]:
            targets.append({'component': component, 'kind': 'http', 'host': '127.0.0.1', 'port': port, 'path': path, 'tls': None})
    mounts = [package, data(bundle, node, 'monitoring') + ':/var/lib/monitoring', '/proc:/host/proc:ro']
    filename = protected(bundle, node, 'monitoring', 'agent.json', json.dumps(config))
    bundle.port(node, 19443, 'monitoring')
    service = bundle.service(node, 'monitoring', 'psim-platform/monitoring:' + profile.image_version,
                             ['--config', filename], volumes=mounts)
    return service


def collector(bundle, node, central):
    tls = identity(bundle, node, 'observability', dns=['127.0.0.1'])
    bundle.port(node, 4317, 'otel')
    bundle.port(node, 24224, 'otel-logs')
    bundle.port(node, 18889, 'otel-metrics')
    bundle.port(node, 13133, 'otel-health')
    base = '/var/lib/otel'
    config = json.loads((PACKAGE / 'collector.json').read_text())
    config['receivers']['otlp']['protocols']['grpc']['endpoint'] = node.address + ':4317'
    config['processors']['resource']['attributes'] = [
        {'key': 'deployment.environment.name', 'value': bundle.profile.name, 'action': 'insert'},
        {'key': 'host.name', 'value': node.name, 'action': 'insert'}]
    queue = {'enabled': True, 'storage': 'file_storage', 'queue_size': 1000}
    retry = {'enabled': True, 'max_elapsed_time': '0s'}
    if node == central:
        config['receivers']['otlp/remote'] = {'protocols': {'grpc': {'endpoint': node.address + ':14317', 'tls': {
            'cert_file': tls['cert'], 'key_file': tls['key'], 'client_ca_file': tls['ca']}}}}
        bundle.port(node, 14317, 'otel-remote')
        exporters = {'traces': ['otlp/tempo'], 'logs': ['otlphttp/loki'], 'metrics': ['prometheusremotewrite']}
    else:
        del config['receivers']['otlp/remote']
        config['exporters'] = {'otlp/central': {'endpoint': central.name + ':14317', 'tls': {
            'ca_file': tls['ca'], 'cert_file': tls['cert'], 'key_file': tls['key']}, 'sending_queue': queue, 'retry_on_failure': retry}}
        exporters = {kind: ['otlp/central'] for kind in ('traces', 'logs', 'metrics')}
    for kind in ('traces', 'logs', 'metrics'):
        receivers = ['otlp'] + (['otlp/remote'] if node == central else [])
        if kind == 'logs': receivers += ['fluentforward', 'filelog']
        processors = config['service']['pipelines'][kind]['processors']
        config['service']['pipelines'][kind] = {'receivers': receivers, 'processors': processors, 'exporters': exporters[kind]}
    filename = protected(bundle, node, 'observability', 'collector.json', json.dumps(config))
    bundle.service(node, 'otel', bundle.images['otel'], ['--config=' + filename], owner='observability', volumes=[
        data(bundle, node, 'otel') + ':' + base, data(bundle, node, 'service-logs') + ':/var/log/psim:ro'])


def stack(bundle, node):
    profile = bundle.profile
    package = profile.prefix + '/config/' + node.name + '/observability:' + CONFIG + '/observability:ro'
    for name, ports in [('loki', (13100, 19095)), ('tempo', (13200, 19096, 14318)), ('alertmanager', (19093,))]:
        for port in ports: bundle.port(node, port, name)
    for name in ('loki', 'tempo', 'alertmanager', 'prometheus', 'grafana'):
        identity(bundle, node, name, dns=['127.0.0.1'])
    rules = json.loads((PACKAGE / 'rules.json').read_text())
    for group in rules['groups']:
        for rule in group['rules']:
            if 'alert' in rule:
                rule.setdefault('labels', {})['deployment'] = profile.name
                for field in ('runbook_url', 'dashboard_url'):
                    rule['annotations'][field] = profile.origin + rule['annotations'][field]
    # These configuration fragments carry only inventory labels and public URLs.
    bundle.write(node.name + '/observability/runtime-rules.json', rules)
    jobs = []
    for target in profile.nodes:
        tls = {'ca_file': CONFIG + '/prometheus/secrets/ca.pem', 'cert_file': CONFIG + '/prometheus/secrets/identity.pem',
               'key_file': CONFIG + '/prometheus/secrets/identity.key'}
        labels = {'deployment': profile.name, 'node': target.name}
        jobs.append({'job_name': 'monitoring-' + target.name, 'scheme': 'https', 'tls_config': tls,
            'static_configs': [{'targets': [target.name + ':19443'], 'labels': {**labels, 'job': 'psim-monitoring'}}]})
        if 'application' in target.roles:
            for name in SERVICES:
                jobs.append({'job_name': name + '-' + target.name, 'metrics_path': '/service-metrics/' + name,
                    'scheme': 'https', 'tls_config': tls, 'static_configs': [{'targets': [target.name + ':19443'],
                        'labels': {**labels, 'service': name, 'job': 'psim-services'}}]})
    for name, port in [('prometheus', 19090), ('alertmanager', 19093), ('otel-collector', 18889)]:
        jobs.append({'job_name': name, 'static_configs': [{'targets': ['127.0.0.1:' + str(port)],
            'labels': {'deployment': profile.name, 'node': node.name}}]})
    prometheus = {'global': {'scrape_interval': '15s', 'evaluation_interval': '15s', 'external_labels': {'deployment': profile.name}},
        'rule_files': [CONFIG + '/observability/runtime-rules.json'], 'scrape_configs': jobs,
        'alerting': {'alertmanagers': [{'static_configs': [{'targets': ['127.0.0.1:19093']}]}]}}
    filename = protected(bundle, node, 'prometheus', 'prometheus.json', json.dumps(prometheus))
    bundle.port(node, 19090, 'prometheus')
    bundle.service(node, 'prometheus', bundle.images['prometheus'], ['--config.file=' + filename,
        '--web.listen-address=127.0.0.1:19090', '--web.enable-remote-write-receiver', '--storage.tsdb.path=/prometheus',
        '--storage.tsdb.retention.time=15d', '--storage.tsdb.retention.size=10GB'], volumes=[package, data(bundle, node, 'prometheus') + ':/prometheus'])
    for name, command, destination in [
        ('loki', ['-config.file=' + CONFIG + '/observability/loki.yaml'], '/var/lib/loki'),
        ('tempo', ['-config.file=' + CONFIG + '/observability/tempo.yaml'], '/var/tempo'),
        ('alertmanager', ['--config.file=' + CONFIG + '/observability/alertmanager.yaml', '--web.listen-address=127.0.0.1:19093',
            '--cluster.listen-address=', '--storage.path=/alertmanager', '--data.retention=120h'], '/alertmanager'),
    ]:
        bundle.service(node, name, bundle.images[name], command, volumes=[package, data(bundle, node, name) + ':' + destination])
    grafana(bundle, node, package)


def grafana(bundle, node, package):
    profile = bundle.profile
    bundle.credential(node, 'grafana', 'admin.secret')
    bundle.credential(node, 'grafana', 'identity.secret')
    bundle.credential(node, 'grafana', 'session.secret')
    filename = protected(bundle, node, 'grafana', 'grafana.ini', (PACKAGE / 'grafana.ini').read_text())
    bundle.port(node, 19444, 'grafana')
    source = profile.prefix + '/config/' + node.name + '/observability'
    bundle.service(node, 'grafana', bundle.images['grafana'], ['--config=' + filename], volumes=[package,
        data(bundle, node, 'grafana') + ':/var/lib/grafana', source + '/datasources:/etc/grafana/provisioning/datasources:ro',
        source + '/providers:/etc/grafana/provisioning/dashboards:ro'], environment={
        'PSIM_NODE_ADDRESS': node.address, 'PSIM_PUBLIC_NAME': profile.public_name,
        'PSIM_ORIGIN': profile.origin, 'PSIM_ISSUER': profile.issuer,
        'SSL_CERT_FILE': CONFIG + '/grafana/secrets/ca.pem'})


def finalize(bundle):
    # Give agents only public certificates; private material stays with its service UID.
    for node in bundle.profile.nodes:
        relative = node.name + '/monitoring/agent.json'
        config = json.loads(bundle.material[relative]['template'])
        for service in bundle.composes[node.name]['services'].values():
            for volume in list(service['volumes']):
                path = volume.split(':', 1)[0]
                if path.startswith(bundle.profile.prefix + '/data/' + node.name + '/'):
                    name = Path(path).name
                    if name not in config['volumes']:
                        config['volumes'][name] = '/volumes/' + name
                        bundle.composes[node.name]['services']['monitoring']['volumes'].append(path + ':/volumes/' + name + ':ro')
        for source, entry in list(bundle.material.items()):
            if entry['node'] != node.name or entry['kind'] not in ('certificate', 'trust-bundle'): continue
            filename = ('trust-' if entry['kind'] == 'trust-bundle' else '') + entry['owner'] + '-' + Path(source).name
            path = bundle.credential(node, 'monitoring', filename, 'certificate-copy', source=source)
            config['certificates'].append({'path': path, 'owner': entry['owner'], 'name': Path(source).name})
        bundle.material[relative]['template'] = json.dumps(config)
        for name, service in bundle.composes[node.name]['services'].items():
            if name in SERVICES:
                service['logging'] = {'driver': 'fluentd', 'options': {'fluentd-address': '127.0.0.1:24224',
                    'fluentd-async': 'true', 'fluentd-buffer-limit': '16384', 'tag': name}}
