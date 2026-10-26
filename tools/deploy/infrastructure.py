"""Infrastructure definitions for the same inventory used by application configuration."""
import json
from xml.sax.saxutils import escape

from model import SERVICES
from render import CONFIG


def text_properties(values):
    return ''.join(str(key) + '=' + str(value) + '\n' for key, value in values.items())


def protected(bundle, node, owner, name, content):
    return bundle.credential(node, owner, name, 'template', template=content)


def data(bundle, node, name):
    return bundle.profile.prefix + '/data/' + node.name + '/' + name


def render_infrastructure(bundle):
    profile = bundle.profile
    nodes = profile.select('infrastructure')
    cluster = profile.topology == 'cluster'
    for index, node in enumerate(nodes, 1):
        kafka(bundle, node, index, nodes)
        postgres(bundle, node, nodes)
        valkey(bundle, node, index, nodes)
        keeper(bundle, node, index, nodes)
        seaweed(bundle, node, index, nodes)
        if index <= 2 or not cluster:
            clickhouse(bundle, node, index, nodes)
            identity(bundle, node)
            registry(bundle, node)
    for node in profile.select('edge'):
        proxy(bundle, node)
    for node in profile.select('application'):
        web(bundle, node)
    for node in profile.select('observability'):
        observability(bundle, node)


def kafka(bundle, node, index, nodes):
    for port in (9092, 9093):
        bundle.port(node, port, 'kafka')
    ca, certificate, key = bundle.tls(node, 'kafka', dns=[n.name for n in nodes])
    props = {
        'process.roles': 'broker,controller', 'node.id': index,
        'controller.quorum.voters': ','.join(str(i) + '@' + n.name + ':9093' for i, n in enumerate(nodes, 1)),
        'listeners': 'CLIENT://' + node.address + ':9092,CONTROLLER://' + node.address + ':9093',
        'advertised.listeners': 'CLIENT://' + node.name + ':9092',
        'controller.listener.names': 'CONTROLLER', 'inter.broker.listener.name': 'CLIENT',
        'listener.security.protocol.map': 'CLIENT:SSL,CONTROLLER:SSL',
        'ssl.client.auth': 'required', 'ssl.keystore.type': 'PEM', 'ssl.truststore.type': 'PEM',
        'ssl.keystore.certificate.chain': '{{file:identity.pem}}', 'ssl.keystore.key': '{{file:identity.key}}',
        'ssl.truststore.certificates': '{{file:ca.pem}}',
        'ssl.endpoint.identification.algorithm': 'HTTPS',
        'ssl.principal.mapping.rules': 'RULE:^CN=([^,]+).*$/$1/,DEFAULT',
        'authorizer.class.name': 'org.apache.kafka.metadata.authorizer.StandardAuthorizer',
        'allow.everyone.if.no.acl.found': 'false', 'super.users': 'User:kafka;User:deployment-admin',
        'auto.create.topics.enable': 'false', 'default.replication.factor': len(nodes),
        'min.insync.replicas': 2 if len(nodes) == 3 else 1,
        'offsets.topic.replication.factor': len(nodes), 'transaction.state.log.replication.factor': len(nodes),
        'transaction.state.log.min.isr': 2 if len(nodes) == 3 else 1,
        'log.dirs': '/var/lib/kafka/data', 'num.partitions': 12,
        'unclean.leader.election.enable': 'false', 'delete.topic.enable': 'false',
    }
    config = protected(bundle, node, 'kafka', 'server.properties', text_properties(props))
    # PEM blocks are materialized with continuation escaping by the material provisioner.
    script = f'''#!/bin/sh
set -eu
if [ ! -f /var/lib/kafka/data/meta.properties ]; then
 /opt/kafka/bin/kafka-storage.sh format -t '{{{{cluster-id}}}}' -c '{config}'
fi
exec /opt/kafka/bin/kafka-server-start.sh '{config}'
'''
    entry = protected(bundle, node, 'kafka', 'start.sh', script)
    service = bundle.service(node, 'kafka', bundle.images['kafka'], [], owner='kafka', volumes=[data(bundle, node, 'kafka') + ':/var/lib/kafka/data'], environment={'KAFKA_HEAP_OPTS': '-Xms256m -Xmx1g', 'LOG_DIR': '/tmp/kafka-logs'})
    service['entrypoint'] = ['/bin/sh', entry]
    # Zstandard JNI loads its bundled native library from the private temporary filesystem.
    service['tmpfs'] = ['/tmp:rw,exec,nosuid,nodev,size=256m,mode=1777']


