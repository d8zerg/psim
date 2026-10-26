"""Generate service-specific permissions from the deployment's fresh secret inventory."""
import copy
import hashlib
import json
from pathlib import Path
import secrets
from xml.sax.saxutils import escape

from model import SERVICES


def sql_name(value):
    return '"' + value.replace('"', '""') + '"'


def sql_value(value):
    return "'" + value.replace("'", "''") + "'"


def postgres(profile, secret):
    roles = {name.replace('-', '_'): secret(name + '/postgres.secret') for name in SERVICES}
    roles.update({name: secret(name + '/postgres.secret') for name in ('keycloak', 'registry', 'seaweed')})
    roles['monitoring'] = secret('monitoring/postgres.secret')
    statements = []
    for role, password in roles.items():
        statements += [f'SELECT {sql_value("CREATE ROLE " + sql_name(role))} WHERE NOT EXISTS (SELECT FROM pg_roles WHERE rolname={sql_value(role)}) \\gexec',
                       f'ALTER ROLE {sql_name(role)} WITH LOGIN NOSUPERUSER NOCREATEDB NOCREATEROLE NOREPLICATION NOBYPASSRLS PASSWORD {sql_value(password)};']
    for database, owner in [('psim', 'postgres'), ('keycloak', 'keycloak'), ('registry', 'registry'), ('seaweed', 'seaweed')]:
        statements += [f'SELECT {sql_value("CREATE DATABASE " + sql_name(database) + " OWNER " + sql_name(owner))} WHERE NOT EXISTS (SELECT FROM pg_database WHERE datname={sql_value(database)}) \\gexec',
                       f'REVOKE ALL ON DATABASE {sql_name(database)} FROM PUBLIC;']
    statements.append('GRANT CONNECT ON DATABASE psim TO ' + ','.join(sql_name(name) for name in roles if name not in ('keycloak', 'registry', 'seaweed')) + ';')
    statements += ['\\connect psim', 'REVOKE ALL ON SCHEMA public FROM PUBLIC;']
    for name, (_, _, schema) in SERVICES.items():
        if not schema or name == 'audit-service':
            continue
        for item in [schema, *({'response-engine': ['escalation'], 'projection-service': ['console']}.get(name, []))]:
            statements.append(f'CREATE SCHEMA IF NOT EXISTS {sql_name(item)} AUTHORIZATION {sql_name(name.replace("-", "_"))};')
    statements += ['CREATE SCHEMA IF NOT EXISTS psim_clickhouse;',
        'CREATE TABLE IF NOT EXISTS psim_clickhouse.schema_migrations(target text NOT NULL,version bigint NOT NULL,phase text NOT NULL,name text NOT NULL,checksum text NOT NULL,step bigint NOT NULL DEFAULT 0,minimum_reader bigint NOT NULL DEFAULT 0,PRIMARY KEY(target,version,phase));',
        'ALTER TABLE psim_clickhouse.schema_migrations ADD COLUMN IF NOT EXISTS minimum_reader bigint NOT NULL DEFAULT 0;',
        'CREATE TABLE IF NOT EXISTS psim_clickhouse.pending_batches(loader text NOT NULL,topic text NOT NULL,partition integer NOT NULL,first_offset bigint NOT NULL,next_offset bigint NOT NULL,token text NOT NULL,payload bytea NOT NULL,applied boolean NOT NULL DEFAULT false,PRIMARY KEY(loader,topic,partition),CHECK(first_offset>=0 AND next_offset>first_offset));']
    for service, target, loaders in [('event-history', 'event_history_psim', ['psim-event-history/events_v1', 'psim-history-facts/history_facts_v1']),
                                      ('projection-service', 'incident_reports_psim', ['psim-projection-service/incident_facts_v1'])]:
        role = sql_name(service.replace('-', '_'))
        statements += [f'GRANT USAGE,CREATE ON SCHEMA psim_clickhouse TO {role};',
                       f'GRANT SELECT,INSERT,UPDATE,DELETE ON ALL TABLES IN SCHEMA psim_clickhouse TO {role};']
        for table, condition in [('schema_migrations', 'target=' + sql_value(target)), ('pending_batches', 'loader IN (' + ','.join(map(sql_value, loaders)) + ')')]:
            policy = sql_name(service.replace('-', '_'))
            statements += [f'ALTER TABLE psim_clickhouse.{table} ENABLE ROW LEVEL SECURITY;',
                           f'ALTER TABLE psim_clickhouse.{table} FORCE ROW LEVEL SECURITY;',
                           f'DROP POLICY IF EXISTS {policy} ON psim_clickhouse.{table};',
                           f'CREATE POLICY {policy} ON psim_clickhouse.{table} TO {role} USING ({condition}) WITH CHECK ({condition});']
    statements += ['GRANT pg_monitor TO monitoring;', (Path(__file__).resolve().parents[2] / 'deploy/observability/business.sql').read_text()]
    return '\n'.join(statements) + '\n'


def password_hash(value):
    return '#' + hashlib.sha256(value.encode()).hexdigest()


