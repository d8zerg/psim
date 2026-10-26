"""Emit immutable public configuration and the required external material inventory."""
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

from model import Profile, SERVICES, require

ROOT = Path(__file__).resolve().parents[2]
CONFIG = '/etc/psim-platform'


class Bundle:
    def __init__(self, profile, directory):
        self.profile = profile
        self.directory = Path(directory)
        self.images = json.loads((ROOT / 'deploy/topologies/images.json').read_text())
        self.material = {}
        self.composes = {node.name: {'name': profile.name + '-' + node.name, 'services': {}} for node in profile.nodes}
        self.ports = {node.name: {} for node in profile.nodes}

    def write(self, relative, value, mode=0o644):
        path = self.directory / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        if not isinstance(value, str):
            value = json.dumps(value, indent=2, sort_keys=True) + '\n'
        path.write_text(value)
        path.chmod(mode)

    def port(self, node, number, owner):
        require(number not in self.ports[node.name], 'conflicting port for ' + owner)
        self.ports[node.name][number] = owner
        return number

    def credential(self, node, owner, name, kind='password', **attributes):
        relative = node.name + '/' + owner + '/' + name
        entry = {'node': node.name, 'owner': owner, 'uid': self.profile.uid(owner), 'mode': '0400', 'kind': kind, **attributes}
        require(relative not in self.material or self.material[relative] == entry, 'conflicting material declaration')
        self.material[relative] = entry
        return CONFIG + '/' + owner + '/secrets/' + name

    def tls(self, node, owner, dns=(), uri=None, purpose=None, authority='platform', common_name=None):
        names = sorted(set([node.name, node.address, *dns]))
        stem = purpose or ('server' if owner == 'connector-gateway' else 'identity')
        certificate = self.credential(node, owner, stem + '.pem', 'certificate', dns_names=names, uri=uri, common_name=common_name or owner, authority=authority)
        key = self.credential(node, owner, stem + '.key', 'private-key')
        ca = self.credential(node, owner, (purpose + '-ca.pem') if purpose else 'ca.pem', 'trust-bundle', authorities=[authority])
        return ca, certificate, key

    def service(self, node, name, image, command, *, volumes=(), environment=None, owner=None, read_only=True):
        owner = owner or name
        uid = self.profile.uid(owner)
        service = {'image': image, 'network_mode': 'host', 'user': f'{uid}:{uid}', 'read_only': read_only,
                   'cap_drop': ['ALL'], 'security_opt': ['no-new-privileges:true'], 'restart': 'unless-stopped',
                   'stop_grace_period': '40s', 'command': command,
                   'tmpfs': ['/tmp:rw,nosuid,size=256m,mode=1777'],
                   'logging': {'driver': 'local', 'options': {'max-size': '20m', 'max-file': '5'}},
                   'volumes': list(volumes), 'extra_hosts': self.hosts(node),
                   'labels': {'psim.deployment': self.profile.name, 'psim.node': node.name, 'psim.failure-domain': node.domain}}
        if environment:
            service['environment'] = environment
        if any(requirement['node'] == node.name and requirement['owner'] == owner for requirement in self.material.values()):
            service['volumes'].append(self.profile.prefix + '/material/' + node.name + '/' + owner + ':' + CONFIG + '/' + owner + '/secrets:ro')
        self.composes[node.name]['services'][name] = service
        return service

    def hosts(self, node):
        result = {item.name: item.address for item in self.profile.nodes}
        for role in ('postgres', 'registry', 's3', 'keycloak', 'clickhouse', *SERVICES):
            result[role] = str(self.profile.virtual_ip).split('/')[0] if self.profile.virtual_ip else node.address
        if self.profile.public_name != 'localhost':
            result[self.profile.public_name] = str(self.profile.virtual_ip).split('/')[0] if self.profile.virtual_ip else node.address
        return result

    def application(self):
        profile = self.profile
        infra = profile.select('infrastructure')
        for node in profile.select('application'):
            for index, (name, (binary, port, schema)) in enumerate(SERVICES.items()):
                if name not in ('normalizer', 'correlation-engine'):
                    self.port(node, port, name)
                admin = self.port(node, 19100 + index, name + '-admin')
                ca, certificate, key = self.tls(node, name, dns=[name, profile.public_name], uri='psim://service/' + name)
                _, pg_certificate, pg_key = self.tls(node, name, purpose='postgres', common_name=name.replace('-', '_'), uri='psim://service/' + name)
                secret = lambda suffix: self.credential(node, name, suffix)
                settings = {
                    'allow_plaintext': False,
                    'brokers': ','.join(item.name + ':9092' for item in infra),
                    'registry': 'https://registry:18443/apis/ccompat/v7',
                    'registry_ca_file': ca, 'registry_certificate_file': certificate, 'registry_private_key_file': key,
                    'kafka_ca_file': ca, 'kafka_certificate_file': certificate, 'kafka_private_key_file': key,
                    'pg_host': 'postgres', 'pg_port': '15432', 'pg_database': 'psim', 'pg_user': name.replace('-', '_'),
                    'pg_ca_file': ca, 'pg_certificate_file': pg_certificate, 'pg_private_key_file': pg_key,
                    'pg_password_file': secret('postgres.secret'),
                    'issuer': profile.issuer, 'jwks_url': 'https://keycloak:18444/identity/realms/psim/protocol/openid-connect/certs',
                    'introspection_url': 'https://keycloak:18444/identity/realms/psim/protocol/openid-connect/token/introspect',
                    'introspection_client': name, 'introspection_secret_file': secret('identity.secret'),
                }
                if name not in ('normalizer', 'correlation-engine', 'connector-gateway'):
                    settings.update({'listen_port': port, 'tls_ca_file': ca, 'tls_certificate_file': certificate,
                                     'tls_private_key_file': key, 'jwks_ca_file': ca})
                if name in ('normalizer', 'correlation-engine', 'rule-management', 'resource-catalog'):
                    settings['taxonomy_file' if name != 'resource-catalog' else 'taxonomy_path'] = '/usr/share/psim/correlation-taxonomy.yaml' if name in ('correlation-engine', 'rule-management') else '/usr/share/psim/taxonomy.yaml'
                if name == 'connector-gateway':
                    settings.update({'listen_port': port, 'jwks_ca_file': ca, 'tls_directory': CONFIG + '/' + name + '/secrets', 'security_audit_tenant': profile.security_tenant})
                if name == 'rule-management':
                    settings['role'] = 'management'
                    settings['replay_kafka_certificate_file'] = self.credential(node, name, 'replay.pem', 'certificate', dns_names=[node.name], uri='psim://service/rule-replay', common_name='rule-replay')
                    settings['replay_kafka_private_key_file'] = self.credential(node, name, 'replay.key', 'private-key')
                if name in ('connector-gateway', 'command-service', 'projection-service', 'api-gateway'):
                    settings.update({'redis_host': infra[0].name, 'redis_user': name, 'redis_password_file': secret('valkey.secret'),
                                     'redis_ca_file': ca, 'redis_certificate_file': certificate, 'redis_key_file': key})
                    if profile.topology == 'cluster':
                        settings.update({'redis_sentinels': [item.name + ':26379' for item in infra], 'redis_primary_name': profile.name,
                                         'redis_sentinel_user': name, 'redis_sentinel_password_file': secret('sentinel.secret')})
                if name in ('event-history', 'projection-service', 'rule-management'):
                    settings.update({'clickhouse_host': 'clickhouse', 'clickhouse_port': 19440,
                                     'clickhouse_user': name, 'clickhouse_password_file': secret('clickhouse.secret'),
                                     'clickhouse_ca_file': ca, 'clickhouse_certificate_file': certificate, 'clickhouse_private_key_file': key})
                    if name != 'rule-management':
                        settings['clickhouse_cluster'] = 'psim'
                        prefix = 'history' if name == 'event-history' else 'reports'
                        settings[prefix + '_read_user'] = name + '-reader'
                        settings[prefix + '_read_password_file'] = secret('clickhouse-read.secret')
                    if name == 'event-history':
                        settings['history_api'] = True
                if name == 'correlation-engine':
                    settings['state_directory'] = profile.prefix + '/data/' + node.name + '/correlation-state'
                if name == 'audit-service':
                    settings.update({'signing_key_file': self.credential(node, name, 'signing.pem', 'audit-signing-key'),
                                     'trusted_keys_file': self.credential(node, name, 'trusted-keys.json', 'audit-trust'),
                                     'checkpoint_directory': profile.prefix + '/data/' + node.name + '/audit-checkpoints',
                                     'identity_base': 'https://keycloak:18444/identity', 'identity_client': name + '-directory',
                                     'identity_secret_file': secret('directory.secret'), 'identity_security_tenant': profile.security_tenant})
                if name in ('incident-service', 'response-engine', 'notification-service'):
                    settings.update({'directory_base': 'https://keycloak:18444/identity', 'directory_client': name + '-directory',
                                     'directory_roles_client': 'psim', 'directory_secret_file': secret('directory.secret')})
                if name == 'resource-catalog':
                    settings.update({'s3_endpoint': 'https://s3:18446', 's3_bucket': 'psim-plans', 's3_ca_file': ca,
                                     's3_access_key_file': secret('s3.access'), 's3_secret_key_file': secret('s3.secret'),
                                     'management_endpoint': 'connector-gateway:27443', 'management_ca_file': ca,
                                     'management_certificate_file': certificate, 'management_key_file': key,
                                     'spool_directory': profile.prefix + '/data/' + node.name + '/catalog-spool', 'pg_max_result_bytes': 134217728})
                if name == 'api-gateway':
                    settings.update({'listen_plaintext': False, 'allowed_origins': [profile.origin],
                                     'upstream_ca_file': ca, 'upstream_certificate_file': certificate, 'upstream_key_file': key})
                    aliases = {'catalog': 'resource-catalog', 'rules': 'rule-management', 'incidents': 'incident-service', 'commands': 'command-service',
                               'response': 'response-engine', 'history': 'event-history', 'reports': 'projection-service', 'projection': 'projection-service',
                               'audit': 'audit-service', 'notifications': 'notification-service'}
                    for alias, target in aliases.items():
                        settings[alias + '_host'] = target
                        settings[alias + '_port'] = str(SERVICES[target][1] + 10000)
                config = {'service': {'instance': name + '-' + node.name}, 'admin': {'listen': node.address + ':' + str(admin)},
                          'telemetry': {'otlp_endpoint': node.name + ':4317', 'otlp_tls': True, 'otlp_ca_file': ca, 'sampling_ratio': 0.01},
                          'runtime': {'shards': profile.shards if name in ('normalizer', 'event-history', 'correlation-engine', 'connector-gateway') else 1},
                          'shutdown': {'drain_timeout_ms': 30000}, 'settings': settings}
                self.write(node.name + '/services/' + name + '.json', config)
                filename = CONFIG + '/services/' + name + '.json'
                image = 'psim-platform/psim-' + binary + ':' + profile.image_version
                mounts = [profile.prefix + '/config/' + node.name + '/services/' + name + '.json:' + filename + ':ro']
                for field in ('spool_directory', 'state_directory', 'checkpoint_directory'):
                    if field in settings:
                        mounts.append(settings[field] + ':' + settings[field])
                self.service(node, name, image, ['--config', filename], volumes=mounts)
                self.write(node.name + '/systemd/psim-' + name + '.service', self.unit(node, name, binary, filename))

    def unit(self, node, name, binary, filename):
        return f'''[Unit]
Description=Open-Source PSIM Platform {name}
Wants=network-online.target
After=network-online.target

[Service]
Type=simple
User=psim-{name}
Group=psim-{name}
ExecStart=/usr/bin/psim-{binary} --config {filename}
Restart=on-failure
RestartSec=2s
TimeoutStopSec=40s
KillSignal=SIGTERM
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=true
PrivateTmp=true
PrivateDevices=true
ProtectKernelTunables=true
ProtectKernelModules=true
ProtectControlGroups=true
RestrictSUIDSGID=true
LockPersonality=true
CapabilityBoundingSet=
UMask=0077
StandardOutput=append:{self.profile.prefix}/data/{node.name}/service-logs/{name}.jsonl
StandardError=journal
BindReadOnlyPaths={self.profile.prefix}/config/{node.name}/services:/etc/psim-platform/services
BindReadOnlyPaths={self.profile.prefix}/material/{node.name}/{name}:/etc/psim-platform/{name}/secrets
BindReadOnlyPaths={self.profile.prefix}/config/{node.name}/hosts:/etc/hosts
{('ReadWritePaths=' + self.profile.prefix + '/data/' + node.name + '/' + {'resource-catalog': 'catalog-spool', 'correlation-engine': 'correlation-state', 'audit-service': 'audit-checkpoints'}[name]) if name in ('resource-catalog', 'correlation-engine', 'audit-service') else ''}

[Install]
WantedBy=multi-user.target
'''

    def finish(self):
        self.write('profile.json', self.profile.document())
        operator = self.profile.select('infrastructure')[0]
        self.tls(operator, 'deployment-admin', uri='psim://service/deployment-admin')
        self.tls(operator, 'deployment-admin', purpose='admin', common_name='gateway-admin', uri='psim://service/gateway-admin')
        self.tls(operator, 'deployment-admin', purpose='postgres', common_name='postgres', uri='psim://service/deployment-admin')
        for purpose in ('etcd', 'keeper', 'seaweed'):
            self.tls(operator, 'deployment-admin', purpose=purpose, authority=purpose, uri='psim://service/deployment-admin')
        self.credential(operator, 'deployment-admin', 'postgres.secret', secret_id='postgres/admin.secret')
        for name in ('identity.secret', 's3.access', 's3.secret', 'valkey.secret', 'sentinel.secret', 'clickhouse.secret'):
            self.credential(operator, 'deployment-admin', name)
        from infrastructure import protected, text_properties
        from kafka import bootstrap
        protected(self, operator, 'deployment-admin', 'kafka.properties', text_properties({
            'security.protocol': 'SSL', 'ssl.keystore.type': 'PEM', 'ssl.truststore.type': 'PEM',
            'ssl.keystore.certificate.chain': '{{file:identity.pem}}', 'ssl.keystore.key': '{{file:identity.key}}',
            'ssl.truststore.certificates': '{{file:ca.pem}}', 'ssl.endpoint.identification.algorithm': 'HTTPS',
        }))
        protected(self, operator, 'deployment-admin', 'kafka-bootstrap.sh', bootstrap(self.profile))
        for name in ('registry.py', 'contracts.py'):
            self.write('support/' + name, (ROOT / 'tools/contracts' / name).read_text())
        self.write('support/setup.py', (ROOT / 'tools/deploy/node_setup.py').read_text())
        self.write('support/model.py', (ROOT / 'tools/deploy/model.py').read_text())
        for directory in ('contracts/proto', 'contracts/third_party/proto'):
            for path in sorted((ROOT / directory).rglob('*.proto')):
                self.write('support/tree/' + str(path.relative_to(ROOT)), path.read_text())
        self.write('support/tree/contracts/topics/topics.yaml', (ROOT / 'contracts/topics/topics.yaml').read_text())
        from observability import finalize
        finalize(self)
        for node in self.profile.nodes:
            self.write(node.name + '/compose.json', self.composes[node.name])
            self.write(node.name + '/logrotate.conf', f'''{self.profile.prefix}/data/{node.name}/service-logs/*.jsonl {{
  size 20M
  rotate 5
  copytruncate
  compress
  missingok
  notifempty
  su root root
}}
''')
            hosts = '\n'.join(address + ' ' + name for name, address in sorted(self.hosts(node).items()))
            self.write(node.name + '/hosts', '127.0.0.1 localhost\n' + hosts + '\n')
        self.write('material.json', {'format': 1, 'files': self.material})
        self.write('ports.json', self.ports)
        self.write('generation.json', {relative: hashlib.sha256((ROOT / relative).read_bytes()).hexdigest()
                   for relative in ('tools/deploy/material.py', 'tools/deploy/policies.py',
                                    'deploy/observability/business.sql', 'deploy/local/config/keycloak/psim-realm.json')})
        files = {}
        for path in sorted(self.directory.rglob('*')):
            if path.is_file():
                files[str(path.relative_to(self.directory))] = hashlib.sha256(path.read_bytes()).hexdigest()
        self.write('manifest.json', {'format': 1, 'name': self.profile.name, 'topology': self.profile.topology, 'files': files})


def render(profile, output):
    destination = Path(output)
    require(not destination.exists(), 'output exists; render a new bundle before replacing an installation')
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = Path(tempfile.mkdtemp(prefix='.psim-render-', dir=destination.parent))
    try:
        bundle = Bundle(profile, temporary)
        bundle.application()
        from infrastructure import render_infrastructure
        render_infrastructure(bundle)
        bundle.finish()
        temporary.rename(destination)
    finally:
        if temporary.exists():
            shutil.rmtree(temporary)