def postgres(bundle, node, nodes):
    profile = bundle.profile
    cluster = profile.topology == 'cluster'
    bundle.port(node, 5432, 'postgres')
    ca, certificate, key = bundle.tls(node, 'postgres', dns=['postgres', *[n.name for n in nodes]])
    admin = bundle.credential(node, 'postgres', 'admin.secret')
    replication = bundle.credential(node, 'postgres', 'replication.secret')
    common = {'ssl': 'on', 'ssl_ca_file': ca, 'ssl_cert_file': certificate, 'ssl_key_file': key,
              'password_encryption': 'scram-sha-256', 'max_connections': 400,
              'wal_level': 'replica', 'max_wal_senders': 10, 'max_replication_slots': 10,
              'wal_log_hints': 'on', 'hot_standby': 'on', 'synchronous_commit': 'on', 'unix_socket_directories': '/tmp',
              'shared_buffers': '256MB', 'work_mem': '4MB'}
    hba = ['local all all peer', 'hostssl replication replicator 0.0.0.0/0 scram-sha-256 clientcert=verify-full',
           'hostssl all all 0.0.0.0/0 scram-sha-256 clientcert=verify-full', 'hostnossl all all 0.0.0.0/0 reject']
    if cluster:
        etcd_ca, etcd_certificate, etcd_key = bundle.tls(node, 'postgres', purpose='etcd', authority='etcd')
        _, replication_certificate, replication_key = bundle.tls(node, 'postgres', purpose='replication', common_name='replicator', uri='psim://service/postgres')
        bundle.port(node, 8008, 'patroni')
        config = {'scope': profile.name, 'name': node.name, 'namespace': '/psim/',
                  'restapi': {'listen': node.address + ':8008', 'connect_address': node.name + ':8008',
                              'certfile': certificate, 'keyfile': key, 'cafile': ca, 'verify_client': 'required',
                              'authentication': {'username': 'deployment-admin', 'password': '{{secret:postgres/rest.secret}}'}},
                  'etcd3': {'hosts': [n.name + ':2379' for n in nodes], 'protocol': 'https', 'cacert': etcd_ca, 'cert': etcd_certificate, 'key': etcd_key},
                  'bootstrap': {'dcs': {'ttl': 30, 'loop_wait': 5, 'retry_timeout': 5, 'maximum_lag_on_failover': 0,
                                        'synchronous_mode': True, 'synchronous_mode_strict': True, 'synchronous_node_count': 1,
                                        'check_timeline': True, 'postgresql': {'use_pg_rewind': True, 'parameters': common}},
                                'initdb': [{'encoding': 'UTF8'}, 'data-checksums'], 'pg_hba': hba,
                                'post_init': '/usr/local/bin/psim-postgres-env /bin/sh ' + CONFIG + '/postgres/secrets/bootstrap.sh'},
                  'postgresql': {'listen': node.address + ':5432', 'connect_address': node.name + ':5432',
                                 'data_dir': '/var/lib/postgresql/data', 'bin_dir': '/opt/psim/postgres-bin',
                                 'basebackup': [{'checkpoint': 'fast'}],
                                 'pgpass': '/tmp/pgpass',
                                 'use_unix_socket': True,
                                 'authentication': {'superuser': {'username': 'postgres', 'password': '{{secret:postgres/admin.secret}}',
                                     'sslmode': 'verify-full', 'sslrootcert': ca, 'sslcert': certificate, 'sslkey': key},
                                                    'replication': {'username': 'replicator', 'password': '{{secret:postgres/replication.secret}}',
                                     'sslmode': 'verify-full', 'sslrootcert': ca, 'sslcert': replication_certificate, 'sslkey': replication_key}}},
                  'watchdog': {'mode': 'required', 'device': '/dev/watchdog', 'safety_margin': -1}}
        bundle.credential(node, 'postgres', 'rest.secret')
        command = ['patroni', protected(bundle, node, 'postgres', 'patroni.json', json.dumps(config, indent=2))]
        etcd(bundle, node, nodes)
    else:
        common['listen_addresses'] = node.address
        common['hba_file'] = CONFIG + '/postgres/secrets/pg_hba.conf'
        protected(bundle, node, 'postgres', 'pg_hba.conf', '\n'.join(hba) + '\n')
        protected(bundle, node, 'postgres', 'postgresql.conf', '\n'.join(key + "='" + str(value).replace("'", "''") + "'" for key, value in common.items()) + '\n')
        command = ['/usr/local/bin/psim-postgres-single']
    protected(bundle, node, 'postgres', 'bootstrap.sh', '#!/bin/sh\nset -eu\npsql --no-psqlrc --set ON_ERROR_STOP=1 -d postgres -f ' + CONFIG + '/postgres/secrets/bootstrap.sql\nPSIM_PG_HOST=/tmp PSIM_PG_DATABASE=psim PSIM_PG_USER=postgres /usr/bin/psim-audit-tool migrate audit_service\n')
    protected(bundle, node, 'postgres', 'bootstrap.sql', '{{postgres-bootstrap}}')
    protected(bundle, node, 'postgres', 'passwd', f'postgres:x:{profile.uid("postgres")}:{profile.uid("postgres")}:psim-platform:/var/lib/postgresql:/usr/sbin/nologin\n')
    protected(bundle, node, 'postgres', 'group', f'postgres:x:{profile.uid("postgres")}:\n')
    service = bundle.service(node, 'postgres', 'psim-platform/postgres:' + profile.version, command, volumes=[data(bundle, node, 'postgres') + ':/var/lib/postgresql'], environment={'PGHOST': '/tmp'})
    service['shm_size'] = '256m'
    if cluster:
        service['devices'] = ['/dev/watchdog:/dev/watchdog']