def valkey(profile, secret):
    users = ['user default off', 'user deployment-admin on ' + password_hash(secret('deployment-admin/valkey.secret')) + ' ~* &* +@all',
             'user monitoring on ' + password_hash(secret('monitoring/valkey.secret')) + ' +ping +info']
    for name, commands, pattern in [
        ('connector-gateway', '+get +set +del +pexpire +pttl +incrby +eval', '~psim:17:connector-gateway*'),
        ('projection-service', '+get +set +del', '~psim:18:projection-service*'),
        ('command-service', '+get', '%R~psim:17:connector-gateway*7:session*'),
        ('api-gateway', '+eval +time +hmget +hset +pexpire', '~psim:11:api-gateway*6:bucket*'),
    ]:
        users.append(f'user {name} on {password_hash(secret(name + "/valkey.secret"))} {pattern} +ping +role +client|setname {commands}')
    if profile.topology == 'cluster':
        users += ['user replicator on ' + password_hash(secret('valkey/replication.secret')) + ' +ping +psync +replconf',
                  'user sentinel-monitor on ' + password_hash(secret('sentinel/monitor.secret')) + ' &__sentinel__:hello +ping +info +role +publish +subscribe +multi +exec +slaveof +replicaof +config|rewrite +client|setname +client|kill +script|kill']
    return '\n'.join(users) + '\n'


def sentinel(profile, secret):
    users = ['user default off', 'user deployment-admin on ' + password_hash(secret('deployment-admin/sentinel.secret')) + ' ~* &* +@all',
             'user sentinel-peer on ' + password_hash(secret('sentinel/peer.secret')) + ' +ping +client|setname +sentinel|is-master-down-by-addr']
    users.append('user monitoring on ' + password_hash(secret('monitoring/sentinel.secret')) + ' +ping +sentinel|masters')
    for name in ('connector-gateway', 'command-service', 'projection-service', 'api-gateway'):
        users.append(f'user {name} on {password_hash(secret(name + "/sentinel.secret"))} +ping +client|setname +sentinel|get-master-addr-by-name')
    return '\n'.join(users) + '\n'


def clickhouse(profile, secret):
    users = []
    def user(name, password, grants=None, reader=False, administrator=False):
        digest = hashlib.sha256(password.encode()).hexdigest()
        body = f'<{name}><password_sha256_hex>{digest}</password_sha256_hex><networks><ip>0.0.0.0/0</ip></networks><profile>{"reader" if reader else "default"}</profile><quota>default</quota>'
        if administrator:
            body += '<access_management>1</access_management>'
        if grants:
            body += '<grants>' + ''.join('<query>' + escape('GRANT ' + grant) + '</query>' for grant in grants) + '</grants>'
        users.append(body + '</' + name + '>')
    user('default', secrets.token_urlsafe(48), ['USAGE ON *.*'])
    user('deployment-admin', secret('deployment-admin/clickhouse.secret'), administrator=True)
    user('monitoring', secret('monitoring/clickhouse.secret'), ['SELECT ON system.parts', 'SELECT ON system.replicas'], reader=True)
    user('cluster-peer', secret('clickhouse/peer.secret'), ['ALL ON default.*', 'CLUSTER ON *.*'])
    views = {'event-history': ['signal_facts', 'event_signals', 'incident_signals'],
             'projection-service': ['incident_report_mv', 'incident_report_current']}
    for name, tables in [('event-history', ['events', 'history_facts', 'retained_events']), ('projection-service', ['incident_facts', 'incident_report_states'])]:
        user(name, secret(name + '/clickhouse.secret'), ['CLUSTER ON *.*'] + ['SELECT,INSERT,CREATE TABLE,ALTER TABLE ON default.' + table for table in tables]
             + ['SELECT,CREATE VIEW,ALTER VIEW ON default.' + view for view in views[name]])
        user(name + '-reader', secret(name + '/clickhouse-read.secret'), ['SELECT ON default.' + table for table in tables + views[name]], reader=True)
    user('rule-management', secret('rule-management/clickhouse.secret'), ['SELECT ON default.' + table for table in ['events', 'history_facts', 'retained_events'] + views['event-history']], reader=True)
    constraints = ''.join('<' + name + '><min>' + str(low) + '</min><max>' + str(high)
                          + '</max><changeable_in_readonly/></' + name + '>'
                          for name, low, high in [('max_execution_time', 1, 30), ('max_result_rows', 1, 1000000),
                              ('max_result_bytes', 1, 67108864), ('max_threads', 1, 2), ('max_block_size', 1, 65536)])
    return '<clickhouse><profiles><default><max_memory_usage>536870912</max_memory_usage><max_threads>2</max_threads></default><reader><readonly>1</readonly><max_memory_usage>268435456</max_memory_usage><max_threads>2</max_threads><max_execution_time>30</max_execution_time><max_result_rows>1000000</max_result_rows><max_result_bytes>67108864</max_result_bytes><constraints>' + constraints + '</constraints></reader></profiles><quotas><default><interval><duration>3600</duration><queries>0</queries><errors>0</errors><execution_time>0</execution_time><read_rows>0</read_rows><result_rows>0</result_rows></interval></default></quotas><users>' + ''.join(users) + '</users></clickhouse>'


