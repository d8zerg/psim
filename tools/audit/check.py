"""Check audit storage and tamper detection against real processes and independent files."""
import concurrent.futures
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import ssl
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(ROOT / 'tools/incidents/check.py'))
IncidentCheck, uid = helpers['Check'], helpers['uid']


class Check(IncidentCheck):
    def __init__(self, project, preset, count):
        super().__init__(project, preset, count)
        self.log.close()
        self.output = ROOT / 'build/audit' / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / 'commands.log').open('w')
        self.archive = self.output / ('checkpoints-' + self.fixtures)
        self.archive.mkdir()
        self.audit_config = self.output / 'audit.json'
        self.signing_key = self.output / 'signing.pem'
        self.trusted_keys = self.output / 'trusted-keys.json'
        self.password = self.output / 'postgres.secret'
        self.password.unlink(missing_ok=True)
        self.password.write_text('psim-test-audit')
        self.password.chmod(0o400)
        services = json.loads(self.override.read_text())['services']
        base = services['incident-service'] | {'profiles': ['audit-test'], 'command': [f'/repo/build/debian13/{preset}/services/audit-service/psim-audit-service', '--config', '/repo/' + str(self.audit_config.relative_to(ROOT))]}
        base['volumes'] = base['volumes'] + [str(self.archive) + ':/checkpoints']
        services.update({'audit-service': base, 'audit-2': base | {'environment': base['environment'] | {'PSIM__SERVICE__INSTANCE': 'audit-2'}}})
        self.override = self.output / 'compose.json'
        self.override.write_text(json.dumps({'services': services}, indent=2))
        self.compose_cmd[-1] = str(self.override)
        self.compose_cmd += ['--profile', 'audit-test', '--profile', 'incident-test']
        self.report = {'passed': False, 'checks': []}
        self.tool = f'build/debian13/{preset}/services/audit-service/psim-audit-tool'

    def sql(self, statement):
        return self.run(self.compose_cmd + ['exec', '-T', 'postgres', 'psql', '-U', 'postgres', '-d', 'psim', '-At', '-v', 'ON_ERROR_STOP=1'], input=statement).strip()

    def aq(self, method='GET', path='/v1/audit/records', body=None, role='auditor', **options):
        return self.request(method, path, body, role=role, service=options.pop('service', 'audit-service'), **options)

    def principals(self):
        super().principals()
        clients = '/admin/realms/psim/clients/' + self.roles_client
        auditor = self.keycloak('GET', clients + '/roles/auditor', self.admin)
        for name in ('other', 'tenant'):
            self.keycloak('POST', '/admin/realms/psim/users/' + self.subjects[name] + '/role-mappings/clients/' + self.roles_client, self.admin, [auditor])

    def emit_records(self, records):
        path = self.output / ('probe-' + uid() + '.json')
        path.write_text(json.dumps({'records': records}))
        self.env_command(f'build/debian13/{self.preset}/tools/commands/psim-command-probe emit /repo/{path.relative_to(ROOT)}')

    def fixture(self, action='fixture.action', tenant=None, **extra):
        return {'topic': 'psim.audit.v1', 'key': tenant or self.tenant, 'repeat': 2, 'value': {
            'envelope': {'tenantId': tenant or self.tenant, 'causationId': uid()},
            'auditEntry': {'actor': {'id': self.subjects['operator'], 'kind': 'ACTOR_KIND_USER'}, 'action': action,
                           'objectType': 'incident', 'objectId': self.fixtures, 'siteId': self.site_id, 'outcome': 'AUDIT_OUTCOME_SUCCESS', **extra}}}

    def prepare(self):
        super().prepare()
        role = self.sql("SELECT count(*) FROM pg_roles WHERE rolname='psim_audit'")
        if role == '0':
            self.sql("CREATE ROLE psim_audit LOGIN PASSWORD 'psim-test-audit'")
        self.sql('GRANT CONNECT ON DATABASE psim TO psim_audit')
        self.env_command(f'PSIM_PG_HOST=postgres PSIM_PG_SSLMODE=disable PSIM_PG_PASSWORD=psim-dev-psim {self.tool} migrate psim_audit')
        self.signing_key.chmod(0o600) if self.signing_key.exists() else None
        self.env_command('openssl genpkey -algorithm ED25519 -out ' + str(self.signing_key.relative_to(ROOT)))
        public = self.env_command('openssl pkey -in ' + str(self.signing_key.relative_to(ROOT)) + ' -pubout')
        self.env_command('openssl pkey -in ' + str(self.signing_key.relative_to(ROOT)) + ' -pubout -outform DER -out ' + str((self.output / 'public.der').relative_to(ROOT)))
        der = (self.output / 'public.der').read_bytes().hex()
        identity = hashlib.sha256(bytes.fromhex(der)[-32:]).hexdigest()
        ring = json.loads(self.trusted_keys.read_text()) if self.trusted_keys.exists() else {}
        ring[identity] = public
        self.trusted_keys.write_text(json.dumps(ring))
        self.signing_key.chmod(0o400)
        self.audit_config.write_text(json.dumps({'service': {'instance': 'audit-1'}, 'runtime': {'shards': 1},
            'telemetry': {'otlp_endpoint': 'otel-collector:4317', 'otlp_tls': False, 'sampling_ratio': 0.001},
            'settings': {'allow_plaintext': True, 'pg_user': 'psim_audit', 'pg_password_file': '/repo/' + str(self.password.relative_to(ROOT)), 'listen_port': 8443,
                         'issuer': 'http://keycloak:8080/realms/psim', 'signing_key_file': '/repo/' + str(self.signing_key.relative_to(ROOT)),
                         'trusted_keys_file': '/repo/' + str(self.trusted_keys.relative_to(ROOT)), 'checkpoint_directory': '/checkpoints',
                         'checkpoint_interval_ms': 5000, 'checkpoint_records': 10}}, indent=2))
        management = self.keycloak('GET', '/admin/realms/psim/clients?clientId=realm-management', self.admin)[0]['id']
        client = self.keycloak('GET', '/admin/realms/psim/clients?clientId=' + self.directory_client, self.admin)[0]['id']
        account = self.keycloak('GET', '/admin/realms/psim/clients/' + client + '/service-account-user', self.admin)['id']
        roles = [self.keycloak('GET', '/admin/realms/psim/clients/' + management + '/roles/' + name, self.admin) for name in ('view-events', 'view-realm')]
        self.keycloak('POST', '/admin/realms/psim/users/' + account + '/role-mappings/clients/' + management, self.admin, roles)
        self.keycloak('PUT', '/admin/realms/psim/events/config', self.admin, {'eventsEnabled': True, 'eventsExpiration': 86400, 'enabledEventTypes': ['LOGIN', 'LOGIN_ERROR', 'LOGOUT', 'REFRESH_TOKEN_ERROR'], 'adminEventsEnabled': True, 'adminEventsDetailsEnabled': False})
        config = json.loads(self.audit_config.read_text())
        config['settings'].update(identity_base='http://keycloak:8080', identity_client=self.directory_client, identity_secret_file='/repo/' + str(self.secret.relative_to(ROOT)), identity_security_tenant=self.tenant)
        self.audit_config.write_text(json.dumps(config, indent=2))
        self.emit_records([self.fixture('fixture.history') for _ in range(self.count)])
        self.compose('up', '-d', '--force-recreate', 'audit-service', 'audit-2')
        for service in ('audit-service', 'audit-2'):
            self.wait(lambda: self.aq(service=service), 90)
        self.wait(lambda: self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='fixture.history'") == str(self.count), 90)
        self.mark('two restricted audit writers import retained Kafka history with duplicate delivery and no repeated records')

    def identity_events(self):
        self.admin = self.keycloak('POST', '/realms/master/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': 'admin-cli', 'username': 'admin', 'password': 'psim-dev-keycloak-admin'}, form=True)['access_token']
        username = 'audit-login-' + self.fixtures
        self.keycloak('POST', '/admin/realms/psim/users', self.admin, {'username': username, 'enabled': True, 'emailVerified': True, 'firstName': 'Audit', 'lastName': 'Fixture', 'email': username + '@example.test', 'attributes': {'psim_tenant': [self.tenant], 'psim_scopes': ['*']}, 'credentials': [{'type': 'password', 'value': 'Audit-Fixture-password-123!', 'temporary': False}]})
        client = 'audit-login-' + self.fixtures
        self.keycloak('POST', '/admin/realms/psim/clients', self.admin, {'clientId': client, 'enabled': True, 'publicClient': True, 'directAccessGrantsEnabled': True, 'standardFlowEnabled': False})
        self.keycloak('POST', '/realms/psim/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': client, 'username': username, 'password': 'Audit-Fixture-password-123!'}, form=True)
        import urllib.error
        try:
            self.keycloak('POST', '/realms/psim/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': client, 'username': username, 'password': 'incorrect-password'}, form=True)
        except urllib.error.HTTPError as error:
            assert error.code == 400 and json.loads(error.read())["error"] == "invalid_grant"
        else:
            raise AssertionError('invalid login succeeded')
        for index in range(105):
            self.keycloak('POST', '/admin/realms/psim/clients', self.admin, {'clientId': 'audit-page-' + self.fixtures + '-' + str(index), 'enabled': False})
        self.wait(lambda: int(self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='identity.admin.CREATE'")) >= 107, 90)
        self.wait(lambda: self.sql("SELECT count(DISTINCT body->'entry'->>'action') FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action' IN ('identity.LOGIN','identity.LOGIN_ERROR','identity.admin.CREATE')") == '3', 90)
        stored = self.sql("SELECT body::text FROM audit.entries WHERE body->'entry'->>'action' LIKE 'identity.%'")
        assert 'Audit-Fixture-password' not in stored and 'access_token' not in stored and 'representation' not in stored
        self.mark('real successful/failed Keycloak logins and administrator changes enter audit through a durable read-only journal bridge without credentials')

    def producers(self):
        self.zone, self.type_id = uid(), uid()
        self.request('POST', '/v1/incident-types', {'incidentTypeId': self.type_id, 'code': self.fixtures, 'name': 'Audit producer fixture', 'defaultPriority': 'PRIORITY_P2', 'groupingWindow': '60s', 'autoCloseAfter': '300s'}, role='admin', expected=201)
        self.emit(count=1, group='audit-' + self.fixtures)
        value = self.wait(lambda: next((item for item in self.request('GET', '/v1/incidents')['incidents'] if item['groupingKey'] == 'audit-' + self.fixtures), None))
        value = self.request('POST', '/v1/incidents/' + value['incidentId'] + ':acknowledge', {}, version=value['version'])['incident']
        value = self.request('POST', '/v1/incidents/' + value['incidentId'] + ':start-work', {}, version=value['version'])['incident']
        self.request('POST', '/v1/incidents/' + value['incidentId'] + ':resolve', {'resolution': 'RESOLUTION_CONFIRMED'}, version=value['version'])
        self.wait(lambda: self.sql("SELECT count(DISTINCT body->'entry'->>'action') FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action' IN ('incident.acknowledge','incident.start-work','incident.resolve')") == '3')
        self.mark('real Incident Service administrative and lifecycle actions are delivered through its outbox and audit Kafka topic')

    def access(self):
        self.aq(role=None, expected=401)
        self.aq(role='operator', expected=403)
        self.aq(role='admin', expected=403)
        self.aq('POST', '/v1/audit:verify', {}, role='other', expected=403)
        self.aq('GET', '/v1/audit/records:export', role='other', expected=403)
        self.aq(role='other', expected=403)
        assert self.aq(path='/v1/audit/records?query.siteId=' + self.foreign_site, role='other')['records'] == []
        self.aq(path='/v1/audit/records?query.siteId=' + self.site_id, role='other', expected=403)
        assert all(record['tenantId'] == self.legacy_tenant for record in self.aq(role='tenant')['records'])
        self.aq(path='/v1/audit/records?unknown=x', expected=400)
        self.aq(path='/v1/audit/records?pageSize=1001', expected=400)
        self.aq(path='/v1/audit/records?pageSize=x', expected=400)
        self.aq(path='/v1/audit/records?pageSize=1&pageSize=2', expected=400)
        self.aq(path='/v1/audit/records?query.action=%XX', expected=400)
        self.aq('POST', '/v1/audit:verify', {'fromSeq': '20', 'toSeq': '10'}, expected=400)
        self.aq('POST', '/v1/audit:verify', {'toSeq': '9999999'}, expected=400)
        self.aq('POST', '/v1/audit/records', {}, expected=404)
        self.wait(lambda: int(self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='access.denied'")) >= 5)
        self.mark('auditor-only access, tenant/site isolation, denial audit and bounded strict HTTP requests')

    def search(self):
        prefix = '/v1/audit/records?query.action=fixture.history&pageSize=7'
        page = self.aq(path=prefix)
        records = page['records']
        while page.get('nextPageToken'):
            page = self.aq(path=prefix + '&pageToken=' + page['nextPageToken'], service='audit-2')
            records += page['records']
        assert len(records) == self.count
        sequences = [int(record['auditSeq']) for record in records]
        assert sequences == sorted(sequences, reverse=True) and len(set(sequences)) == self.count
        token = self.aq(path=prefix)['nextPageToken']
        self.aq(path='/v1/audit/records?query.action=different&pageToken=' + token, expected=400)
        path = '/v1/audit/records?query.actorId=' + self.subjects['operator'] + '&query.siteId=' + self.site_id + '&query.objectType=incident&query.objectId=' + self.fixtures + '&query.outcome=AUDIT_OUTCOME_SUCCESS&query.action=fixture.history&pageSize=1000'
        page = self.aq(path=path)
        filtered = page['records']
        while page.get('nextPageToken'):
            page = self.aq(path=path + '&pageToken=' + page['nextPageToken'])
            filtered += page['records']
        assert len(filtered) == self.count
        self.mark('stable reverse sequence pagination and combined actor, action, object, outcome and site filters')

    def large_records(self):
        records = [self.fixture('fixture.large', details={'snapshot': 'x' * 800000}) for _ in range(12)]
        self.emit_records(records)
        self.wait(lambda: self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='fixture.large'") == '12', 120)
        page = self.aq(path='/v1/audit/records?query.action=fixture.large&pageSize=1000')
        assert len(page['records']) < 12 and page.get('nextPageToken'), len(page['records'])
        values = page['records']
        while page.get('nextPageToken'):
            page = self.aq(path='/v1/audit/records?query.action=fixture.large&pageSize=1000&pageToken=' + page['nextPageToken'])
            values += page['records']
        assert len(values) == 12 and len({value['auditSeq'] for value in values}) == 12
        assert all(len(value['entry']['details']['snapshot']) == 800000 for value in values)
        assert self.aq('POST', '/v1/audit:verify', {})['intact']
        self.mark('existing producer snapshots over 64 KiB are preserved; byte-bounded database reads paginate a result larger than 8 MiB without loss')

    def protections(self):
        for sql in ("UPDATE audit.entries SET body=body", 'DELETE FROM audit.entries', 'TRUNCATE audit.entries', 'ALTER TABLE audit.entries DISABLE TRIGGER ALL', 'UPDATE audit.checkpoints SET body=body', 'DELETE FROM audit.checkpoints'):
            try:
                self.sql('SET ROLE psim_audit; ' + sql)
            except RuntimeError:
                continue
            raise AssertionError('append role permitted: ' + sql)
        try:
            self.sql('UPDATE audit.entries SET body=body')
        except RuntimeError as error:
            assert 'append only' in str(error)
        else:
            raise AssertionError('trigger allowed update')
        self.mark('database grants and immutable triggers block mutations, truncation and trigger bypass by service role')

    def archive_failure(self):
        import urllib.error
        def status():
            try:
                with urllib.request.urlopen('http://localhost:' + self.port('audit-service', 9100) + '/health/ready', timeout=5) as response:
                    return response.status
            except urllib.error.HTTPError as error:
                return error.code
        self.archive.chmod(0)
        try:
            self.wait(lambda: status() == 503, 15)
            self.aq('POST', '/v1/audit:verify', {}, expected=500)
        finally:
            self.archive.chmod(0o700)
        self.wait(lambda: status() == 200, 30)
        assert self.aq('POST', '/v1/audit:verify', {})['intact']
        self.mark('external archive outage fails readiness and integrity inspection; restoring permissions recovers without replacing checkpoints')

    def checkpoint(self):
        if self.preset == 'coverage':
            self.compose('stop', '-t', '35', 'audit-service', 'audit-2')
            self.compose('up', '-d', 'audit-service', 'audit-2')
            for service in ('audit-service', 'audit-2'):
                self.wait(lambda: self.aq(service=service), 90)

    def recovery(self):
        self.checkpoint()
        self.compose('kill', '-s', 'SIGKILL', 'audit-service')
        self.emit_records([self.fixture('fixture.failover') for _ in range(20)])
        self.wait(lambda: self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='fixture.failover'") == '20', 90)
        self.compose('up', '-d', 'audit-service')
        self.wait(lambda: self.aq())
        assert self.aq('POST', '/v1/audit:verify', {})['intact']
        self.mark('SIGKILL writer recovery, second-instance continuation and contiguous chain sequence')

    def export(self):
        # Freeze background consumers while checking an exported snapshot against its exact independent head anchor.
        raw = self.aq(path='/v1/audit/records:export?query.action=fixture.history')
        self.bundle = self.output / 'export.jsonl'
        self.bundle.write_bytes(raw)
        self.frozen_archive = self.output / ('external-copy-' + self.fixtures)
        import shutil
        shutil.copytree(self.archive, self.frozen_archive)
        manifest = '/repo/' + str(self.bundle.relative_to(ROOT))
        keys = '/repo/' + str(self.trusted_keys.relative_to(ROOT))
        archive = '/repo/' + str(self.frozen_archive.relative_to(ROOT))
        result = self.env_command(f'{self.tool} verify {manifest} {keys} {archive}', network=False)
        assert json.loads(result.strip().splitlines()[-1])['intact']
        values = [json.loads(line) for line in raw.splitlines()]
        values[1]['record']['entry']['action'] = 'forged'
        corrupted = self.output / 'corrupted.jsonl'
        corrupted.write_text('\n'.join(json.dumps(value) for value in values) + '\n')
        try:
            self.env_command(f'{self.tool} verify /repo/{corrupted.relative_to(ROOT)} {keys} {archive}', network=False)
        except RuntimeError:
            pass
        else:
            raise AssertionError('offline verifier accepted forged export')
        self.mark('bounded JSONL export is verified offline using separately trusted keys and an external checkpoint copy; forged export rejected')

    def tampering(self):
        # Isolate audit writers so each corruption test has a stable database snapshot.
        self.compose('stop', '-t', '35', 'audit-service', 'audit-2')
        backup = json.loads(self.sql("SELECT json_agg(json_build_object('tenant',tenant,'seq',seq,'source',source,'body',body) ORDER BY seq)::text FROM audit.entries WHERE tenant='" + self.tenant + "'"))
        first = backup[1]
        where = " WHERE tenant='" + self.tenant + "' AND seq=" + str(first['seq'])
        self.sql('ALTER TABLE audit.entries DISABLE TRIGGER immutable_entries')
        self.sql("UPDATE audit.entries SET body=jsonb_set(body,'{entry,action}','\"modified\"')" + where)
        self.compose('up', '-d', 'audit-service', 'audit-2')
        result = self.wait(lambda: self.aq('POST', '/v1/audit:verify', {}), 90)
        assert not result['intact'] and result['firstBrokenSeq'] == str(first['seq']), result
        self.aq(path='/v1/audit/records:export', expected=409)
        self.sql("UPDATE audit.entries SET body='" + json.dumps(first['body']).replace("'", "''") + "'::jsonb" + where)
        assert self.aq('POST', '/v1/audit:verify', {})['intact']
        self.sql('DELETE FROM audit.entries' + where)
        result = self.aq('POST', '/v1/audit:verify', {})
        assert not result['intact'] and result['firstBrokenSeq'] == str(first['seq']), result
        self.sql("INSERT INTO audit.entries VALUES('" + self.tenant + "'," + str(first['seq']) + ",'" + first['source'] + "','" + json.dumps(first['body']).replace("'", "''") + "'::jsonb)")
        self.sql('ALTER TABLE audit.entries ENABLE TRIGGER immutable_entries')
        assert self.aq('POST', '/v1/audit:verify', {})['intact']
        self.mark('direct SQL modification and deletion pinpoint the first broken sequence; corrupt chains cannot be exported or anchored')
        self.compose('stop', '-t', '35', 'audit-service', 'audit-2')
        snapshot = json.loads(self.sql("SELECT json_agg(json_build_object('tenant',tenant,'seq',seq,'source',source,'body',body) ORDER BY seq)::text FROM audit.entries WHERE tenant='" + self.tenant + "'"))
        checkpoints = json.loads(self.sql("SELECT COALESCE(json_agg(json_build_object('seq',seq,'body',body)),'[]'::json)::text FROM audit.checkpoints WHERE tenant='" + self.tenant + "'"))
        manifest = self.output / 'rewrite-input.json'
        manifest.write_text(json.dumps(snapshot))
        rewritten = json.loads(self.env_command(f'build/debian13/{self.preset}/services/audit-service/psim-audit-probe /repo/{manifest.relative_to(ROOT)}'))
        def replace_rows(rows):
            self.sql('ALTER TABLE audit.entries DISABLE TRIGGER immutable_entries')
            self.sql("DELETE FROM audit.entries WHERE tenant='" + self.tenant + "'")
            self.sql("INSERT INTO audit.entries SELECT (value->>'tenant')::uuid,(value->>'seq')::bigint,value->>'source',value->'body' FROM jsonb_array_elements('" + json.dumps(rows).replace("'", "''") + "'::jsonb)")
            self.sql('ALTER TABLE audit.entries ENABLE TRIGGER immutable_entries')
        import copy
        rogue = copy.deepcopy(snapshot[0])
        rogue['seq'] = snapshot[-1]['seq'] + 1
        rogue['source'] = uid()
        rogue['body'].update(auditSeq=str(rogue['seq']), sourceEventId=rogue['source'])
        rogue['body']['entry'].setdefault('details', {})['snapshot'] = 'x' * 9000000
        replace_rows(snapshot + [rogue])
        self.compose('up', '-d', 'audit-service')
        try:
            inserted = self.wait(lambda: self.aq('POST', '/v1/audit:verify', {}), 90)
            assert not inserted['intact'] and inserted['firstBrokenSeq'] == str(rogue['seq']), inserted
        finally:
            self.compose('stop', '-t', '35', 'audit-service')
            replace_rows(snapshot)
        self.mark('an oversized fabricated SQL tail is detected at its exact sequence and cannot be hidden by the database read budget')
        replace_rows(rewritten)
        self.sql('ALTER TABLE audit.checkpoints DISABLE TRIGGER immutable_checkpoints')
        self.sql("DELETE FROM audit.checkpoints WHERE tenant='" + self.tenant + "'")
        self.sql('ALTER TABLE audit.checkpoints ENABLE TRIGGER immutable_checkpoints')
        self.compose('up', '-d', 'audit-service')
        try:
            result = self.wait(lambda: self.aq('POST', '/v1/audit:verify', {}), 90)
            assert not result['intact'] and result['detail'] == 'chain disagrees with external checkpoint', result
        finally:
            self.compose('stop', '-t', '35', 'audit-service')
            replace_rows(snapshot)
            for point in checkpoints:
                self.sql("INSERT INTO audit.checkpoints VALUES('" + self.tenant + "'," + str(point['seq']) + ",'" + json.dumps(point['body']).replace("'", "''") + "'::jsonb) ON CONFLICT DO NOTHING")
        self.mark('coherent SQL rewrite of all hashes with deletion of database checkpoints is detected against the independently archived signed checkpoint')


    def cleanup(self):
        try:
            self.compose('stop', '-t', '35', 'audit-service', 'audit-2', 'incident-service', 'incident-2')
            logs = self.compose('logs', '--no-color', 'audit-service', 'audit-2', 'incident-service', 'incident-2')
            (self.output / 'servers.log').write_text(logs)
            assert not re.search(r'AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:', logs), logs[-3000:]
            if self.owned and os.environ.get('PSIM_AUDIT_KEEP_ENV') != '1':
                self.compose('down', '-v', '--remove-orphans', '--timeout', '10')
        finally:
            (self.output / 'report.json').write_text(json.dumps(self.report, indent=2) + '\n')
            self.log.close()


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.identity_events()
        check.producers()
        check.access()
        check.search()
        check.large_records()
        check.protections()
        check.archive_failure()
        check.recovery()
        check.export()
        check.tampering()
        check.report['passed'] = True
    finally:
        check.cleanup()
    print(json.dumps(check.report, indent=2))


if __name__ == '__main__':
    main()