def etcd(bundle, node, nodes):
    for port in (2379, 2380):
        bundle.port(node, port, 'etcd')
    ca, certificate, key = bundle.tls(node, 'etcd', dns=[n.name for n in nodes], authority='etcd')
    config = {'name': node.name, 'data-dir': '/var/lib/etcd', 'listen-peer-urls': 'https://' + node.address + ':2380',
              'initial-advertise-peer-urls': 'https://' + node.name + ':2380', 'listen-client-urls': 'https://' + node.address + ':2379',
              'advertise-client-urls': 'https://' + node.name + ':2379',
              'initial-cluster': ','.join(n.name + '=https://' + n.name + ':2380' for n in nodes),
              'initial-cluster-token': bundle.profile.name, 'initial-cluster-state': 'new',
              'client-transport-security': {'cert-file': certificate, 'key-file': key, 'trusted-ca-file': ca, 'client-cert-auth': True},
              'peer-transport-security': {'cert-file': certificate, 'key-file': key, 'trusted-ca-file': ca, 'client-cert-auth': True}}
    filename = protected(bundle, node, 'etcd', 'etcd.json', json.dumps(config, indent=2))
    bundle.service(node, 'etcd', bundle.images['etcd'], ['--config-file', filename], volumes=[data(bundle, node, 'etcd') + ':/var/lib/etcd'])['entrypoint'] = ['/usr/local/bin/etcd']


def valkey(bundle, node, index, nodes):
    bundle.port(node, 6379, 'valkey')
    ca, certificate, key = bundle.tls(node, 'valkey', dns=[n.name for n in nodes])
    acl = protected(bundle, node, 'valkey', 'users.acl', '{{valkey-acl}}')
    password = bundle.credential(node, 'valkey', 'replication.secret')
    conf = [f'bind {node.address}', 'port 0', 'tls-port 6379', 'protected-mode yes', 'tls-auth-clients yes',
            'tls-replication yes', f'tls-cert-file {certificate}', f'tls-key-file {key}', f'tls-ca-cert-file {ca}',
            'tls-protocols "TLSv1.2 TLSv1.3"', 'appendonly yes', 'appendfsync everysec',
            'maxmemory 512mb', 'maxmemory-policy noeviction', 'dir /var/lib/valkey', f'aclfile {acl}']
    if len(nodes) == 3:
        conf.extend(['masteruser replicator', 'masterauth {{secret:valkey/replication.secret}}'])
        if index != 1:
            conf.append('replicaof ' + nodes[0].name + ' 6379')
    config = protected(bundle, node, 'valkey', 'valkey.conf', '\n'.join(conf) + '\n')
    start = f'umask 077; if [ ! -f /var/lib/valkey/valkey.conf ]; then cp {config} /var/lib/valkey/valkey.conf; chmod 0600 /var/lib/valkey/valkey.conf; fi; exec valkey-server /var/lib/valkey/valkey.conf'
    bundle.service(node, 'valkey', bundle.images['valkey'], ['sh', '-ec', start], volumes=[data(bundle, node, 'valkey') + ':/var/lib/valkey'])['entrypoint'] = []
    if len(nodes) != 3:
        return
    bundle.port(node, 26379, 'sentinel')
    ca, certificate, key = bundle.tls(node, 'sentinel', dns=[n.name for n in nodes])
    bundle.credential(node, 'sentinel', 'monitor.secret')
    bundle.credential(node, 'sentinel', 'peer.secret')
    sentinel_acl = protected(bundle, node, 'sentinel', 'users.acl', '{{sentinel-acl}}')
    conf = [f'bind {node.address}', 'port 0', 'tls-port 26379', 'tls-replication yes', 'tls-auth-clients yes',
            f'tls-cert-file {certificate}', f'tls-key-file {key}', f'tls-ca-cert-file {ca}', f'aclfile {sentinel_acl}',
            'dir /var/lib/sentinel', 'sentinel resolve-hostnames yes', 'sentinel announce-hostnames yes',
            'sentinel sentinel-user sentinel-peer', 'sentinel sentinel-pass {{secret:sentinel/peer.secret}}',
            f'sentinel announce-ip {node.name}', 'sentinel announce-port 26379',
            f'sentinel monitor {bundle.profile.name} {nodes[0].name} 6379 2',
            f'sentinel auth-user {bundle.profile.name} sentinel-monitor',
            f'sentinel auth-pass {bundle.profile.name} {{{{secret:sentinel/monitor.secret}}}}',
            f'sentinel down-after-milliseconds {bundle.profile.name} 5000',
            f'sentinel failover-timeout {bundle.profile.name} 30000', f'sentinel parallel-syncs {bundle.profile.name} 1']
    initial = protected(bundle, node, 'sentinel', 'initial.conf', '\n'.join(conf) + '\n')
    start = f'umask 077; if [ ! -f /var/lib/sentinel/sentinel.conf ]; then cp {initial} /var/lib/sentinel/sentinel.conf; chmod 0600 /var/lib/sentinel/sentinel.conf; fi; exec valkey-sentinel /var/lib/sentinel/sentinel.conf'
    bundle.service(node, 'sentinel', bundle.images['valkey'], ['sh', '-ec', start], volumes=[data(bundle, node, 'sentinel') + ':/var/lib/sentinel'])['entrypoint'] = []


