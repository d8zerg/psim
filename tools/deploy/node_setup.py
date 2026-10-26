"""Initialize shared schemas and the private floor-plan bucket with installation credentials."""
import argparse
import datetime
import hashlib
import hmac
import json
from pathlib import Path
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request

import registry

MATERIAL = Path('/operator')


def context(identity=True):
    value = ssl.create_default_context(cafile=str(MATERIAL / 'ca.pem'))
    if identity: value.load_cert_chain(MATERIAL / 'identity.pem', MATERIAL / 'identity.key')
    return value


def schemas():
    client = registry.Registry('https://registry:18443/apis/ccompat/v7', context=context())
    topics, subjects = registry.register_tree(client, str(Path(__file__).parent / 'tree'))
    print(f'Registered {topics} topic schemas and {len(subjects)} subjects over mTLS')


def core():
    import psycopg2
    profile = json.loads(Path('/config/profile.json').read_text())
    with psycopg2.connect(host='postgres', port=15432, dbname='psim', user='postgres',
            password=(MATERIAL / 'postgres.secret').read_text().strip(), connect_timeout=3,
            sslmode='verify-full', sslrootcert=str(MATERIAL / 'ca.pem'),
            sslcert=str(MATERIAL / 'postgres.pem'), sslkey=str(MATERIAL / 'postgres.key')) as connection:
        with connection.cursor() as cursor:
            cursor.execute('SELECT pg_is_in_recovery()')
            if cursor.fetchone()[0]: raise RuntimeError('PostgreSQL endpoint is a replica')
            if profile['topology'] == 'cluster':
                cursor.execute("SELECT count(*),bool_or(sync_state='sync') FROM pg_stat_replication WHERE state='streaming'")
                replicas, synchronous = cursor.fetchone()
                if replicas != 2 or not synchronous: raise RuntimeError('PostgreSQL replicas are not ready')
    print('PostgreSQL primary and replication ready')


def identity():
    profile = json.loads(Path('/config/profile.json').read_text())
    url = 'https://keycloak:18444/identity/realms/psim/.well-known/openid-configuration'
    with urllib.request.urlopen(url, context=context(), timeout=10) as response:
        value = json.load(response)
    expected = 'https://' + profile['public_name'] + ((':' + str(profile['public_port'])) if profile['public_port'] != 443 else '') + '/identity/realms/psim'
    if value['issuer'] != expected: raise RuntimeError('unexpected identity issuer')
    identity_profile()
    print('Identity realm and issuer ready')


def identity_profile():
    base = 'https://keycloak:18444/identity'
    body = urllib.parse.urlencode({'grant_type': 'client_credentials', 'client_id': 'deployment-admin',
                                  'client_secret': (MATERIAL / 'identity.secret').read_text().strip()}).encode()
    with urllib.request.urlopen(urllib.request.Request(base + '/realms/psim/protocol/openid-connect/token', data=body),
                                context=context(), timeout=10) as response:
        token = json.load(response)['access_token']
    url = base + '/admin/realms/psim/users/profile'
    headers = {'Authorization': 'Bearer ' + token, 'Content-Type': 'application/json'}
    with urllib.request.urlopen(urllib.request.Request(url, headers=headers), context=context(), timeout=10) as response:
        profile = json.load(response)
    names = {'psim_tenant', 'psim_scopes', 'psim_scope_set'}
    profile['attributes'] = [attribute for attribute in profile['attributes'] if attribute['name'] not in names]
    for name in sorted(names):
        profile['attributes'].append({'name': name, 'multivalued': name == 'psim_scopes',
            'permissions': {'view': ['admin'], 'edit': ['admin']}, 'validations': {'length': {'min': 1, 'max': 256}}})
    profile.pop('unmanagedAttributePolicy', None)
    with urllib.request.urlopen(urllib.request.Request(url, data=json.dumps(profile).encode(), headers=headers, method='PUT'),
                                context=context(), timeout=10) as response:
        if response.status not in (200, 204): raise RuntimeError('identity user profile configuration rejected')
    print('Identity authorization attributes restricted to administrative editing')


def identity_journals():
    base = 'https://keycloak:18444/identity'
    body = urllib.parse.urlencode({'grant_type': 'client_credentials', 'client_id': 'deployment-admin',
                                  'client_secret': (MATERIAL / 'identity.secret').read_text().strip()}).encode()
    request = urllib.request.Request(base + '/realms/psim/protocol/openid-connect/token', data=body,
                                     headers={'Content-Type': 'application/x-www-form-urlencoded'})
    with urllib.request.urlopen(request, context=context(), timeout=10) as response:
        token = json.load(response)['access_token']
    settings = {'eventsEnabled': True, 'eventsExpiration': 604800,
                'enabledEventTypes': ['LOGIN', 'LOGIN_ERROR', 'LOGOUT', 'REFRESH_TOKEN_ERROR'],
                'adminEventsEnabled': True, 'adminEventsDetailsEnabled': False}
    request = urllib.request.Request(base + '/admin/realms/psim/events/config', data=json.dumps(settings).encode(),
        headers={'Content-Type': 'application/json', 'Authorization': 'Bearer ' + token}, method='PUT')
    with urllib.request.urlopen(request, context=context(), timeout=10) as response:
        if response.status != 204: raise RuntimeError('identity journal configuration rejected')
    print('Identity security journals configured without credential representations')


