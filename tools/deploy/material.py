"""Create fresh lab material; production installs import material issued by external authorities."""
import argparse
import base64
import hashlib
import ipaddress
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import subprocess
import sys
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import Profile, require
import policies


def command(*args):
    result = subprocess.run(args, capture_output=True, text=True)
    require(result.returncode == 0, 'material command failed: ' + args[0])
    return result.stdout


def verify_bundle(directory):
    directory = Path(directory)
    manifest = json.loads((directory / 'manifest.json').read_text())
    actual = {str(path.relative_to(directory)) for path in directory.rglob('*') if path.is_file() and path != directory / 'manifest.json'}
    require(actual == set(manifest['files']), 'deployment bundle file set differs from its manifest')
    for relative, digest in manifest['files'].items():
        path = Path(relative)
        require(not path.is_absolute() and '..' not in path.parts and not (directory / path).is_symlink(), 'invalid bundle file path')
        require(hashlib.sha256((directory / path).read_bytes()).hexdigest() == digest, 'deployment bundle checksum mismatch')
    return Profile.load(directory / 'profile.json')


class Material:
    def __init__(self, bundle, output):
        self.bundle = Path(bundle)
        self.profile = verify_bundle(bundle)
        root = Path(__file__).resolve().parents[2]
        for relative, checksum in json.loads((self.bundle / 'generation.json').read_text()).items():
            require(relative in ('tools/deploy/material.py', 'tools/deploy/policies.py', 'deploy/observability/business.sql', 'deploy/local/config/keycloak/psim-realm.json'),
                    'unknown material generation input')
            require(hashlib.sha256((root / relative).read_bytes()).hexdigest() == checksum,
                    'material generator differs from the sealed deployment bundle')
        self.output = Path(output)
        self.files = json.loads((self.bundle / 'material.json').read_text())['files']
        for relative, entry in self.files.items():
            if entry['kind'] == 'certificate': entry.setdefault('authority', 'platform')
            parts = Path(relative).parts
            require(len(parts) == 3 and parts[:2] == (entry['node'], entry['owner']), 'invalid material path')
            require(entry['node'] in {n.name for n in self.profile.nodes} and re.fullmatch(r'[A-Za-z0-9_.-]+', parts[2]) and parts[2] not in ('.', '..'), 'invalid material owner or filename')
            require(entry['uid'] == self.profile.uid(entry['owner']) and entry['mode'] == '0400', 'invalid material ownership')
        self.secrets = {}
        self.tenant = str(uuid.uuid4())
        self.cluster_id = base64.urlsafe_b64encode(uuid.uuid4().bytes).decode().rstrip('=')

    def target(self, relative):
        path = self.output / 'files' / relative
        path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        return path

    def secret(self, identifier):
        require(identifier in self.secrets, 'undeclared deployment secret: ' + identifier)
        return self.secrets[identifier]

    def sibling(self, relative, filename):
        require(isinstance(filename, str) and re.fullmatch(r'[A-Za-z0-9_.-]+', filename) and filename not in ('.', '..'), 'invalid material reference')
        source = str(Path(relative).parent / filename)
        require(source in self.files, 'undeclared material reference')
        return self.target(source)

    def create_lab(self):
        self.prepare(False)

    def request_external(self):
        self.prepare(True)

    def prepare(self, external):
        require(not self.output.exists(), 'material destination exists; refuse to replace identities or secrets')
        self.output.mkdir(parents=True, mode=0o700)
        control = self.output / 'installation.json'
        installation = {'format': 1, 'id': str(uuid.uuid4()), 'name': self.profile.name, 'tenant': self.tenant,
            'cluster_id': self.cluster_id, 'lab': not external, 'complete': False,
            'public_manifest_sha256': hashlib.sha256((self.bundle / 'manifest.json').read_bytes()).hexdigest()}
        control.write_text(json.dumps(installation, indent=2) + '\n')
        authority = self.output / 'authority'
        if not external: authority.mkdir(mode=0o700)
        domains = {entry.get('authority', 'platform') for entry in self.files.values() if entry['kind'] == 'certificate'}
        for name in sorted(domains):
            require(re.fullmatch('[a-z][a-z0-9-]{0,23}', name), 'invalid authority name')
            if external: continue
            command('openssl', 'req', '-x509', '-newkey', 'ec', '-pkeyopt', 'ec_paramgen_curve:P-256', '-noenc', '-days', '7',
                    '-subj', '/CN=psim-platform-lab-' + name, '-addext', 'basicConstraints=critical,CA:TRUE,pathlen:0',
                    '-addext', 'keyUsage=critical,keyCertSign,cRLSign', '-keyout', str(authority / (name + '.key')),
                    '-out', str(authority / (name + '.pem')))
        for relative, entry in self.files.items():
            kind = entry['kind']
            if kind == 'password':
                identifier = entry.get('secret_id', entry['owner'] + '/' + Path(relative).name)
                self.secrets.setdefault(identifier, secrets.token_urlsafe(32))
                self.target(relative).write_text(self.secrets[identifier])
            elif kind in ('private-key', 'audit-signing-key'):
                args = ['openssl', 'genpkey', '-algorithm', 'ED25519'] if kind == 'audit-signing-key' else ['openssl', 'genpkey', '-algorithm', 'EC', '-pkeyopt', 'ec_paramgen_curve:P-256']
                command(*args, '-out', str(self.target(relative)))
        for relative, entry in self.files.items():
            if entry['kind'] != 'certificate':
                continue
            domain = entry.get('authority', 'platform')
            path = self.target(relative)
            key = path.with_suffix('.key')
            request = path.with_suffix('.csr')
            extension = path.with_suffix('.ext')
            sans = []
            for name in entry['dns_names']:
                try:
                    ipaddress.ip_address(name); sans.append('IP:' + name)
                except ValueError:
                    require(re.fullmatch(r'[A-Za-z0-9.-]+', name), 'invalid certificate DNS name')
                    sans.append('DNS:' + name)
            if entry.get('uri'):
                require(re.fullmatch(r'psim://service/[a-z][a-z0-9-]+', entry['uri']), 'invalid service certificate URI')
                sans.append('URI:' + entry['uri'])
            require(re.fullmatch(r'[a-z][a-z0-9_-]+', entry['common_name']), 'invalid certificate common name')
            extension.write_text('basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=serverAuth,clientAuth\nsubjectAltName=' + ','.join(sans) + '\n')
            arguments = ['openssl', 'req', '-new', '-key', str(key), '-subj', '/CN=' + entry['common_name'], '-out', str(request)]
            for line in extension.read_text().splitlines(): arguments += ['-addext', line]
            command(*arguments)
            if external:
                destination = self.output / 'requests' / (relative + '.csr')
                destination.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
                request.replace(destination); extension.unlink()
                continue
            command('openssl', 'x509', '-req', '-in', str(request), '-days', '2', '-set_serial', '0x' + secrets.token_hex(16),
                    '-CA', str(authority / (domain + '.pem')), '-CAkey', str(authority / (domain + '.key')),
                    '-extfile', str(extension), '-out', str(path))
            request.unlink(); extension.unlink()
        if external:
            self.output.joinpath('requests.json').write_text(json.dumps({'format': 1, 'files': {
                relative + '.csr': {'authority': entry['authority'], 'common_name': entry['common_name'],
                                    'dns_names': entry['dns_names'], 'uri': entry.get('uri')}
                for relative, entry in self.files.items() if entry['kind'] == 'certificate'}}, indent=2) + '\n')
            self.lock_files()
            print('external certificate requests prepared: ' + self.profile.name)
            return
        self.finish(installation, authority)

    def lock_files(self):
        for path in self.output.rglob('*'):
            if path.is_file() and path.stat().st_mode & 0o777 != 0o400: path.chmod(0o400)
        for relative, entry in self.files.items():
            path = self.target(relative)
            if path.exists(): os.chown(path, entry['uid'], entry['uid'])
            os.chown(path.parent, entry['uid'], entry['uid'])
            require(path.parent.stat().st_mode & 0o777 == 0o700, 'unsafe private material directory permissions')

    def complete_external(self, trust_directory, signed_directory):
        require(not self.output.is_symlink(), 'invalid private material location')
        installation = json.loads((self.output / 'installation.json').read_text())
        require(installation['lab'] is False and installation['complete'] is False and installation['name'] == self.profile.name,
                'external material identity or phase mismatch')
        require(installation['public_manifest_sha256'] == hashlib.sha256((self.bundle / 'manifest.json').read_bytes()).hexdigest(),
                'external material belongs to another bundle')
        self.tenant, self.cluster_id = installation['tenant'], installation['cluster_id']
        authority = self.output / 'authority'; authority.mkdir(mode=0o700, exist_ok=True)
        trust_directory, signed_directory = Path(trust_directory), Path(signed_directory)
        require(trust_directory.is_dir() and signed_directory.is_dir() and not trust_directory.is_symlink() and not signed_directory.is_symlink(),
                'external material inputs must be real directories')
        for name in sorted({entry.get('authority', 'platform') for entry in self.files.values() if entry['kind'] == 'certificate'}):
            source = trust_directory / (name + '.pem')
            require(source.is_file() and not source.is_symlink() and source.stat().st_size <= 1048576, 'missing or oversized external trust bundle')
            body = source.read_bytes()
            require(b'PRIVATE KEY' not in body, 'trust bundle must not contain a private key')
            (authority / (name + '.pem')).write_bytes(body)
        for relative, entry in self.files.items():
            if entry['kind'] == 'password':
                identifier = entry.get('secret_id', entry['owner'] + '/' + Path(relative).name)
                value = self.target(relative).read_text()
                require(value and self.secrets.get(identifier, value) == value, 'inconsistent prepared service credentials')
                self.secrets[identifier] = value
            if entry['kind'] != 'certificate': continue
            source = signed_directory / relative
            require(source.is_file() and not source.is_symlink() and source.stat().st_size <= 1048576, 'missing or oversized signed certificate')
            trust = str(authority / (entry['authority'] + '.pem'))
            command('openssl', 'x509', '-in', str(source), '-checkend', '3600', '-noout')
            for purpose in ('sslserver', 'sslclient'):
                command('openssl', 'verify', '-purpose', purpose, '-CAfile', trust, '-untrusted', str(source), str(source))
            require(command('openssl', 'x509', '-in', str(source), '-noout', '-subject', '-nameopt', 'RFC2253').strip() == 'subject=CN=' + entry['common_name'],
                    'signed certificate subject differs from the request')
            key = self.sibling(relative, Path(relative).stem + '.key')
            require(command('openssl', 'x509', '-in', str(source), '-pubkey', '-noout').strip() == command('openssl', 'pkey', '-in', str(key), '-pubout').strip(),
                    'signed certificate does not match its private key')
            actual = command('openssl', 'x509', '-in', str(source), '-noout', '-ext', 'subjectAltName').splitlines()[1:]
            actual = {part.strip().replace('IP Address:', 'IP:') for part in ''.join(actual).split(',') if part.strip()}
            expected = set()
            for name in entry['dns_names']:
                try: expected.add('IP:' + str(ipaddress.ip_address(name)))
                except ValueError: expected.add('DNS:' + name)
            if entry.get('uri'): expected.add('URI:' + entry['uri'])
            require(actual == expected, 'signed certificate SANs differ from the request')
            basic = command('openssl', 'x509', '-in', str(source), '-noout', '-ext', 'basicConstraints')
            usage = command('openssl', 'x509', '-in', str(source), '-noout', '-ext', 'extendedKeyUsage')
            require('CA:FALSE' in basic and 'TLS Web Server Authentication' in usage and 'TLS Web Client Authentication' in usage,
                    'signed certificate constraints or usage differ from the request')
            shutil.copyfile(source, self.target(relative))
        self.finish(installation, authority)

    def finish(self, installation, authority):
        trusted = {}
        for relative, entry in self.files.items():
            if entry['kind'] == 'audit-signing-key':
                key = self.target(relative)
                public = command('openssl', 'pkey', '-in', str(key), '-pubout')
                raw = subprocess.check_output(['openssl', 'pkey', '-in', str(key), '-pubout', '-outform', 'DER'])[-32:]
                trusted[hashlib.sha256(raw).hexdigest()] = public
        for relative, entry in self.files.items():
            path = self.target(relative)
            if entry['kind'] == 'trust-bundle':
                path.write_text(''.join((authority / (name + '.pem')).read_text() for name in entry.get('authorities', ['platform'])))
            elif entry['kind'] == 'audit-trust':
                path.write_text(json.dumps(trusted, indent=2) + '\n')
            elif entry['kind'] == 'private-key-der':
                command('openssl', 'pkcs8', '-topk8', '-nocrypt', '-in', str(self.sibling(relative, entry['source'])), '-outform', 'DER', '-out', str(path))
        for relative, entry in self.files.items():
            if entry['kind'] == 'certificate-copy':
                source = entry['source']
                require(source in self.files and self.files[source]['kind'] in ('certificate', 'trust-bundle')
                        and self.files[source]['node'] == entry['node'], 'invalid public certificate source')
                self.target(relative).write_bytes(self.target(source).read_bytes())
        providers = {'postgres-bootstrap': policies.postgres, 'valkey-acl': policies.valkey, 'sentinel-acl': policies.sentinel,
                     'clickhouse-users': policies.clickhouse, 's3-identities': policies.s3,
                     'keycloak-realm': lambda profile, secret: policies.realm(profile, secret, self.tenant)}
        values = {'cluster-id': self.cluster_id}
        for relative, entry in self.files.items():
            if entry['kind'] != 'template':
                continue
            path = self.target(relative)
            def replace(match):
                token = match.group(1)
                if token.startswith('secret:'):
                    return self.secret(token[7:])
                if token.startswith('file:'):
                    value = self.sibling(relative, token[5:]).read_text()
                    return value.replace('\\', '\\\\').replace('\n', '\\n') if path.suffix == '.properties' else value
                if token.startswith('combine:'):
                    return ''.join(self.sibling(relative, filename).read_text() for filename in token[8:].split(','))
                if token in providers:
                    if token not in values:
                        values[token] = providers[token](self.profile, self.secret)
                    return values[token]
                require(token in values, 'unknown material template')
                return values[token]
            path.write_text(re.sub(r'\{\{([^{}]+)\}\}', replace, entry['template']))
            require('{{' not in path.read_text(), 'unresolved material template')
        installation['complete'] = True
        (self.output / 'installation.json').write_text(json.dumps(installation, indent=2) + '\n')
        self.lock_files()
        print('private material complete: ' + self.profile.name + '; files=' + str(len(self.files)))


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--output', required=True)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--lab-issue', action='store_true')
    mode.add_argument('--external-request', action='store_true')
    mode.add_argument('--external-complete', action='store_true')
    parser.add_argument('--trust-directory')
    parser.add_argument('--signed-directory')
    args = parser.parse_args()
    os.umask(0o077)
    material = Material(args.bundle, args.output)
    if args.external_complete:
        require(args.trust_directory and args.signed_directory, 'external completion requires trust and signed certificate directories')
        material.complete_external(args.trust_directory, args.signed_directory)
    elif args.external_request: material.request_external()
    else: material.create_lab()