def keeper(bundle, node, index, nodes):
    bundle.port(node, 9281, 'keeper')
    bundle.port(node, 9234, 'keeper-raft')
    ca, certificate, key = bundle.tls(node, 'keeper', dns=[n.name for n in nodes], authority='keeper')
    members = ''.join(f'<server><id>{i}</id><hostname>{n.name}</hostname><port>9234</port></server>' for i, n in enumerate(nodes, 1))
    xml = f'''<clickhouse>
<listen_host>{node.address}</listen_host><logger><level>warning</level><console>1</console></logger>
<keeper_server><tcp_port_secure>9281</tcp_port_secure><server_id>{index}</server_id>
<log_storage_path>/var/lib/clickhouse/coordination/log</log_storage_path><snapshot_storage_path>/var/lib/clickhouse/coordination/snapshots</snapshot_storage_path>
<raft_configuration><secure>true</secure>{members}</raft_configuration>
<coordination_settings><operation_timeout_ms>10000</operation_timeout_ms><session_timeout_ms>30000</session_timeout_ms></coordination_settings>
</keeper_server><openSSL><server><certificateFile>{certificate}</certificateFile><privateKeyFile>{key}</privateKeyFile><caConfig>{ca}</caConfig><verificationMode>strict</verificationMode><loadDefaultCAFile>false</loadDefaultCAFile><disableProtocols>sslv2,sslv3,tlsv1,tlsv1_1</disableProtocols></server>
<client><certificateFile>{certificate}</certificateFile><privateKeyFile>{key}</privateKeyFile><caConfig>{ca}</caConfig><verificationMode>strict</verificationMode><loadDefaultCAFile>false</loadDefaultCAFile></client></openSSL></clickhouse>'''
    config = protected(bundle, node, 'keeper', 'keeper.xml', xml)
    bundle.service(node, 'keeper', bundle.images['keeper'], ['--config-file=' + config], volumes=[data(bundle, node, 'keeper') + ':/var/lib/clickhouse'])['entrypoint'] = ['/usr/bin/clickhouse-keeper']