def s3(profile, secret):
    return json.dumps({'identities': [
        {'name': 'catalog', 'credentials': [{'accessKey': secret('resource-catalog/s3.access'), 'secretKey': secret('resource-catalog/s3.secret')}], 'actions': ['Read:psim-plans', 'Write:psim-plans', 'List:psim-plans']},
        {'name': 'deployment-admin', 'credentials': [{'accessKey': secret('deployment-admin/s3.access'), 'secretKey': secret('deployment-admin/s3.secret')}], 'actions': ['Admin:psim-plans', 'Read:psim-plans', 'Write:psim-plans', 'List:psim-plans']},
    ]}, indent=2)


def realm(profile, secret, tenant):
    root = Path(__file__).resolve().parents[2]
    template = json.loads((root / 'deploy/local/config/keycloak/psim-realm.json').read_text())
    template['displayName'] = 'Open-Source PSIM Platform'
    template['sslRequired'] = 'all'
    template.update({'eventsEnabled': True, 'eventsExpiration': 604800,
                     'enabledEventTypes': ['LOGIN', 'LOGIN_ERROR', 'LOGOUT', 'REFRESH_TOKEN_ERROR'],
                     'adminEventsEnabled': True, 'adminEventsDetailsEnabled': False})
    template['users'] = []
    template['clients'] = [copy.deepcopy(client) for client in template['clients'] if client['clientId'] in ('psim', 'psim-console')]
    resource, console = template['clients']
    resource.pop('secret', None); resource['bearerOnly'] = True
    console['redirectUris'] = [profile.origin + '/*']; console['webOrigins'] = [profile.origin]
    console.setdefault('attributes', {})['post.logout.redirect.uris'] = profile.origin + '/*'
    # Each resource server authenticates introspection with its own client.
    for name in SERVICES:
        console.setdefault('protocolMappers', []).append({'name': 'audience-' + name, 'protocol': 'openid-connect',
            'protocolMapper': 'oidc-audience-mapper', 'config': {'included.client.audience': name,
            'access.token.claim': 'true', 'introspection.token.claim': 'true', 'id.token.claim': 'false'}})
    mappers = copy.deepcopy(console.get('protocolMappers', []))
    for name in SERVICES:
        template['clients'].append({'clientId': name, 'enabled': True, 'publicClient': False, 'standardFlowEnabled': False,
            'directAccessGrantsEnabled': False, 'serviceAccountsEnabled': False, 'secret': secret(name + '/identity.secret')})
    for name in ('incident-service', 'response-engine', 'notification-service', 'audit-service', 'deployment-admin'):
        directory = name != 'deployment-admin'
        client_id = name + '-directory' if directory else name
        password = secret(name + ('/directory.secret' if directory else '/identity.secret'))
        template['clients'].append({'clientId': client_id, 'enabled': True, 'publicClient': False, 'standardFlowEnabled': False,
            'directAccessGrantsEnabled': False, 'serviceAccountsEnabled': True, 'secret': password, 'protocolMappers': copy.deepcopy(mappers)})
        roles = ['view-users', 'query-users', 'view-realm', 'view-events'] if directory else ['realm-admin']
        template['users'].append({'username': 'service-account-' + client_id, 'enabled': True, 'serviceAccountClientId': client_id,
            'attributes': {'psim_tenant': [tenant], 'psim_scopes': ['*'], 'psim_scope_set': ['initial']},
            'clientRoles': {'realm-management': roles, **({'psim': ['admin', 'integrator', 'operator', 'supervisor', 'auditor']} if not directory else {})}})
    template['roles']['realm'].extend({'name': name, 'composite': True, 'composites': {'realm': ['mfa-required']}}
        for name in ('psim-observability-admin', 'psim-observability-viewer'))
    template['clients'].append({'clientId': 'psim-grafana', 'enabled': True, 'publicClient': False,
        'standardFlowEnabled': True, 'directAccessGrantsEnabled': False, 'serviceAccountsEnabled': False,
        'fullScopeAllowed': True,
        'secret': secret('grafana/identity.secret'), 'redirectUris': [profile.origin + '/monitoring/login/generic_oauth'],
        'webOrigins': [profile.origin], 'attributes': {'pkce.code.challenge.method': 'S256'},
        'defaultClientScopes': ['web-origins', 'profile', 'roles', 'email'],
        'protocolMappers': [{'name': 'observability-roles', 'protocol': 'openid-connect',
            'protocolMapper': 'oidc-usermodel-realm-role-mapper', 'config': {'claim.name': 'psim_observability_roles',
                'jsonType.label': 'String', 'multivalued': 'true', 'access.token.claim': 'true',
                'id.token.claim': 'true', 'userinfo.token.claim': 'true'}}]})
    return json.dumps(template, indent=2)
