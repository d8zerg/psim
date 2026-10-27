"""Exercise the compiled administration CLI with independent disposable storage and real databases."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parents[3]
IMAGE = 'psim-platform/operations-test:local'


def run(*arguments, **kwargs):
    return subprocess.run(arguments, check=True, text=True, capture_output=True, **kwargs).stdout


def prepare(directory):
    directory.mkdir(exist_ok=True); directory.chmod(0o755)
    for name in ('protected', 'state', 'wal-state', 'ch-backups'):
        path = directory / name; path.mkdir(); path.chmod(0o777)
    (directory / 'protected/settings').write_text('synthetic-protected-material')
    run('openssl', 'req', '-x509', '-newkey', 'rsa:3072', '-noenc', '-subj', '/CN=backup-reader',
        '-keyout', str(directory / 'reader.key'), '-out', str(directory / 'reader.pem'))
    for name, value in {'writer.access': 'backup-writer', 'writer.secret': 'backup-writer-fixture',
                        'reader.access': 'backup-reader', 'reader.secret': 'backup-reader-fixture',
                        'plans.access': 'plans-client', 'plans.secret': 'plans-client-fixture',
                        'ch.secret': 'clickhouse-fixture'}.items():
        path = directory / name; path.write_text(value); path.chmod(0o400)
    (directory / 'reader.key').chmod(0o400)
    def identity(name, access, secret, actions):
        return {'name': name, 'credentials': [{'accessKey': access, 'secretKey': secret}], 'actions': actions}
    (directory / 'archive-identities.json').write_text(json.dumps({'identities': [
        identity('bootstrap', 'archive-admin', 'archive-admin-fixture', ['Admin', 'Read', 'Write', 'List']),
        identity('writer', 'backup-writer', 'backup-writer-fixture', ['Write:psim-backups']),
        identity('reader', 'backup-reader', 'backup-reader-fixture', ['Read:psim-backups', 'List:psim-backups'])]}))
    (directory / 'plans-identities.json').write_text(json.dumps({'identities': [identity('plans', 'plans-client', 'plans-client-fixture', ['Admin', 'Read', 'Write', 'List'])]}))
    def storage(endpoint, bucket, owner):
        return {'endpoint': endpoint, 'bucket': bucket, 'access_key_file': '/suite/' + owner + '.access',
                'secret_key_file': '/suite/' + owner + '.secret', 'allow_plaintext': True}
    config = {'format': 1, 'deployment': 'recovery-fixture', 'actor': 'recovery-test', 'state_directory': '/suite/state',
              'postgres': {'host': 'postgres', 'database': 'psim', 'user': 'postgres', 'allow_plaintext': True, 'data_uid': 999},
              'plans': storage('http://plans:8333', 'psim-plans', 'plans'),
              'kafka': {'brokers': 'kafka:9092', 'allow_plaintext': True, 'dlq_sources': {'psim.dlq.normalizer.v1': ['psim.ingest.raw.v1']}},
              'clickhouse': {'host': 'clickhouse', 'port': 9000, 'user': 'default', 'password_file': '/suite/ch.secret',
                             'allow_plaintext': True, 'backup_directory': '/suite/ch-backups', 'data_uid': 101},
              'backup': {'storage': storage('http://archive:8333', 'psim-backups', 'writer'),
                         'read_storage': storage('http://archive:8333', 'psim-backups', 'reader'),
                         'recipient_certificate_file': '/suite/reader.pem', 'recipient_private_key_file': '/suite/reader.key',
                         'protected_directory': '/suite/protected', 'status_file': '/suite/backups.json', 'wal_namespace': 'fixture-' + uuid.uuid4().hex,
                         'pitr_window_seconds': 604800, 'copy_interval_seconds': 3600, 'orphan_grace_seconds': 691200}}
    (directory / 'config.json').write_text(json.dumps(config))
    wal_config = json.loads(json.dumps(config)); wal_config['state_directory'] = '/suite/wal-state'
    for key in ('access_key_file', 'secret_key_file'):
        old = Path(wal_config['backup']['storage'][key]).name
        path = directory / ('pg-' + old); shutil.copyfile(directory / old, path); path.chmod(0o400); os.chown(path, 999, 999)
        wal_config['backup']['storage'][key] = '/suite/' + path.name
    (directory / 'wal.json').write_text(json.dumps(wal_config)); (directory / 'wal.json').chmod(0o644)
    (directory / 'ch.xml').write_text('<clickhouse><storage_configuration><disks><backups><type>local</type><path>/suite/ch-backups/</path></backups></disks></storage_configuration><backups><allowed_disk>backups</allowed_disk><allowed_path>/suite/ch-backups/</allowed_path></backups></clickhouse>')
    (directory / 'pg_hba.conf').write_text('local all all trust\nhost all all all trust\nhost replication all all trust\n')


def verify(directory):
    import boto3
    import psycopg2
    from botocore.config import Config
    from botocore.exceptions import ClientError
    from confluent_kafka import Producer, Consumer, TopicPartition
    from confluent_kafka.admin import AdminClient, NewTopic
    binary = '/repo/build/debian13/release/tools/psimctl/psimctl'
    def cli(*args, config='config.json', expect=0):
        result = subprocess.run([binary, '--config', '/suite/' + config, *args], capture_output=True, text=True, timeout=600)
        assert result.returncode == expect, (args, result.stdout, result.stderr)
        return json.loads(result.stdout) if expect == 0 else result
    def sql(statement, connection='postgres', database='psim'):
        with psycopg2.connect(host=connection, dbname=database, user='postgres') as client:
            client.autocommit = True
            with client.cursor() as cursor:
                cursor.execute(statement)
                return cursor.fetchall() if cursor.description else None
    def until(operation, timeout=120):
        deadline = time.monotonic() + timeout
        while True:
            try:
                value = operation()
                if value: return value
            except Exception:
                if time.monotonic() >= deadline: raise
            if time.monotonic() >= deadline: raise TimeoutError('fixture did not become ready')
            time.sleep(1)
    def s3(endpoint, access, secret):
        return boto3.client('s3', endpoint_url='http://' + endpoint + ':8333', aws_access_key_id=access,
                            aws_secret_access_key=secret, region_name='us-east-1', config=Config(s3={'addressing_style': 'path'}))
    plans = s3('plans', 'plans-client', 'plans-client-fixture')
    archive = s3('archive', 'archive-admin', 'archive-admin-fixture')
    until(lambda: plans.create_bucket(Bucket='psim-plans'))
    until(lambda: archive.create_bucket(Bucket='psim-backups'))
    writer = s3('archive', 'backup-writer', 'backup-writer-fixture')
    until(lambda: writer.put_object(Bucket='psim-backups', Key='permission-probe', Body=b'fixture', IfNoneMatch='*'))
    try:
        writer.get_object(Bucket='psim-backups', Key='permission-probe')
        raise AssertionError('backup writer can read copies')
    except ClientError as error:
        assert error.response['ResponseMetadata']['HTTPStatusCode'] == 403
    until(lambda: sql('SELECT 1'))
    key = str(uuid.uuid4()) + '/' + str(uuid.uuid4()); body = b'synthetic-floor-plan'
    checksum = hashlib.sha256(body).hexdigest()
    plans.put_object(Bucket='psim-plans', Key=key, Body=body, Metadata={'sha256': checksum})
    orphan = str(uuid.uuid4()) + '/' + str(uuid.uuid4())
    plans.put_object(Bucket='psim-plans', Key=orphan, Body=body, Metadata={'sha256': checksum})
    sql('CREATE SCHEMA catalog; CREATE TABLE catalog.files(key text PRIMARY KEY,body jsonb NOT NULL,linked boolean NOT NULL); CREATE TABLE marker(value text); INSERT INTO marker VALUES(\'before\')')
    sql("INSERT INTO catalog.files VALUES ('" + key + "','" + json.dumps({'size': str(len(body)), 'sha256': checksum}) + "',true)")
    bundle = directory / 'migration-bundle'; bundle.mkdir()
    (bundle / 'release.json').write_text(json.dumps({'schema_epoch': 2, 'migrations': [{'engine': 'postgres', 'file': 'postgres.json'}]}))
    statements = ['CREATE TABLE IF NOT EXISTS migration_fixture("поле" integer, unused text)',
                  'CREATE INDEX CONCURRENTLY IF NOT EXISTS migration_fixture_index ON migration_fixture("поле")',
                  'CREATE INDEX CONCURRENTLY IF NOT EXISTS migration_extra_index ON migration_extra(id)']
    plan = {'migrations': [{'version': 1, 'expand': statements, 'contract': ['ALTER TABLE migration_fixture DROP COLUMN IF EXISTS unused']}]}
    (bundle / 'postgres.json').write_text(json.dumps(plan))
    def migrate(*arguments, expect=0):
        environment = {**os.environ, 'PSIM_PG_HOST': 'postgres', 'PSIM_PG_PORT': '5432', 'PSIM_PG_USER': 'postgres', 'PSIM_PG_SSLMODE': 'disable'}
        result = subprocess.run(['/repo/build/debian13/release/tools/pg-migrate/psim-pg-migrate', 'release', '--release', str(bundle), *arguments], env=environment, capture_output=True, text=True)
        assert result.returncode == expect, result.stderr
    migrate(expect=1)
    expected = hashlib.sha256(json.dumps(statements, separators=(',', ':')).encode()).hexdigest()
    assert sql('SELECT checksum,step FROM psim_release.migrations') == [(expected, 2)]
    sql('CREATE TABLE migration_extra(id integer)'); migrate(); migrate()
    assert sql('SELECT step FROM psim_release.migrations') == [(3,)]
    plan['migrations'][0]['expand'] = statements + ['SELECT 1']; (bundle / 'postgres.json').write_text(json.dumps(plan)); migrate(expect=1)
    plan['migrations'][0]['expand'] = statements; (bundle / 'postgres.json').write_text(json.dumps(plan))
    migrate('--phase', 'contract', '--minimum-epoch', '2', expect=1)
    migrate('--phase', 'contract', '--minimum-epoch', '3')
    verified = cli('plans', 'verify')
    assert verified['unlinked'] == [orphan], (verified, plans.list_objects_v2(Bucket='psim-plans').get('Contents'))
    admin = AdminClient({'bootstrap.servers': 'kafka:9092'})
    until(lambda: admin.list_topics(timeout=3).brokers)
    for name, cleanup in [('psim.config.v1', 'compact'), ('psim.normalizer.state.v1', 'compact'), ('psim.ingest.raw.v1', 'delete'), ('psim.dlq.normalizer.v1', 'delete')]:
        admin.create_topics([NewTopic(name, 1, 1, config={'cleanup.policy': cleanup})])[name].result(30)
    producer = Producer({'bootstrap.servers': 'kafka:9092'})
    producer.produce('psim.config.v1', key=b'config', value=b'current')
    producer.produce('psim.config.v1', key=b'empty', value=b'')
    producer.produce('psim.config.v1', key=b'gone', value=None)
    producer.produce('psim.dlq.normalizer.v1', key=b'original', value=b'original-payload', headers=[('psim-dlq-source-topic', b'psim.ingest.raw.v1'), ('psim-dlq-source-partition', b'0')])
    assert producer.flush(30) == 0
    inspected = cli('dlq', 'inspect', '--topic', 'psim.dlq.normalizer.v1', '--partition', '0', '--from-offset', '0', '--to-offset', '1')
    assert inspected['records'] == 1 and not inspected['applied']
    retried = cli('dlq', 'retry', '--topic', 'psim.dlq.normalizer.v1', '--partition', '0', '--from-offset', '0', '--to-offset', '1', '--apply')
    assert retried['records'] == 1 and retried['applied']
    assert cli('dlq', 'retry', '--topic', 'psim.dlq.normalizer.v1', '--partition', '0', '--from-offset', '0', '--to-offset', '1', '--apply')['already_processed']
    consumer = Consumer({'bootstrap.servers': 'kafka:9092', 'group.id': 'fixture-read', 'enable.auto.commit': False, 'isolation.level': 'read_committed'})
    consumer.assign([TopicPartition('psim.ingest.raw.v1', 0, 0)])
    message = until(lambda: consumer.poll(1)); assert not message.error() and message.value() == b'original-payload'; consumer.close()
    tenant, device, source = (str(uuid.uuid4()) for _ in range(3))
    sql('CREATE TABLE catalog.resources(tenant uuid,kind text,id uuid,body jsonb); CREATE TABLE catalog.sources(tenant uuid,device uuid,source uuid)')
    sql("INSERT INTO catalog.resources VALUES ('" + tenant + "','device','" + device + "','{\"state\":\"RESOURCE_STATE_ARCHIVED\"}'); INSERT INTO catalog.sources VALUES ('" + tenant + "','" + device + "','" + source + "')")
    producer.produce('psim.normalizer.state.v1', key=(tenant + '/' + source + ':0').encode(), value=b'cursor-fixture', partition=0)
    assert producer.flush(30) == 0
    assert cli('normalizer', 'cleanup', '--tenant', tenant, '--source', source)['cursors'] == 1
    assert cli('normalizer', 'cleanup', '--tenant', tenant, '--source', source, '--apply')['cursors'] == 1
    assert cli('normalizer', 'cleanup', '--tenant', tenant, '--source', source)['cursors'] == 0
    protected = cli('backup', 'create', '--component', 'identity-pki')
    cli('backup', 'restore-protected', protected['id'], '--directory', '/suite/restored-protected')
    assert (directory / 'restored-protected/settings').read_text() == 'synthetic-protected-material'
    configuration = cli('backup', 'create', '--component', 'product-config')
    config_restore = json.loads((directory / 'config.json').read_text()); config_restore['postgres']['host'] = 'config-restored'
    (directory / 'config-restored.json').write_text(json.dumps(config_restore))
    until(lambda: sql('SELECT 1', 'config-restored'))
    cli('backup', 'restore-config', configuration['id'], '--empty-installation', config='config-restored.json')
    assert sql('SELECT count(*) FROM catalog.files', 'config-restored') == [(1,)]
    cli('backup', 'restore-config', configuration['id'], '--empty-installation', config='config-restored.json', expect=1)
    topics = cli('backup', 'create', '--component', 'kafka-config')
    kafka_restore = json.loads((directory / 'config.json').read_text()); kafka_restore['kafka']['brokers'] = 'kafka-restored:9092'
    (directory / 'kafka-restored.json').write_text(json.dumps(kafka_restore))
    assert cli('backup', 'restore-kafka', topics['id'], '--offline', config='kafka-restored.json')['topics'] == 4
    replayed = Consumer({'bootstrap.servers': 'kafka-restored:9092', 'group.id': 'fixture-restore', 'enable.auto.commit': False})
    replayed.assign([TopicPartition('psim.config.v1', 0, 0)])
    values = {}
    def receive():
        value = replayed.poll(1)
        return (value,) if value is not None else None
    while len(values) < 3:
        value = until(receive)[0]; assert not value.error(); values[value.key()] = value.value()
    assert values == {b'config': b'current', b'empty': b'', b'gone': None}; replayed.close()
    from urllib.request import Request, urlopen
    import base64
    def ch(query, host='clickhouse'):
        request = Request('http://' + host + ':8123/', data=query.encode(), headers={'Authorization': 'Basic ' + base64.b64encode(b'default:clickhouse-fixture').decode()})
        with urlopen(request, timeout=30) as result: return result.read()
    until(lambda: ch('SELECT 1'))
    ch('CREATE TABLE retained_events(id UInt64) ENGINE=MergeTree ORDER BY id'); ch('INSERT INTO retained_events VALUES (7)')
    analytical = cli('backup', 'create', '--component', 'clickhouse')
    ch_restore = json.loads((directory / 'config.json').read_text()); ch_restore['clickhouse']['host'] = 'clickhouse-restored'
    (directory / 'clickhouse-restored.json').write_text(json.dumps(ch_restore))
    until(lambda: ch('SELECT 1', 'clickhouse-restored'))
    cli('backup', 'restore-clickhouse', analytical['id'], config='clickhouse-restored.json')
    assert ch('SELECT id FROM retained_events', 'clickhouse-restored').strip() == b'7'
    started = time.monotonic()
    manifest = cli('backup', 'create', '--component', 'postgres')
    identifier = manifest['id']
    assert manifest['components']['plans']['covered_until_seconds'] >= manifest['checkpoint']['at_seconds']
    assert cli('backup', 'inspect', identifier)['id'] == identifier
    sql("INSERT INTO marker VALUES('between')")
    target_time = sql("SELECT to_char(clock_timestamp() AT TIME ZONE 'UTC','YYYY-MM-DD\"T\"HH24:MI:SS.US\"Z\"')")[0][0]
    sql("INSERT INTO marker VALUES('after')")
    later_plans = cli('backup', 'create', '--component', 'plans')
    wal_file = sql("SELECT pg_walfile_name(pg_switch_wal())")[0][0]
    until(lambda: sql("SELECT last_archived_wal >= '" + wal_file + "' FROM pg_stat_archiver")[0][0], 180)
    plans.delete_object(Bucket='psim-plans', Key=key)
    cli('backup', 'restore-plans', later_plans['id'])
    assert plans.get_object(Bucket='psim-plans', Key=key)['Body'].read() == body
    (directory / 'recovery.json').write_text((directory / 'config.json').read_text())
    cli('backup', 'restore-postgres', identifier, '--directory', '/suite/restored', '--target-time', target_time,
        '--plans-snapshot', later_plans['id'], config='recovery.json')
    restored = json.loads((directory / 'config.json').read_text()); restored['postgres']['host'] = 'restored'
    restored['backup']['storage'] = json.loads((directory / 'wal.json').read_text())['backup']['storage']
    # The PostgreSQL recovery process receives its own read identity and offline decryption key.
    for owner in ('reader.access', 'reader.secret', 'reader.key'):
        path = directory / ('pg-' + owner); shutil.copyfile(directory / owner, path); path.chmod(0o400); os.chown(path, 999, 999)
    wal_read = json.loads((directory / 'wal.json').read_text())
    for field in ('access_key_file', 'secret_key_file'):
        wal_read['backup']['read_storage'][field] = '/suite/pg-' + Path(wal_read['backup']['read_storage'][field]).name
    wal_read['backup']['recipient_private_key_file'] = '/suite/pg-reader.key'
    (directory / 'wal.json').write_text(json.dumps(wal_read))
    recovery = json.loads((directory / 'recovery.json').read_text())
    recovery['backup']['read_storage'] = wal_read['backup']['read_storage']
    recovery['backup']['recipient_private_key_file'] = '/suite/pg-reader.key'
    recovery['state_directory'] = '/suite/wal-state'
    (directory / 'recovery.json').write_text(json.dumps(recovery)); (directory / 'recovery.json').chmod(0o644)
    (directory / 'restored.json').write_text(json.dumps(restored))
    # Signal the host runner that the clean data directory can be started.
    (directory / 'start-restored').touch()
    until(lambda: sql("SELECT pg_is_wal_replay_paused()", 'restored')[0][0], 180)
    assert sql('SELECT value FROM marker ORDER BY value', 'restored') == [('before',), ('between',)]
    assert cli('backup', 'recovery-check', identifier, '--target-time', target_time, config='restored.json')['ready_for_promotion']
    recovered = time.monotonic()
    status = json.loads((directory / 'backups.json').read_text())
    assert status['backups']['postgres']['last_success_seconds'] > 0 and status['backups']['clickhouse']['last_success_seconds'] > 0
    protected_key = 'snapshots/' + protected['id'] + '/manifest.enc'
    encrypted = bytearray(archive.get_object(Bucket='psim-backups', Key=protected_key)['Body'].read())
    encrypted[len(encrypted) // 2] ^= 1
    archive.put_object(Bucket='psim-backups', Key=protected_key, Body=bytes(encrypted), Metadata={'sha256': hashlib.sha256(encrypted).hexdigest()})
    cli('backup', 'inspect', protected['id'], expect=1)
    bundle = '/suite/support.tar.gz'; cli('support', '--output', bundle)
    import tarfile
    with tarfile.open(bundle) as stream:
        assert stream.getnames() == ['diagnostics.json']
        diagnostic = stream.extractfile('diagnostics.json').read()
        assert b'backup-writer-fixture' not in diagnostic and b'PRIVATE KEY' not in diagnostic
    report = {'native_cli': True, 'independent_storage': True, 'encrypted_postgres_pitr': True,
              'plans_sha256': True, 'extra_objects_reported': True, 'dlq_transaction_and_retry': True,
              'post_target_write_excluded': True, 'rpo_at_target_records': 0,
              'backup_and_restore_seconds': recovered - started, 'live_cluster_qualified': False,
              'compacted_topics_restore': True, 'native_clickhouse_backup_restore': True,
              'normalizer_tombstones': True, 'protected_material_restore': True, 'writer_read_denied': True}
    report.update(product_configuration_import=True, corrupted_encrypted_manifest_rejected=True,
                  native_release_migrations=True, migration_resume_and_legacy_checksum=True, contract_epoch_barrier=True)
    (directory / 'report.json').write_text(json.dumps(report, indent=2))


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--verify'); args = parser.parse_args()
    if args.verify: verify(Path(args.verify)); return
    output = ROOT / 'build/operations'; output.mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix='recovery-', dir=output)); project = 'psim-recovery-' + uuid.uuid4().hex[:8]
    images = json.loads((ROOT / 'deploy/topologies/images.json').read_text())
    binary = str(ROOT / 'build/debian13/release/tools/psimctl/psimctl')
    mount = str(directory) + ':/suite'; native = binary + ':/usr/bin/psimctl:ro'
    # Private test identities are generated in the container, with the same UID rules as the native clients.
    run('docker', 'run', '--rm', '-v', str(ROOT) + ':/repo', '-w', '/repo', IMAGE, 'python3', '-c',
        'import runpy,sys; from pathlib import Path; runpy.run_path(sys.argv[1])["prepare"](Path(sys.argv[2]))',
        '/repo/tools/psimctl/tests/integration.py', '/repo/' + str(directory.relative_to(ROOT)))
    def weed(name, identities):
        return {'image': images['seaweed'], 'command': ['server', '-dir=/data', '-ip=' + name, '-master.telemetry=false', '-master.volumeSizeLimitMB=64', '-volume.max=8', '-filer', '-s3', '-s3.port=8333', '-s3.config=/suite/' + identities, '-s3.port.iceberg=0', '-s3.port.lance=0'], 'volumes': [mount, name + ':/data']}
    services = {'plans': weed('plans', 'plans-identities.json'), 'archive': weed('archive', 'archive-identities.json'),
                'postgres': {'image': IMAGE, 'environment': {'POSTGRES_DB': 'psim', 'POSTGRES_HOST_AUTH_METHOD': 'trust', 'PGDATA': '/var/lib/postgresql/data'},
                             'volumes': [mount, native, 'postgres:/var/lib/postgresql'],
                             'command': ['postgres', '-c', 'hba_file=/suite/pg_hba.conf', '-c', 'archive_mode=on', '-c', 'archive_timeout=60', '-c', 'archive_command=/usr/bin/psimctl --config /suite/wal.json backup wal-push %f %p']},
                'restored': {'image': IMAGE, 'environment': {'PGDATA': '/suite/restored'}, 'volumes': [mount, native], 'profiles': ['restore'],
                             'command': ['postgres', '-c', 'listen_addresses=*', '-c', 'archive_mode=off']},
                'kafka': {'image': images['kafka'], 'environment': {'KAFKA_NODE_ID': '1', 'KAFKA_PROCESS_ROLES': 'broker,controller',
                           'KAFKA_LISTENERS': 'PLAINTEXT://:9092,CONTROLLER://:9093', 'KAFKA_ADVERTISED_LISTENERS': 'PLAINTEXT://kafka:9092',
                           'KAFKA_LISTENER_SECURITY_PROTOCOL_MAP': 'CONTROLLER:PLAINTEXT,PLAINTEXT:PLAINTEXT', 'KAFKA_CONTROLLER_LISTENER_NAMES': 'CONTROLLER',
                           'KAFKA_CONTROLLER_QUORUM_VOTERS': '1@kafka:9093', 'KAFKA_OFFSETS_TOPIC_REPLICATION_FACTOR': '1',
                           'KAFKA_TRANSACTION_STATE_LOG_REPLICATION_FACTOR': '1', 'KAFKA_TRANSACTION_STATE_LOG_MIN_ISR': '1', 'KAFKA_GROUP_INITIAL_REBALANCE_DELAY_MS': '0'}}}
    compose = {'name': project, 'services': services, 'volumes': {name: {} for name in ('plans', 'archive', 'postgres')}}
    services['kafka-restored'] = json.loads(json.dumps(services['kafka']))
    services['kafka-restored']['environment'].update(KAFKA_ADVERTISED_LISTENERS='PLAINTEXT://kafka-restored:9092', KAFKA_CONTROLLER_QUORUM_VOTERS='1@kafka-restored:9093')
    services['config-restored'] = {'image': IMAGE, 'environment': {'POSTGRES_DB': 'psim', 'POSTGRES_HOST_AUTH_METHOD': 'trust'}}
    for name in ('clickhouse', 'clickhouse-restored'):
        services[name] = {'image': images['clickhouse'], 'environment': {'CLICKHOUSE_PASSWORD': 'clickhouse-fixture'},
                          'volumes': [mount, str(directory / 'ch.xml') + ':/etc/clickhouse-server/config.d/backups.xml:ro']}
    path = directory / 'compose.json'; path.write_text(json.dumps(compose))
    command = ['docker', 'compose', '-f', str(path)]
    try:
        run(*command, 'up', '-d')
        log = (output / 'integration.log').open('w')
        process = subprocess.Popen(['docker', 'run', '--rm', '--network', project + '_default', '-v', str(ROOT) + ':/repo:ro', '-v', mount, '-w', '/repo', IMAGE,
                                    'python3', '-I', 'tools/psimctl/tests/integration.py', '--verify', '/suite'], stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + 900
        while process.poll() is None:
            if (directory / 'start-restored').exists() and not (directory / 'restored-started').exists():
                run(*command, '--profile', 'restore', 'up', '-d', 'restored'); (directory / 'restored-started').touch()
            if time.monotonic() > deadline: process.terminate(); raise TimeoutError('recovery qualification exceeded budget')
            time.sleep(1)
        log.close(); assert process.returncode == 0, 'native recovery test failed; see build/operations/integration.log'
        shutil.copyfile(directory / 'report.json', output / 'report.json'); print((output / 'report.json').read_text())
    finally:
        with (output / 'containers.log').open('w') as stream:
            subprocess.run(command + ['--profile', 'restore', 'logs', '--no-color', '--tail=100'], stdout=stream, stderr=subprocess.STDOUT)
        run(*command, '--profile', 'restore', 'down', '--volumes', '--remove-orphans')
        run('docker', 'run', '--rm', '-v', str(output) + ':/output', IMAGE, 'python3', '-c', 'import shutil,sys; shutil.rmtree(sys.argv[1])', '/output/' + directory.name)


if __name__ == '__main__': main()