def clickhouse(bundle, node, index, nodes):
    for port in (9440, 9010):
        bundle.port(node, port, 'clickhouse')
    ca, certificate, key = bundle.tls(node, 'clickhouse', dns=['clickhouse', *[n.name for n in nodes]])
    _, keeper_certificate, keeper_key = bundle.tls(node, 'clickhouse', purpose='keeper', authority='keeper')
    client_ca = bundle.credential(node, 'clickhouse', 'client-ca.pem', 'trust-bundle', authorities=['platform', 'keeper'])
    keepers = ''.join(f'<node><host>{n.name}</host><port>9281</port><secure>1</secure></node>' for n in nodes)
    replicas = ''.join(f'<replica><host>{n.name}</host><port>9440</port><secure>1</secure><user>cluster-peer</user><password>{{{{secret:clickhouse/peer.secret}}}}</password></replica>' for n in nodes[:2])
    bundle.credential(node, 'clickhouse', 'peer.secret')
    bundle.credential(node, 'clickhouse', 'interserver.secret')
    xml = f'''<clickhouse>
<listen_host>{node.address}</listen_host><tcp_port_secure>9440</tcp_port_secure>
<interserver_https_port>9010</interserver_https_port><interserver_http_host>{node.name}</interserver_http_host>
<interserver_http_credentials><user>replication</user><password>{{{{secret:clickhouse/interserver.secret}}}}</password></interserver_http_credentials>
<logger><level>warning</level><console>1</console></logger><path>/var/lib/clickhouse/</path>
<access_control_improvements><settings_constraints_replace_previous>true</settings_constraints_replace_previous></access_control_improvements>
<zookeeper>{keepers}</zookeeper>
<distributed_ddl><path>/distributed-ddl</path></distributed_ddl>
<macros><shard>1</shard><replica>{node.name}</replica></macros>
<remote_servers><psim><shard><internal_replication>true</internal_replication>{replicas}</shard></psim></remote_servers>
<openSSL><server><certificateFile>{certificate}</certificateFile><privateKeyFile>{key}</privateKeyFile><caConfig>{client_ca}</caConfig><verificationMode>strict</verificationMode><loadDefaultCAFile>false</loadDefaultCAFile><disableProtocols>sslv2,sslv3,tlsv1,tlsv1_1</disableProtocols></server>
<client><certificateFile>{keeper_certificate}</certificateFile><privateKeyFile>{keeper_key}</privateKeyFile><caConfig>{client_ca}</caConfig><verificationMode>strict</verificationMode><loadDefaultCAFile>false</loadDefaultCAFile></client></openSSL>
<users_config>{CONFIG}/clickhouse/secrets/users.xml</users_config>
</clickhouse>'''
    config = protected(bundle, node, 'clickhouse', 'server.xml', xml)
    protected(bundle, node, 'clickhouse', 'users.xml', '{{clickhouse-users}}')
    bundle.service(node, 'clickhouse', bundle.images['clickhouse'], ['--config-file=' + config], volumes=[data(bundle, node, 'clickhouse') + ':/var/lib/clickhouse'])['entrypoint'] = ['/usr/bin/clickhouse-server']


def seaweed(bundle, node, index, nodes):
    ca, certificate, key = bundle.tls(node, 'seaweed', dns=['s3', *[n.name for n in nodes]])
    internal_ca, internal_certificate, internal_key = bundle.tls(node, 'seaweed', purpose='internal', authority='seaweed')
    security = f'''[grpc]
ca = "{internal_ca}"
[grpc.master]
cert = "{internal_certificate}"
key = "{internal_key}"
allowed_commonNames = "seaweed,deployment-admin"
[grpc.volume]
cert = "{internal_certificate}"
key = "{internal_key}"
allowed_commonNames = "seaweed,deployment-admin"
[grpc.filer]
cert = "{internal_certificate}"
key = "{internal_key}"
allowed_commonNames = "seaweed,deployment-admin"
[grpc.client]
cert = "{internal_certificate}"
key = "{internal_key}"
[jwt.signing]
key = "{{{{secret:seaweed/jwt.secret}}}}"
[jwt.filer_signing]
key = "{{{{secret:seaweed/filer-jwt.secret}}}}"
[jwt.filer_signing.read]
key = "{{{{secret:seaweed/filer-read-jwt.secret}}}}"
[https.client]
enabled = true
cert = "{internal_certificate}"
key = "{internal_key}"
ca = "{internal_ca}"
[https.master]
cert = "{internal_certificate}"
key = "{internal_key}"
ca = "{internal_ca}"
[https.volume]
cert = "{internal_certificate}"
key = "{internal_key}"
ca = "{internal_ca}"
[https.filer]
cert = "{internal_certificate}"
key = "{internal_key}"
ca = "{internal_ca}"
[filer.expose_directory_metadata]
enabled = false
'''
    for name in ('jwt.secret', 'filer-jwt.secret', 'filer-read-jwt.secret'):
        bundle.credential(node, 'seaweed', name)
    security_file = protected(bundle, node, 'seaweed', 'security.toml', security)
    material_root = bundle.profile.prefix + '/material/' + node.name + '/seaweed/'
    security_mount = material_root + 'security.toml:/etc/seaweedfs/security.toml:ro'
    master = 'seaweed-master'
    for port in (9333, 19333):
        bundle.port(node, port, master)
    peers = ','.join(n.name + ':9333' for n in nodes)
    replication = '010' if len(nodes) == 3 else '000'
    directory = data(bundle, node, 'seaweed-master')
    volumes = [directory + ':/data', security_mount]
    bundle.service(node, master, bundle.images['seaweed'], ['master', '-ip=' + node.name, '-ip.bind=' + node.address,
                   '-port=9333', '-mdir=/data', '-peers=' + peers, '-defaultReplication=' + replication], owner='seaweed', volumes=volumes)['entrypoint'] = ['/usr/bin/weed']
    # Each volume server is a separate rack, so replicas cannot share a host.
    for port in (18080, 28080):
        bundle.port(node, port, 'seaweed-volume')
    bundle.service(node, 'seaweed-volume', bundle.images['seaweed'], ['volume', '-ip=' + node.name, '-ip.bind=' + node.address,
                   '-port=18080', '-mserver=' + peers, '-rack=' + node.domain, '-dataCenter=psim', '-dir=/data', '-max=16'],
                   owner='seaweed', volumes=[data(bundle, node, 'seaweed-volume') + ':/data', security_mount])['entrypoint'] = ['/usr/bin/weed']
    if index > 2:
        return
    for port in (18888, 28888, 18333):
        bundle.port(node, port, 'seaweed-filer-s3')
    bundle.credential(node, 'seaweed', 'postgres.secret')
    filer = f'''[leveldb2]
enabled = false
[postgres2]
enabled = true
hostname = "postgres"
port = 15432
username = "seaweed"
password = "{{{{secret:seaweed/postgres.secret}}}}"
database = "seaweed"
schema = "public"
sslmode = "verify-full"
sslrootcert = "{ca}"
sslcert = "{certificate}"
sslkey = "{key}"
connection_max_idle = 4
connection_max_open = 16
connection_max_lifetime_seconds = 600
'''
    filer_file = protected(bundle, node, 'seaweed', 'filer.toml', filer)
    s3_config = protected(bundle, node, 'seaweed', 's3.json', '{{s3-identities}}')
    common = [security_mount, material_root + 'filer.toml:/etc/seaweedfs/filer.toml:ro']
    bundle.service(node, 'seaweed-filer', bundle.images['seaweed'], ['filer', '-ip=' + node.name, '-ip.bind=' + node.address,
                   '-port=18888', '-master=' + peers, '-defaultReplicaPlacement=' + replication, '-encryptVolumeData'], owner='seaweed',
                   volumes=[data(bundle, node, 'seaweed-filer') + ':/data', *common])['entrypoint'] = ['/usr/bin/weed']
    bundle.service(node, 'seaweed-s3', bundle.images['seaweed'], ['s3', '-ip.bind=' + node.address, '-port=0', '-port.https=18333',
                   '-filer=' + ','.join(n.name + ':18888' for n in nodes[:2]), '-config=' + s3_config,
                   '-cert.file=' + certificate, '-key.file=' + key, '-encryptVolumeData', '-port.iceberg=0', '-port.lance=0'], owner='seaweed',
                   volumes=[data(bundle, node, 'seaweed-s3') + ':/data', *common])['entrypoint'] = ['/usr/bin/weed']


