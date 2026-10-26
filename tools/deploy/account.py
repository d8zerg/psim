"""Create an initial human account without exposing its temporary password in arguments or environment."""
import argparse
import getpass
import json
from pathlib import Path
import re
import subprocess
import sys
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import require


def create(origin):
    from scenario import API
    api = API(origin); data = json.load(sys.stdin)
    tenant = api.request('GET', '/v1/me')['tenantId']
    base = '/identity/admin/realms/psim'
    query = '?exact=true&username=' + urllib.parse.quote(data['username'])
    require(not api.request('GET', base + '/users' + query), 'account already exists; refuse to alter its credentials or permissions')
    api.request('POST', base + '/users', {'username': data['username'], 'enabled': True,
        'firstName': data['username'], 'lastName': 'PSIM',
        'attributes': {'psim_tenant': [tenant], 'psim_scopes': ['*'], 'psim_scope_set': ['initial']},
        'requiredActions': ['UPDATE_PASSWORD', 'CONFIGURE_TOTP'],
        'credentials': [{'type': 'password', 'value': data['password'], 'temporary': True}]})
    users = api.request('GET', base + '/users' + query)
    require(len(users) == 1, 'initial account could not be located')
    user = api.request('GET', base + '/users/' + users[0]['id'])
    require(user.get('attributes', {}).get('psim_tenant') == [tenant], 'initial account attributes were not retained')
    clients = api.request('GET', base + '/clients?clientId=psim')
    require(len(clients) == 1, 'PSIM resource client is missing')
    client = clients[0]['id']
    roles = [api.request('GET', base + '/clients/' + client + '/roles/' + role) for role in data['roles']]
    api.request('POST', base + '/users/' + users[0]['id'] + '/role-mappings/clients/' + client, roles)
    print('Initial account created; first sign-in requires a new password and TOTP enrollment')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle'); parser.add_argument('--private'); parser.add_argument('--username')
    parser.add_argument('--roles', nargs='+', choices=['admin', 'integrator', 'operator', 'supervisor', 'auditor'], default=['admin'])
    parser.add_argument('--in-container', action='store_true', help=argparse.SUPPRESS); parser.add_argument('--origin')
    args = parser.parse_args()
    if args.in_container:
        create(args.origin); return
    from material import verify_bundle
    import local
    require(args.bundle and args.private and args.username, 'bundle, private material and username are required')
    require(re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._-]{2,63}', args.username), 'invalid initial account name')
    profile = verify_bundle(args.bundle); material = Path(args.private).resolve()
    password = getpass.getpass('Temporary password (at least 16 characters): ')
    require(len(password) >= 16 and password == getpass.getpass('Repeat temporary password: '), 'temporary password is too short or differs')
    image = 'psim-platform/postgres:' + profile.version
    command = ['docker', 'run', '--rm', '--interactive', '--network', 'host', '--read-only', '--cap-drop', 'ALL',
        '--security-opt', 'no-new-privileges:true', '--env', 'LD_PRELOAD=', '--user', str(profile.uid('deployment-admin')),
        '--tmpfs', '/tmp:rw,nosuid,size=32m,mode=1777',
        '--mount', 'type=bind,src=' + str(local.ROOT) + ',dst=/repo,readonly',
        '--mount', 'type=bind,src=' + str(material / 'files' / profile.select('infrastructure')[0].name / 'deployment-admin') + ',dst=/case,readonly',
        '--entrypoint', 'python3', image, '/repo/tools/deploy/account.py', '--in-container', '--origin', profile.origin]
    subprocess.run(command, input=json.dumps({'username': args.username, 'password': password, 'roles': args.roles}), text=True, check=True)


if __name__ == '__main__': main()
