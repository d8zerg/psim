"""Issue fresh synthetic connector identities only for a complete local lab installation."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import secrets
import shutil
import ssl
import subprocess
import sys
import urllib.parse
import urllib.request
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material import command, verify_bundle
from model import require


def issue_identity(material, target, index, identity):
    key = target / (str(index) + '.key'); csr = target / (str(index) + '.csr'); extension = target / (str(index) + '.ext')
    command('openssl', 'req', '-new', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-noenc',
            '-subj', '/CN=psim-platform-probe', '-keyout', str(key), '-out', str(csr))
    extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\nsubjectAltName=URI:psim://connector/' + identity + '\n')
    command('openssl', 'x509', '-req', '-days', '1', '-set_serial', '0x' + secrets.token_hex(16),
            '-in', str(csr), '-CA', str(material / 'authority/platform.pem'), '-CAkey', str(material / 'authority/platform.key'),
            '-extfile', str(extension), '-out', str(target / (str(index) + '.pem')))
    csr.unlink(); extension.unlink()


def prepare(bundle, material):
    profile = verify_bundle(bundle)
    material = Path(material)
    installation = json.loads((material / 'installation.json').read_text())
    require(installation['lab'] is True and installation['complete'] is True and installation['name'] == profile.name,
            'synthetic issuance requires a complete lab installation')
    require(installation['public_manifest_sha256'] == hashlib.sha256((Path(bundle) / 'manifest.json').read_bytes()).hexdigest(),
            'lab material differs from qualification bundle')
    target = material / 'qualification'
    require(not target.is_symlink(), 'invalid qualification directory')
    if target.exists():
        require(json.loads((target / 'installation.json').read_text())['id'] == installation['id'], 'foreign qualification identities')
        shutil.rmtree(target)
    target.mkdir(mode=0o700)
    (target / 'installation.json').write_text(json.dumps({'id': installation['id'], 'tenant': installation['tenant']}))
    operator = material / 'files' / profile.select('infrastructure')[0].name / 'deployment-admin'
    for source, destination in [('ca.pem', 'ca.pem'), ('admin.pem', 'admin.pem'), ('admin.key', 'admin.key'),
                                ('identity.pem', 'reader.pem'), ('identity.key', 'reader.key'), ('identity.secret', 'identity.secret'),
                                ('postgres.pem', 'postgres.pem'), ('postgres.key', 'postgres.key'), ('postgres.secret', 'postgres.secret')]:
        shutil.copyfile(operator / source, target / destination)
    for filename in ('seaweed-ca.pem', 'seaweed.pem', 'seaweed.key', 's3.access', 's3.secret', 'clickhouse.secret'):
        shutil.copyfile(operator / filename, target / filename)
    (target / 'seaweed-client.toml').write_text('''[grpc]
ca = "/case/seaweed-ca.pem"
[grpc.client]
cert = "/case/seaweed.pem"
key = "/case/seaweed.key"
[https.client]
enabled = true
ca = "/case/seaweed-ca.pem"
cert = "/case/seaweed.pem"
key = "/case/seaweed.key"
''')
    identities = []
    for index in range(20):
        identity = str(uuid.uuid4()); identities.append(identity)
        issue_identity(material, target, index, identity)
    (target / 'identities.json').write_text(json.dumps(identities))
    nodes = profile.select('application')
    config = {'fixtures': '/case', 'tls': '/case', 'connector_type': 'qualification-' + installation['id'][:8],
              'gateways': [node.name + ':17443' for node in nodes],
              'sdk_endpoint': 'connector-gateway:7443', 'failover_interval_ms': 5, 'kafka': {'brokers': ','.join(node.name + ':9092' for node in profile.select('kafka')),
              'ca_file': '/case/ca.pem', 'certificate_file': '/case/reader.pem', 'private_key_file': '/case/reader.key'}}
    (target / 'config.json').write_text(json.dumps(config, indent=2) + '\n')
    for path in target.iterdir():
        path.chmod(0o400); os.chown(path, profile.uid('deployment-admin'), profile.uid('deployment-admin'))
    os.chown(target, profile.uid('deployment-admin'), profile.uid('deployment-admin'))
    print('Fresh synthetic identities prepared without exporting authority keys')


def extend(bundle, material, clients):
    profile = verify_bundle(bundle)
    require(1 <= clients <= 100 and 1000 % clients == 0, 'invalid load client count')
    material = Path(material)
    installation = json.loads((material / 'installation.json').read_text())
    target = material / 'qualification'
    require(installation['lab'] is True and installation['complete'] is True and
            json.loads((target / 'installation.json').read_text())['id'] == installation['id'],
            'load identities require an owned complete lab installation')
    path = target / 'identities.json'
    identities = json.loads(path.read_text())
    while len(identities) < 19 + clients:
        index = len(identities)
        identity = str(uuid.uuid4())
        for suffix in ('.pem', '.key', '.csr', '.ext'):
            (target / (str(index) + suffix)).unlink(missing_ok=True)
        issue_identity(material, target, index, identity)
        for suffix in ('.pem', '.key'):
            file = target / (str(index) + suffix)
            file.chmod(0o400)
            os.chown(file, profile.uid('deployment-admin'), profile.uid('deployment-admin'))
        identities.append(identity)
        temporary = target / 'identities.tmp'
        temporary.unlink(missing_ok=True)
        temporary.write_text(json.dumps(identities))
        temporary.chmod(0o400)
        os.chown(temporary, profile.uid('deployment-admin'), profile.uid('deployment-admin'))
        temporary.replace(path)
    print('Independent synthetic connector identities available: ' + str(clients))


def token():
    target = Path('/case')
    context = ssl.create_default_context(cafile=str(target / 'ca.pem'))
    body = urllib.parse.urlencode({'grant_type': 'client_credentials', 'client_id': 'deployment-admin',
                                  'client_secret': (target / 'identity.secret').read_text().strip()}).encode()
    request = urllib.request.Request('https://keycloak:18444/identity/realms/psim/protocol/openid-connect/token', data=body,
                                    headers={'Content-Type': 'application/x-www-form-urlencoded'})
    with urllib.request.urlopen(request, context=context, timeout=15) as response:
        access = json.load(response)['access_token']
    path = target / 'operator.token'
    if path.exists(): path.chmod(0o600)
    path.write_text(access); path.chmod(0o400)
    print('Qualification token issued by the installation identity provider')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['prepare', 'extend', 'token'])
    parser.add_argument('--clients', type=int, default=100)
    parser.add_argument('--bundle'); parser.add_argument('--material')
    args = parser.parse_args()
    if args.action == 'prepare': prepare(args.bundle, args.material)
    elif args.action == 'extend': extend(args.bundle, args.material, args.clients)
    else: token()