def identity(bundle, node):
    bundle.port(node, 18445, 'keycloak')
    ca, certificate, key = bundle.tls(node, 'keycloak', dns=['keycloak', bundle.profile.public_name])
    bundle.credential(node, 'keycloak', 'postgres.secret')
    pg_key = bundle.credential(node, 'keycloak', 'postgres.pk8', 'private-key-der', source='identity.key')
    config = text_properties({'db': 'postgres', 'db-url': 'jdbc:postgresql://postgres:15432/keycloak?sslmode=verify-full&sslrootcert=' + ca + '&sslcert=' + certificate + '&sslkey=' + pg_key,
        'db-username': 'keycloak', 'db-password': '{{secret:keycloak/postgres.secret}}',
        'hostname': bundle.profile.origin + '/identity', 'http-relative-path': '/identity',
        'http-enabled': 'false', 'http-host': node.address, 'https-port': 18445, 'https-certificate-file': certificate,
        'https-certificate-key-file': key, 'health-enabled': 'true', 'metrics-enabled': 'true',
        'http-management-port': 19000, 'proxy-headers': 'xforwarded',
        'cache-embedded-network-bind-address': node.address, 'cache-embedded-mtls-enabled': 'true',
        'cache': 'ispn',
        'spi-connections-jpa-quarkus-migration-strategy': 'update'})
    bundle.port(node, 19000, 'keycloak-management')
    if bundle.profile.topology == 'cluster':
        bundle.port(node, 7800, 'keycloak-cache')
        bundle.port(node, 57800, 'keycloak-cache-health')
    config_file = protected(bundle, node, 'keycloak', 'keycloak.conf', config)
    realm = protected(bundle, node, 'keycloak', 'realm.json', '{{keycloak-realm}}')
    bundle.service(node, 'keycloak', 'psim-platform/keycloak:' + bundle.profile.version, ['--config-file=' + config_file, 'start', '--optimized', '--import-realm'],
                   volumes=[data(bundle, node, 'keycloak') + ':/opt/keycloak/data', bundle.profile.prefix + '/material/' + node.name + '/keycloak/realm.json:/opt/keycloak/data/import/psim.json:ro'],
                   environment={'JAVA_OPTS_KC_HEAP': '-Xms128m -Xmx768m'})


