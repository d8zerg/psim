"""Create a fresh scoped operator for the owned synthetic load case."""
import json
from pathlib import Path
import secrets
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'deploy'))
from scenario import API, CASE


def run():
    api = API('https://localhost:8443')
    state = json.loads((CASE / 'scenario.json').read_text())
    tenant = api.request('GET', '/v1/me')['tenantId']
    username, password = 'load-' + uuid.uuid4().hex, secrets.token_urlsafe(24)
    base = '/identity/admin/realms/psim'
    api.request('POST', base + '/users', {'username': username, 'enabled': True, 'emailVerified': True,
        'firstName': 'Load', 'lastName': 'Operator', 'email': username + '@example.test',
        'requiredActions': ['CONFIGURE_TOTP'], 'attributes': {'psim_tenant': [tenant], 'psim_scopes': [state['site']]},
        'credentials': [{'type': 'password', 'value': password, 'temporary': False}]})
    user = api.request('GET', base + '/users?exact=true&username=' + username)[0]['id']
    client = api.request('GET', base + '/clients?clientId=psim')[0]['id']
    role = api.request('GET', base + '/clients/' + client + '/roles/operator')
    api.request('POST', base + '/users/' + user + '/role-mappings/clients/' + client, [role])
    path = CASE / 'load-browser.json'
    path.write_text(json.dumps({'username': username, 'password': password, 'origin': api.origin}))
    path.chmod(0o600)


if __name__ == '__main__':
    run()