def identity_audit():
    base = 'https://keycloak:18444/identity'
    body = urllib.parse.urlencode({'grant_type': 'client_credentials', 'client_id': 'audit-service-directory',
                                  'client_secret': (MATERIAL / 'directory.secret').read_text().strip()}).encode()
    request = urllib.request.Request(base + '/realms/psim/protocol/openid-connect/token', data=body,
                                     headers={'Content-Type': 'application/x-www-form-urlencoded'})
    with urllib.request.urlopen(request, context=context(False), timeout=10) as response:
        token = json.load(response)['access_token']
    cutoff = int(datetime.datetime.now(datetime.timezone.utc).timestamp() * 1000) - 2000
    query = urllib.parse.urlencode({'dateFrom': 0, 'dateTo': cutoff, 'direction': 'asc', 'max': 1, 'first': 0})
    for path in ['events/config', 'events?' + query, 'admin-events?' + query]:
        request = urllib.request.Request(base + '/admin/realms/psim/' + path, headers={'Authorization': 'Bearer ' + token})
        with urllib.request.urlopen(request, context=context(False), timeout=10) as response:
            value = json.load(response)
        if isinstance(value, list): print(path + ': ' + str(len(value)) + ' records; fields=' + ','.join(sorted(value[0]) if value else []))
        else: print(path + ': ' + json.dumps({key: value.get(key) for key in ['eventsEnabled', 'adminEventsEnabled', 'adminEventsDetailsEnabled']}))


def applications():
    from model import Profile, SERVICES
    profile = Profile.load('/config/profile.json')
    for node in profile.select('application'):
        for index, name in enumerate(SERVICES):
            try:
                with urllib.request.urlopen(f'http://{node.address}:{19100 + index}/health/ready', timeout=3) as response:
                    if response.status != 200: raise RuntimeError('unexpected health status')
            except urllib.error.HTTPError as error:
                raise RuntimeError('application is not ready: ' + node.name + '/' + name + ', HTTP ' + str(error.code) + ' ' + error.read(1024).decode(errors='replace')) from None
            except (urllib.error.URLError, OSError):
                raise RuntimeError('application health unavailable: ' + node.name + '/' + name) from None
    print('Application replicas ready')


def schema_reader():
    client = registry.Registry('https://registry:18443/apis/ccompat/v7', context=context())
    status, _ = client.call('GET', '/subjects')
    if status != 200: raise RuntimeError('schema reader cannot read subjects: HTTP ' + str(status))
    status, _ = client.call('PUT', '/config/psim.ingest.raw.v1-value', {'compatibility': 'BACKWARD_TRANSITIVE'})
    if status != 403: raise RuntimeError('schema reader write was not denied: HTTP ' + str(status))
    print('Schema reader can read; schema mutations denied')


def bucket():
    host = 's3:18446'
    path = '/psim-plans'
    now = datetime.datetime.now(datetime.timezone.utc)
    day, timestamp = now.strftime('%Y%m%d'), now.strftime('%Y%m%dT%H%M%SZ')
    digest = hashlib.sha256(b'').hexdigest()
    headers = {'host': host, 'x-amz-content-sha256': digest, 'x-amz-date': timestamp}
    signed = ';'.join(sorted(headers))
    canonical = '\n'.join(['PUT', path, '', ''.join(name + ':' + headers[name] + '\n' for name in sorted(headers)), signed, digest])
    scope = day + '/us-east-1/s3/aws4_request'
    string = '\n'.join(['AWS4-HMAC-SHA256', timestamp, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    key = ('AWS4' + (MATERIAL / 's3.secret').read_text().strip()).encode()
    for value in (day, 'us-east-1', 's3', 'aws4_request'):
        key = hmac.new(key, value.encode(), hashlib.sha256).digest()
    access = (MATERIAL / 's3.access').read_text().strip()
    headers['Authorization'] = f'AWS4-HMAC-SHA256 Credential={access}/{scope}, SignedHeaders={signed}, Signature=' + hmac.new(key, string.encode(), hashlib.sha256).hexdigest()
    request = urllib.request.Request('https://' + host + path, data=b'', headers=headers, method='PUT')
    try:
        with urllib.request.urlopen(request, context=context(), timeout=30) as response:
            if response.status != 200: raise RuntimeError('unexpected bucket creation response')
    except urllib.error.HTTPError as error:
        if error.code != 409 or b'<Code>BucketAlreadyOwnedByYou</Code>' not in error.read():
            raise RuntimeError('bucket creation failed: HTTP ' + str(error.code)) from None
    try:
        with urllib.request.urlopen('https://' + host + path, context=context(False), timeout=30):
            raise RuntimeError('floor-plan bucket permits anonymous access')
    except urllib.error.HTTPError as error:
        if error.code != 403: raise RuntimeError('unexpected anonymous bucket response: HTTP ' + str(error.code)) from None
    print('Floor-plan bucket configured; anonymous access denied')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    actions = {'schemas': schemas, 'bucket': bucket, 'core': core, 'identity': identity, 'identity-journals': identity_journals, 'identity-audit': identity_audit,
               'applications': applications, 'schema-reader': schema_reader}
    parser.add_argument('action', choices=actions)
    arguments = parser.parse_args()
    try:
        actions[arguments.action]()
    except urllib.error.HTTPError as error:
        print('deployment operation failed: HTTP ' + str(error.code), file=sys.stderr)
        raise SystemExit(1)
    except Exception as error:
        print('deployment operation failed: ' + (str(error) if isinstance(error, RuntimeError) else type(error).__name__), file=sys.stderr)
        raise SystemExit(1)