def registry(bundle, node):
    bundle.port(node, 18181, 'registry')
    bundle.port(node, 19001, 'registry-management')
    bundle.credential(node, 'registry', 'postgres.secret')
    ca, certificate, key = bundle.tls(node, 'registry', dns=['registry'])
    roles = protected(bundle, node, 'registry', 'certificate-roles.properties', 'deployment-admin=registry-admin\n')
    pg_key = bundle.credential(node, 'registry', 'postgres.pk8', 'private-key-der', source='identity.key')
    config = text_properties({'quarkus.http.host': node.address, 'quarkus.http.ssl-port': 18181,
        'quarkus.http.insecure-requests': 'disabled', 'quarkus.http.ssl.client-auth': 'required',
        'quarkus.http.ssl.certificate.files': certificate, 'quarkus.http.ssl.certificate.key-files': key,
        'quarkus.http.ssl.certificate.trust-store-file': ca, 'quarkus.http.ssl.certificate.trust-store-file-type': 'PEM',
        'quarkus.http.auth.certificate-role-properties': roles,
        'quarkus.http.auth.permission.read.paths': '/*', 'quarkus.http.auth.permission.read.methods': 'GET,HEAD,OPTIONS',
        'quarkus.http.auth.permission.read.policy': 'authenticated',
        'quarkus.http.auth.permission.write.paths': '/*', 'quarkus.http.auth.permission.write.methods': 'POST,PUT,DELETE,PATCH',
        'quarkus.http.auth.permission.write.policy': 'registry-admin',
        'quarkus.http.auth.policy.registry-admin.roles-allowed': 'registry-admin',
        'quarkus.management.host': node.address, 'quarkus.management.port': 19001,
        'apicurio.storage.kind': 'sql', 'apicurio.storage.sql.kind': 'postgresql',
        'apicurio.datasource.url': 'jdbc:postgresql://postgres:15432/registry?sslmode=verify-full&sslrootcert=' + ca + '&sslcert=' + certificate + '&sslkey=' + pg_key,
        'apicurio.datasource.username': 'registry', 'apicurio.datasource.password': '{{secret:registry/postgres.secret}}',
        'apicurio.ccompat.legacy-id-mode.enabled': 'true',
        'apicurio.rest.deletion.artifact.enabled': 'false'})
    config_file = protected(bundle, node, 'registry', 'application.properties', config)
    bundle.service(node, 'registry', 'psim-platform/registry:' + bundle.profile.version, [], environment={'QUARKUS_CONFIG_LOCATIONS': config_file,
                   'JAVA_OPTS': '-Xms128m -Xmx512m -XX:+ExitOnOutOfMemoryError'})


