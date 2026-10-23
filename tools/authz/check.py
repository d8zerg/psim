"""Verify real service boundaries, compact scope references and revocation with Keycloak."""
import base64
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import runpy
import re
import sys
import tempfile
import time
import urllib.parse
import urllib.error

ROOT = Path(__file__).resolve().parents[2]
realtime = runpy.run_path(str(ROOT / 'tools/realtime/check.py'))
Base, helpers, uid = realtime['Check'], realtime['helpers'], realtime['uid']
def stamp(offset=0):
    return (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=offset)).isoformat(timespec='milliseconds').replace('+00:00', 'Z')


def private_json(path, value):
    with tempfile.NamedTemporaryFile(mode='w', dir=path.parent, delete=False) as temporary:
        json.dump(value, temporary)
    os.chmod(temporary.name, 0o400)
    os.replace(temporary.name, path)


ROLES = ('operator', 'supervisor', 'admin', 'integrator', 'auditor', 'manager')


class Check(Base):
    def __init__(self, project, preset):
        super().__init__(project, preset)
        self.auth_output = ROOT / 'build/authz' / preset
        self.auth_output.mkdir(parents=True, exist_ok=True)
        self.subjects = {}
        self.matrix_zone = uid()
        self.zone_created = False
        self.owners = ['incident-service', 'command-service', 'response-engine', 'notification-service', 'event-history', 'rule-management', 'audit-service']
        with self.override.open('a') as out:
            for service in self.owners:
                binary = 'correlation-engine' if service == 'rule-management' else service
                config = '/repo/' + str((self.auth_output / (service + '.json')).relative_to(ROOT))
                out.write(f'  {service}:\n    ports: ["127.0.0.1::8443"]\n    command: ' + json.dumps([f'/repo/build/debian13/{preset}/services/{binary}/psim-{binary}', '--config', config]) + '\n')
                if service in ('response-engine', 'notification-service', 'audit-service'):
                    out.write('    read_only: true\n    cap_drop: [ALL]\n    tmpfs: [/tmp]\n    security_opt: ' + json.dumps(['no-new-privileges:true'] + (['seccomp=unconfined'] if preset == 'tsan' else [])) + '\n')
                    out.write(f'    image: {self.toolchain}\n    user: "{os.getuid()}:{os.getgid()}"\n    volumes: ["{ROOT}:/repo", "{ROOT}/build/debian13/{preset}/profiles:/profiles"]\n    environment: {{LLVM_PROFILE_FILE: "/profiles/%p-%m.profraw"}}\n')
        config = json.loads((self.output / 'api.json').read_text())
        config['settings']['incidents_port'] = '8443'
        (self.output / 'api.json').write_text(json.dumps(config))

    def principal_cases(self):
        result = super().principal_cases()
        for role in ROLES:
            for scope, values in [('global', ['*']), ('site', ['site:' + self.site_id]), ('other', ['site:' + self.foreign_site]), ('zone', ['zone:' + self.matrix_zone]), ('empty', [])]:
                result[role + '_' + scope] = (role, values, helpers['TENANT'])
        return result

    def location(self, kind, parent=None, identity=None):
        if kind == 'ZONE' and not self.zone_created:
            identity = self.matrix_zone
            self.zone_created = True
        return super().location(kind, parent, identity)

    def ready(self, service):
        identity = self.compose('ps', '-aq', service).strip()
        state = json.loads(self.run(['docker', 'inspect', '--format', '{{json .State}}', identity]))
        if state['Status'] == 'exited':
            raise AssertionError(service + ' exited during initialization: ' + self.compose('logs', '--tail', '5', service))
        super().ready(service)

    def sql(self, statement):
        return self.compose('exec', '-T', 'postgres', 'psql', '-U', 'postgres', '-d', 'psim', '-At', '-v', 'ON_ERROR_STOP=1', '-c', statement).strip()

    def admin_token(self):
        return self.keycloak('POST', '/realms/master/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': 'admin-cli', 'username': 'admin', 'password': 'psim-dev-keycloak-admin'}, form=True)['access_token']

    def principals(self):
        super().principals()
        self.admin = self.admin_token()
        clients = '/admin/realms/psim/clients'
        self.role_client = self.keycloak('GET', clients + '?clientId=psim', self.admin)[0]['id']
        # Use the same reference mappers as the console, without a password login grant.
        model = json.loads((ROOT / 'deploy/local/config/keycloak/psim-realm.json').read_text())
        mappers = next(client['protocolMappers'] for client in model['clients'] if client['clientId'] == 'psim-services')
        for name in ('operator_site', 'admin_site'):
            client = self.keycloak('GET', clients + '?clientId=' + self.tokens[name]['client'], self.admin)[0]
            client['protocolMappers'] = mappers
            self.keycloak('PUT', clients + '/' + client['id'], self.admin, client)
            user = self.keycloak('GET', clients + '/' + client['id'] + '/service-account-user', self.admin)
            user['attributes'] = {'psim_tenant': [helpers['TENANT']], 'psim_scopes': ['site:' + self.site_id]}
            self.keycloak('PUT', '/admin/realms/psim/users/' + user['id'], self.admin, user)
            self.subjects[name] = user['id']

    def profile(self, name, **changes):
        self.admin = self.admin_token()
        path = '/admin/realms/psim/users/' + self.subjects[name]
        user = self.keycloak('GET', path, self.admin)
        self.keycloak('PUT', path, self.admin, user | changes)

    def prepare(self):
        super().prepare()
        self.basic()
        password_dir = ROOT / '.cache/processing'
        password_dir.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode='w', dir=password_dir, delete=False) as temporary:
            temporary.write('psim-dev-clickhouse')
        os.chmod(temporary.name, 0o400)
        os.replace(temporary.name, password_dir / 'clickhouse.password')
        self.compose('up', '-d', '--wait', '--wait-timeout', '180', 'keeper', 'clickhouse')
        common = {'service': {'instance': 'authorization-fixture'}, 'runtime': {'shards': 1}, 'telemetry': {'otlp_endpoint': 'otel-collector:4317', 'otlp_tls': False, 'sampling_ratio': 0.001}, 'settings': {'allow_plaintext': True, 'listen_port': 8443, 'pg_password_file': '/repo/.cache/event-flow/postgres.secret', 'introspection_secret_file': '/repo/.cache/event-flow/introspection.secret', 'issuer': 'http://keycloak:8080/realms/psim'}}
        extras = {'incident-service': {'legacy_types': True}, 'command-service': {'redis_password_file': '/repo/.cache/event-flow/redis.secret'}, 'event-history': {'history_api': True, 'clickhouse_host': 'clickhouse', 'clickhouse_port': 9000, 'clickhouse_password_file': '/repo/.cache/processing/clickhouse.password'}, 'rule-management': {'tls_certificate_file': '/repo/.cache/event-flow/rule-server.pem', 'tls_private_key_file': '/repo/.cache/event-flow/rule-server.key', 'role': 'management', 'taxonomy_file': '/repo/contracts/taxonomy/v1/taxonomy.yaml', 'clickhouse_port': 9000, 'clickhouse_user': 'psim', 'clickhouse_password_file': '/repo/.cache/processing/clickhouse.password'}}
        for owner in self.owners:
            config = common | {'settings': common['settings'] | extras.get(owner, {})}
            (self.auth_output / (owner + '.json')).write_text(json.dumps(config))
        projection = common | {'settings': common['settings'] | {'reports_enabled': True, 'clickhouse_host': 'clickhouse', 'clickhouse_port': 9000, 'clickhouse_password_file': '/repo/.cache/processing/clickhouse.password', 'redis_host': 'valkey', 'redis_password_file': '/repo/.cache/event-flow/redis.secret'}}
        path = self.auth_output / 'projection.json'
        path.write_text(json.dumps(projection))
        with self.override.open('a') as file:
            file.write('  projection-service:\n    ports: ["127.0.0.1::8443"]\n    command: ' + json.dumps([f'/repo/build/debian13/{self.preset}/services/projection-service/psim-projection-service', '--config', '/repo/' + str(path.relative_to(ROOT))]) + '\n')
        self.compose('up', '-d', '--force-recreate', 'projection-service')
        self.ready('projection-service')
        self.prepare_audit(common)
        self.compose('up', '-d', '--force-recreate', *self.owners)
        for owner in self.owners:
            self.ready(owner)
        self.compose('up', '-d', '--force-recreate', 'api-2')
        self.ready('api-2')
        event = self.event()
        self.incident = event['value']['incidentCreated']['incident']
        self.sql("INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) VALUES('" + helpers['TENANT'] + "','" + self.incident['incidentId'] + "','authz',0,'" + json.dumps(self.incident) + "'::jsonb)")
        self.probe([event])

    def prepare_audit(self, common):
        output = '/repo/' + str(self.auth_output.relative_to(ROOT))
        key, public, ring = self.auth_output / 'signing.pem', self.auth_output / 'public.der', self.auth_output / 'keys.json'
        key.chmod(0o600) if key.exists() else None
        self.env_command('openssl genpkey -algorithm ED25519 -out ' + output + '/signing.pem')
        pem = self.env_command('openssl pkey -in ' + output + '/signing.pem -pubout')
        self.env_command('openssl pkey -in ' + output + '/signing.pem -pubout -outform DER -out ' + output + '/public.der')
        ring.write_text(json.dumps({hashlib.sha256(public.read_bytes()[-32:]).hexdigest(): pem}))
        key.chmod(0o400)
        password = self.auth_output / 'audit.secret'
        password.chmod(0o600) if password.exists() else None
        password.write_text('psim-test-authz-audit')
        password.chmod(0o400)
        self.sql("DO $$ BEGIN IF NOT EXISTS (SELECT FROM pg_roles WHERE rolname='psim_authz_audit') THEN CREATE ROLE psim_authz_audit LOGIN PASSWORD 'psim-test-authz-audit'; END IF; END $$")
        self.sql('GRANT CONNECT ON DATABASE psim TO psim_authz_audit')
        self.env_command(f'PSIM_PG_HOST=postgres PSIM_PG_SSLMODE=disable PSIM_PG_PASSWORD=psim-dev-psim build/debian13/{self.preset}/services/audit-service/psim-audit-tool migrate psim_authz_audit')
        archive = self.auth_output / ('checkpoints-' + self.fixtures)
        archive.mkdir(exist_ok=True)
        config = common | {'settings': common['settings'] | {'pg_user': 'psim_authz_audit', 'pg_password_file': output + '/audit.secret', 'signing_key_file': output + '/signing.pem', 'trusted_keys_file': output + '/keys.json', 'checkpoint_directory': '/repo/' + str(archive.relative_to(ROOT))}}
        (self.auth_output / 'audit-service.json').write_text(json.dumps(config))

    def matrix(self):
        cases = [
            ('resource-catalog', '/v1/locations/' + self.site_id, ROLES),
            ('incident-service', '/v1/incidents/' + self.incident['incidentId'], ('operator', 'supervisor', 'auditor')),
            ('command-service', '/v1/commands', ('operator', 'supervisor', 'auditor')),
            ('response-engine', '/v1/plans', ('operator', 'supervisor', 'admin', 'auditor')),
            ('response-engine', '/v1/escalation-policies', ('operator', 'supervisor', 'admin', 'auditor')),
            ('notification-service', '/v1/me/notifications', ('operator', 'supervisor', 'admin', 'auditor')),
            ('rule-management', '/v1/rules', ('admin', 'integrator', 'supervisor', 'auditor')),
            ('event-history', '/v1/events?from=' + urllib.parse.quote(stamp(-60)) + '&to=' + urllib.parse.quote(stamp()), ('operator', 'supervisor', 'auditor')),
            ('audit-service', '/v1/audit/records?query.siteId=' + self.site_id, ('auditor',)),
            ('projection-service', '/v1/reports/incidents?filter.from=' + urllib.parse.quote(stamp(-60)) + '&filter.to=' + urllib.parse.quote(stamp()), ('manager', 'supervisor', 'auditor')),
            ('projection-service', '/v1/feed/incidents', ('operator', 'supervisor', 'auditor')),
        ]
        checked = 0
        for service, path, allowed in cases:
            for role in ROLES:
                for scope in ('global', 'site', 'zone', 'other', 'empty'):
                    tenant_wide = service == 'response-engine' and path in ('/v1/plans', '/v1/escalation-policies')
                    scoped = not tenant_wide or scope == 'global'
                    if service in ('resource-catalog', 'audit-service'):
                        scoped = scope in ('global', 'site')
                    if service == 'incident-service':
                        scoped = scope in ('global', 'site', 'zone')
                    if service == 'notification-service' or path.startswith('/v1/reports/'):
                        scoped = scope != 'empty'
                    expected = 200 if role in allowed and scoped else 403
                    for owner in (service, 'api-gateway'):
                        result = self.request('GET', path, role=role + '_' + scope, expected=expected, service=owner)
                        if expected == 200 and scope in ('other', 'empty'):
                            field = {'command-service': 'commands', 'rule-management': 'rules', 'event-history': 'events'}.get(service)
                            if service == 'projection-service' and path == '/v1/feed/incidents':
                                field = 'incidents'
                            if field:
                                assert result[field] == [], (service, role, scope)

                        checked += 1
        for service, path in [('resource-catalog', '/v1/locations/' + self.site_id), ('incident-service', '/v1/incidents/' + self.incident['incidentId'])]:
            self.request('GET', path, role='operator_other', expected=403, service=service)
        matrix = {role + '_' + scope: self.token(role + '_' + scope) for role in ROLES for scope in ('global', 'site', 'zone', 'other', 'empty')}
        config = self.auth_output / 'connector.json'
        private_json(config, {'preset': self.preset, 'tokens': matrix})
        self.env_command('python3 -I tools/authz/connector.py /repo/' + str(config.relative_to(ROOT)))
        self.report['connectorCombinations'] = len(matrix)
        for schema in ('catalog', 'rules', 'incident', 'command', 'response', 'notification', 'history', 'reports', 'access', 'ingest', 'audit'):
            self.wait(lambda schema=schema: int(self.sql("SELECT count(*) FROM " + schema + ".psim_outbox WHERE position(convert_to('access.denied','UTF8') in payload)>0")) > 0, 30)
        self.wait(lambda: int(self.sql("SELECT count(*) FROM audit.entries WHERE body->'entry'->>'action'='access.denied'")) > 0, 30)
        self.mark('every service commits denial audit through its outbox and the audit owner ingests the records')
        self.report['boundaryRequests'] = checked
        self.mark('six roles with wildcard, site, zone, unrelated and empty scopes at each REST owner and public Gateway')

    def references(self):
        name = 'operator_site'
        token = self.token(name)
        claims = json.loads(base64.urlsafe_b64decode(token.split('.')[1] + '=='))
        assert claims['psim_scope_set'] == self.subjects[name] and not claims.get('psim_scopes')
        scopes = ['site:' + self.site_id] + ['zone:' + uid() for _ in range(1000)]
        self.profile(name, attributes={'psim_tenant': [helpers['TENANT']], 'psim_scopes': scopes})
        time.sleep(0.6)
        me = self.request('GET', '/v1/me', role=name)
        assert len(me['zoneIds']) == 1000 and self.token(name) == token
        self.profile(name, attributes={'psim_tenant': [helpers['TENANT']], 'psim_scopes': ['zone:' + self.zones[0]['locationId']]})
        time.sleep(0.6)
        self.request('GET', '/v1/locations/' + self.zones[0]['locationId'], role=name)
        self.request('GET', '/v1/locations/' + self.zones[1]['locationId'], role=name, expected=403)
        result = self.request('GET', '/v1/locations?kind=LOCATION_KIND_ZONE', role=name)
        assert [row['locationId'] for row in result['locations']] == [self.zones[0]['locationId']]
        self.mark('unchanged compact JWT resolves 1001 live scopes; zone reads and list filtering agree')
        group_path = '/admin/realms/psim/groups'
        group_name = 'authorization-' + self.fixtures
        self.keycloak('POST', group_path, self.admin, {'name': group_name, 'attributes': {'psim_scopes': ['site:' + self.site_id]}})
        parent = next(group for group in self.keycloak('GET', group_path, self.admin) if group['name'] == group_name)
        self.keycloak('POST', group_path + '/' + parent['id'] + '/children', self.admin, {'name': 'operators'})
        child = self.keycloak('GET', group_path + '/' + parent['id'] + '/children', self.admin)[0]
        membership = '/admin/realms/psim/users/' + self.subjects[name] + '/groups/' + child['id']
        self.keycloak('PUT', membership, self.admin)
        self.profile(name, attributes={'psim_tenant': [helpers['TENANT']]})
        time.sleep(0.6)
        assert self.site_id in self.request('GET', '/v1/me', role=name)['siteIds']
        self.keycloak('DELETE', membership, self.admin)
        self.mark('group and ancestor scope attributes are reflected by the same JWT')

        self.profile(name, attributes={'psim_tenant': [helpers['TENANT']], 'psim_scopes': ['site:' + self.site_id]})
        time.sleep(0.6)
        config = {'token': token, 'subject': self.subjects[name], 'site': self.site_id, 'tenant': helpers['TENANT'], 'preset': self.preset, 'output': '/repo/' + str(self.auth_output.relative_to(ROOT))}
        source = self.auth_output / 'revocation.json'
        private_json(source, config)
        self.env_command('python3 -I tools/authz/peer.py /repo/' + str(source.relative_to(ROOT)))
        self.profile(name, enabled=True)
        self.mark('idle and subscribed WSS connections close before JWT expiry after scope change or account disable')
        self.profile(name, enabled=False)
        try:
            time.sleep(0.6)
            self.request('GET', '/v1/me', role=name, expected=401)
            self.request('GET', '/v1/locations/' + self.site_id, role=name, expected=401, service='resource-catalog')
        finally:
            self.profile(name, enabled=True)
        time.sleep(0.6)
        self.request('GET', '/v1/me', role=name)
        self.mark('account disable rejects the existing JWT at Gateway and direct REST owner')
        role_path = '/admin/realms/psim/users/' + self.subjects[name] + '/role-mappings/clients/' + self.role_client
        role = self.keycloak('GET', '/admin/realms/psim/clients/' + self.role_client + '/roles/operator', self.admin)
        self.keycloak('DELETE', role_path, self.admin, [role])
        try:
            time.sleep(0.6)
            self.request('GET', '/v1/me', role=name, expected=401)
        finally:
            self.keycloak('POST', role_path, self.admin, [role])
        time.sleep(0.6)
        self.mark('removing a role invalidates previously issued access claims')
        self.profile_access()
        self.outage(config)

    def profile_access(self):
        self.admin = self.admin_token()
        descriptor = self.keycloak('GET', '/admin/realms/psim/users/profile', self.admin)
        for name in ('psim_tenant', 'psim_scopes'):
            field = next(item for item in descriptor['attributes'] if item['name'] == name)
            assert field['permissions'] == {'view': ['admin'], 'edit': ['admin']}
        clients = '/admin/realms/psim/clients'
        client_id = 'profile-check-' + self.fixtures
        self.keycloak('POST', clients, self.admin, {'clientId': client_id, 'enabled': True, 'publicClient': False, 'secret': 'psim-test-profile', 'standardFlowEnabled': False, 'directAccessGrantsEnabled': True, 'defaultClientScopes': ['roles', 'profile', 'email'], 'protocolMappers': [{'name': 'account-audience', 'protocol': 'openid-connect', 'protocolMapper': 'oidc-audience-mapper', 'config': {'included.client.audience': 'account', 'access.token.claim': 'true'}}]})
        client = self.keycloak('GET', clients + '?clientId=' + client_id, self.admin)[0]['id']
        users = '/admin/realms/psim/users'
        username = 'profile-' + self.fixtures
        self.keycloak('POST', users, self.admin, {'username': username, 'enabled': True, 'emailVerified': True, 'email': username + '@example.test', 'firstName': 'Profile', 'lastName': 'Check', 'attributes': {'psim_tenant': [helpers['TENANT']], 'psim_scopes': ['site:' + self.site_id]}, 'credentials': [{'type': 'password', 'value': 'psim-test-profile-password', 'temporary': False}]})
        user = self.keycloak('GET', users + '?exact=true&username=' + username, self.admin)[0]
        path = users + '/' + user['id']
        try:
            account = self.keycloak('GET', clients + '?clientId=account', self.admin)[0]['id']
            roles = [self.keycloak('GET', clients + '/' + account + '/roles/' + role, self.admin) for role in ('manage-account', 'view-profile')]
            self.keycloak('POST', path + '/role-mappings/clients/' + account, self.admin, roles)
            bearer = self.keycloak('POST', '/realms/psim/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': client_id, 'client_secret': 'psim-test-profile', 'username': username, 'password': 'psim-test-profile-password'}, form=True)['access_token']
            own = self.keycloak('GET', '/realms/psim/account/', bearer)
            assert own['username'] == username
            before = self.keycloak('GET', path, self.admin)
            try:
                self.keycloak('POST', '/realms/psim/account/', bearer, own | {'attributes': {'psim_tenant': [helpers['OTHER_TENANT']], 'psim_scopes': ['*']}})
            except urllib.error.HTTPError as error:
                assert error.code in (400, 403), error.code
            after = self.keycloak('GET', path, self.admin)
            assert after.get('attributes') == before.get('attributes'), 'self-service profile changed access grants'
        finally:
            self.keycloak('DELETE', path, self.admin)
            self.keycloak('DELETE', clients + '/' + client, self.admin)
        self.mark('an authenticated user can read the account profile and cannot change tenant or grant wildcard scope')

    def outage(self, config):
        ready = self.auth_output / 'outage-ready'
        ready.unlink(missing_ok=True)
        source = self.auth_output / 'outage.json'
        private_json(source, config | {'mode': 'outage'})
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            peer = executor.submit(self.env_command, 'python3 -I tools/authz/peer.py /repo/' + str(source.relative_to(ROOT)))
            self.wait(ready.exists, 30)
            self.compose('pause', 'keycloak')
            try:
                time.sleep(1.2)
                self.request('GET', '/v1/me', role='operator_site', expected=503)
                self.request('GET', '/v1/locations/' + self.site_id, role='operator_site', expected=503, service='resource-catalog')
                peer.result(timeout=10)
            finally:
                self.compose('unpause', 'keycloak')
        self.mark('IdP outage rejects REST and closes an idle WSS connection without stale access')


    def cleanup(self):
        for infrastructure in ('registry', 'keycloak', 'postgres'):
            (self.auth_output / (infrastructure + '.log')).write_text(self.compose('logs', '--no-color', '--tail', '200', infrastructure))
            identity = self.compose('ps', '-aq', infrastructure).strip()
            if identity:
                state = self.run(['docker', 'inspect', '--format', '{{json .State}}', identity])
                (self.auth_output / (infrastructure + '-state.json')).write_text(state)
        self.compose('stop', '-t', '30', *self.owners)
        for owner in self.owners:
            logs = self.compose('logs', '--no-color', owner)
            (self.auth_output / (owner + '.log')).write_text(logs)
            if self.report['passed']:
                assert not re.search(r'AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:', logs), owner
        super().cleanup()
        for name in ('connector.json', 'revocation.json', 'outage.json'):
            (self.auth_output / name).unlink(missing_ok=True)


def main():
    check = Check(*sys.argv[1:])
    try:
        check.prepare()
        check.matrix()
        check.references()
        check.report['passed'] = True
    finally:
        try:
            check.cleanup()
        finally:
            (check.auth_output / 'result.json').write_text(json.dumps(check.report, indent=2) + '\n')
    print(json.dumps(check.report, indent=2))


if __name__ == '__main__':
    main()