def proxy(bundle, node):
    profile = bundle.profile
    infra = profile.select('infrastructure')
    apps = profile.select('application')
    ca, certificate, key = bundle.tls(node, 'haproxy', dns=[profile.public_name])
    combined = protected(bundle, node, 'haproxy', 'public.pem', '{{combine:identity.pem,identity.key}}')
    sections = ['global\n  log stdout format raw local0\n  maxconn 20000\n  tune.ssl.default-dh-param 2048\n',
                'defaults\n  log global\n  timeout connect 3s\n  timeout client 60s\n  timeout server 60s\n  timeout tunnel 1h\n']
    def tcp(name, port, targets, check_port=None, primary=False):
        bundle.port(node, port, name)
        text = f'listen {name}\n  bind 0.0.0.0:{port}\n  mode tcp\n  balance roundrobin\n'
        if primary:
            text += f'  option httpchk GET /primary\n  http-check expect status 200\n'
        elif check_port:
            text += '  option httpchk GET /health/ready\n  http-check expect status 200\n'
        for i, (host, target_port) in enumerate(targets, 1):
            options = 'check inter 1s fall 2 rise 2'
            if primary:
                options += f' check-ssl verify required ca-file {ca} crt {combined} port 8008 check-sni postgres verifyhost postgres'
            elif check_port:
                options += f' port {check_port}'
            text += f'  server endpoint-{i} {host}:{target_port} {options}\n'
        sections.append(text)
    tcp('postgres', 15432, [(n.name, 5432) for n in infra], primary=profile.topology == 'cluster')
    tcp('clickhouse', 19440, [(n.name, 9440) for n in infra[:2]])
    tcp('registry', 18443, [(n.name, 18181) for n in infra[:2]])
    tcp('keycloak', 18444, [(n.name, 18445) for n in infra[:2]])
    tcp('s3', 18446, [(n.name, 18333) for n in infra[:2]])
    for index, (name, (_, port, _)) in enumerate(SERVICES.items()):
        if name in ('normalizer', 'correlation-engine'):
            continue
        tcp(name, port + 10000, [(n.name, port) for n in apps], check_port=19100 + index)
    if node in profile.select('edge'):
        bundle.port(node, 443, 'public-https')
        bundle.port(node, 7443, 'public-connectors')
        sections.append(f'''frontend public-https
  bind 0.0.0.0:443 ssl crt {combined} alpn h2,http/1.1 ssl-min-ver TLSv1.3
  mode http
  http-request set-header X-Forwarded-Proto https
  http-request set-header X-Forwarded-Port {profile.public_port}
  http-response set-header Strict-Transport-Security max-age=31536000
  acl identity path_beg /identity/
  acl api path_beg /v1/ /ws
  use_backend public-identity if identity
  use_backend public-api if api
  default_backend public-web
backend public-identity
  mode http
'''+''.join(f'  server identity-{i} {n.name}:18445 ssl verify required ca-file {ca} verifyhost keycloak sni str(keycloak) check\n' for i,n in enumerate(infra[:2],1))+f'''backend public-api
  mode http
  option httpchk GET /health/ready
'''+''.join(f'  server api-{i} {n.name}:{SERVICES["api-gateway"][1]} ssl verify required ca-file {ca} verifyhost api-gateway sni str(api-gateway) check port {19100 + list(SERVICES).index("api-gateway")} no-check-ssl\n' for i,n in enumerate(apps,1))+'''backend public-web
  mode http
'''+''.join(f'  server web-{i} {n.name}:18180 ssl verify required ca-file {ca} crt {combined} verifyhost web sni str(web) check\n' for i,n in enumerate(apps,1)))
        sections.append(f'''listen public-connectors
  bind 0.0.0.0:7443
  mode tcp
  balance leastconn
'''+''.join(f'  server gateway-{i} {n.name}:17443 check\n' for i,n in enumerate(apps,1)))
    config = protected(bundle, node, 'haproxy', 'haproxy.cfg', '\n'.join(sections))
    service = bundle.service(node, 'haproxy', 'psim-platform/haproxy:' + profile.version, ['-W', '-db', '-f', config])
    if node in profile.select('edge'):
        service['cap_add'] = ['NET_BIND_SERVICE']
    # VRRP stays in a native unit: only this process receives network administration rights.
    if profile.topology == 'cluster' and node in profile.select('edge'):
        index = profile.select('edge').index(node)
        peers = '\n'.join('    ' + n.address for n in profile.select('edge') if n != node)
        bundle.write(node.name + '/keepalived.conf', f'''global_defs {{
  router_id {profile.name}-{node.name}
  enable_script_security
}}
vrrp_track_process haproxy {{
  process haproxy
  quorum 1
  delay 2
}}
vrrp_instance PSIM {{
  state BACKUP
  interface {profile.interface}
  virtual_router_id 91
  priority {150-index*10}
  advert_int 1
  unicast_src_ip {node.address}
  unicast_peer {{
{peers}
  }}
  virtual_ipaddress {{
    {profile.virtual_ip}
  }}
  track_process {{
    haproxy
  }}
}}
''')


def web(bundle, node):
    bundle.port(node, 18180, 'web')
    ca, certificate, key = bundle.tls(node, 'web', dns=['web'])
    service = bundle.service(node, 'web', 'psim-platform/web:' + bundle.profile.version, [], environment={
        'PSIM_LISTEN': node.address, 'PSIM_PORT': '18180', 'PSIM_ISSUER': bundle.profile.issuer,
        'PSIM_CA_FILE': ca, 'PSIM_CERTIFICATE_FILE': certificate, 'PSIM_PRIVATE_KEY_FILE': key})
    service['entrypoint'] = ['node', '/srv/server.mjs']


def observability(bundle, node):
    # The collector accepts only verified TLS; product code carries no client-side telemetry credentials.
    bundle.port(node, 4317, 'otel')
    ca, certificate, key = bundle.tls(node, 'observability', dns=[node.name])
    config = {'receivers': {'otlp': {'protocols': {'grpc': {'endpoint': node.address + ':4317', 'tls': {'cert_file': certificate, 'key_file': key}}}}},
        'processors': {'memory_limiter': {'check_interval': '1s', 'limit_mib': 256}, 'batch': {'timeout': '1s'}},
        'exporters': {'debug': {'verbosity': 'basic'}},
        'service': {'pipelines': {kind: {'receivers': ['otlp'], 'processors': ['memory_limiter', 'batch'], 'exporters': ['debug']} for kind in ('traces', 'metrics', 'logs')}}}
    filename = protected(bundle, node, 'observability', 'collector.json', json.dumps(config))
    bundle.service(node, 'otel', bundle.images['otel'], ['--config=' + filename], owner='observability')
