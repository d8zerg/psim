"""Validate signed offline releases and compatibility before changing an installation."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from version import validate


def require(value, message):
    if not value:
        raise ValueError(message)


def digest(path):
    with Path(path).open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def filename(value):
    require(isinstance(value, str) and re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9._+~-]*', value), 'unsafe release filename')
    return value


def verify(directory, trusted, selected_artifacts=None):
    directory, trusted = Path(directory), Path(trusted)
    require(directory.is_dir() and not directory.is_symlink(), 'invalid release directory')
    files = list(directory.iterdir())
    require(all(path.is_file() and not path.is_symlink() for path in files), 'release contains non-regular files')
    for path in files:
        filename(path.name)
    sums = directory / 'SHA256SUMS'
    require(sums.is_file() and (directory / 'SHA256SUMS.sig').is_file(), 'release signature is missing')
    key = subprocess.check_output(['openssl', 'pkey', '-pubin', '-in', str(trusted), '-outform', 'DER'], stderr=subprocess.DEVNULL)
    require(key.startswith(bytes.fromhex('302a300506032b6570032100')) and len(key) == 44, 'release trust key must be Ed25519')
    signature = subprocess.run(['openssl', 'pkeyutl', '-verify', '-pubin', '-inkey', str(trusted), '-rawin',
                               '-in', str(sums), '-sigfile', str(directory / 'SHA256SUMS.sig')], capture_output=True)
    require(signature.returncode == 0, 'release signature does not match the separately trusted key')
    expected = {}
    for line in sums.read_text().splitlines():
        match = re.fullmatch(r'([0-9a-f]{64})  ([A-Za-z0-9][A-Za-z0-9._+~-]*)', line)
        require(match is not None and match[2] not in expected, 'invalid or duplicate signed checksum entry')
        expected[match[2]] = match[1]
    require(set(expected) == {path.name for path in files} - {'SHA256SUMS', 'SHA256SUMS.sig', 'psim-signing.pub.pem'},
            'release file set differs from the signed inventory')
    for name, checksum in expected.items():
        if selected_artifacts is not None and name.endswith(('.deb', '.tar')) and name not in selected_artifacts:
            continue
        require(digest(directory / name) == checksum, 'release checksum mismatch: ' + name)
    for name in expected:
        if not name.endswith(('.deb', '.tar')): continue
        sbom = name + '.cdx.json'
        require(sbom in expected, 'release artifact SBOM is missing')
        document = json.loads((directory / sbom).read_text())
        require(document.get('bomFormat') == 'CycloneDX' and any(
            entry.get('alg') == 'SHA-256' and entry.get('content') == expected[name]
            for entry in document['metadata']['component']['hashes']), 'release artifact SBOM identity mismatch')
    require('release.json' in expected, 'release lifecycle manifest is missing')
    manifest = json.loads((directory / 'release.json').read_text())
    require(manifest['format'] == 1 and manifest['platform'] == 'debian13-amd64', 'unsupported release format or platform')
    validate(manifest['version'])
    require(re.fullmatch(r'[0-9a-f]{40}', manifest['revision']), 'invalid release source revision')
    require(type(manifest['config_format']) is int and manifest['config_format'] > 0, 'invalid configuration format')
    for name, component in manifest['components'].items():
        require(re.fullmatch(r'[a-z][a-z0-9-]*', name), 'invalid component name')
        for kind in ('image', 'deb'):
            artifact = filename(component[kind]['file'])
            require(artifact in expected, 'component artifact is not signed')
            sbom = filename(artifact + '.cdx.json')
            require(sbom in expected, 'component SBOM is not signed')
            document = json.loads((directory / sbom).read_text())
            require(document.get('bomFormat') == 'CycloneDX' and any(
                entry.get('alg') == 'SHA-256' and entry.get('content') == expected[artifact]
                for entry in document['metadata']['component']['hashes']), 'SBOM artifact identity mismatch')
        require(re.fullmatch(r'sha256:[0-9a-f]{64}', component['image']['id']), 'invalid immutable image ID')
        filename(component['image']['tag'].split(':')[-1])
    require(filename(manifest['contracts']) in expected, 'contract snapshot is not signed')
    if 'migration_tools' in manifest:
        require(filename(manifest['migration_tools']['file']) in expected, 'migration tools image is not signed')
        require(manifest['migration_tools']['digests'] and all(re.fullmatch(r'sha256:[0-9a-f]{64}', value)
                for value in manifest['migration_tools']['digests']), 'invalid migration tools OCI identity')
    for name in manifest.get('auxiliary_debs', []):
        require(filename(name) in expected and name.endswith('.deb'), 'auxiliary package is not signed')
    for plan in manifest.get('migrations', []):
        require(filename(plan['file']) in expected and plan['engine'] in ('postgres', 'clickhouse'), 'migration is not signed')
        migration_plan(json.loads((directory / plan['file']).read_text()))
    return manifest


def migration_plan(document):
    require(document.get('phase', 'expand') == 'expand', 'automatic releases may contain only expand migrations')
    previous = 0
    for migration in document['migrations']:
        version = migration['version']
        require(type(version) is int and version > previous and migration['expand'], 'invalid migration order')
        floor = migration.get('minimum_reader', version)
        require(type(floor) is int and 0 < floor <= version, 'invalid migration reader floor')
        for statement in migration['expand']:
            # Deliberately narrow DDL grammar; data movement and destructive operations need a separate release procedure.
            text = re.sub(r'\s+', ' ', statement.strip()).upper()
            require(';' not in text.rstrip(';') and '--' not in text and '/*' not in text, 'multiple SQL statements or comments are forbidden')
            require(re.match(r'^(CREATE TABLE IF NOT EXISTS |CREATE (UNIQUE )?INDEX CONCURRENTLY IF NOT EXISTS |ALTER TABLE \S+(?: ON CLUSTER \S+)? ADD COLUMN IF NOT EXISTS )', text),
                    'migration is outside the supported expand DDL grammar')
            require(not re.search(r'\b(DROP|DELETE|TRUNCATE|RENAME|UPDATE|NOT NULL|ATTACH|DETACH)\b', text), 'destructive or blocking expansion is forbidden')
        previous = version


def compatible(old, new):
    """Conservative additive compatibility for normalized REST and protobuf descriptors."""
    require(set(old.get('admin_info_fields', [])) <= set(new.get('admin_info_fields', [])), 'administrative info fields were removed')
    for service, methods in old.get('services', {}).items():
        require(service in new.get('services', {}) and all(new['services'][service].get(name) == value for name, value in methods.items()),
                'gRPC service or method changed')
    for message, fields in old['messages'].items():
        require(message in new['messages'], 'protobuf message was removed: ' + message)
        for number, field in fields.items():
            target = new['messages'][message].get(number)
            require(target is not None and all(target.get(key) == value for key, value in field.items()),
                    'protobuf field changed: ' + message + '/' + number)
        for number, field in new['messages'][message].items():
            if number in fields: continue
            options = json.dumps(field.get('options') or {}, separators=(',', ':'))
            require(field.get('label') not in ('LABEL_REQUIRED', 2) and '"required":true' not in options,
                    'new required protobuf field forbids mixed-version writers')
    for name, values in old['enums'].items():
        require(name in new['enums'] and all(new['enums'][name].get(key) == value for key, value in values.items()), 'protobuf enum changed: ' + name)
    for path, methods in old['paths'].items():
        require(path in new['paths'], 'REST path was removed: ' + path)
        for method, operation in methods.items():
            require(method in new['paths'][path], 'REST operation was removed')
            target = new['paths'][path][method]
            require(operation == target, 'existing REST operation changed; explicit compatibility review required')
    for name, schema in old['schemas'].items():
        require(name in new['schemas'], 'REST schema was removed')
        target = new['schemas'][name]
        require(set(target.get('required', [])) <= set(schema.get('required', [])), 'new required REST fields forbid rolling upgrade')
        require({key: value for key, value in schema.items() if key not in ('properties', 'required', 'description')} ==
                {key: value for key, value in target.items() if key not in ('properties', 'required', 'description')}, 'REST schema constraints changed')
        for key, value in schema.get('properties', {}).items():
            require(target.get('properties', {}).get(key) == value, 'REST property changed')


def transition(old, new, old_snapshot, new_snapshot):
    def precedence(value):
        match = __import__('version').SEMVER.fullmatch(validate(value))
        return (tuple(map(int, match.group(1, 2, 3))), 1 if match[4] is None else 0,
                tuple((0, int(part)) if part.isdigit() else (1, part) for part in (match[4] or '').split('.')))
    require(precedence(new['version']) > precedence(old['version']), 'upgrade must increase the semantic version; use explicit rollback for retained releases')
    require(set(old['components']) == set(new['components']), 'component inventory changes require a topology migration')
    require(old['infrastructure'] == new['infrastructure'], 'application upgrade cannot silently replace infrastructure versions')
    require(new['config_format'] in (old['config_format'], old['config_format'] + 1), 'unsupported configuration format transition')
    require(old['protocol_major'] == new['protocol_major'], 'incompatible connector protocol')
    compatible(old_snapshot, new_snapshot)


def migrate_config(value, source, target, operations, context=None):
    require(target in (source, source + 1), 'unsupported configuration migration')
    result = json.loads(json.dumps(value))
    for operation in operations:
        require(set(operation) <= {'path', 'rename', 'default', 'profile_default'}, 'unknown configuration migration operation')
        parts = operation['path'].split('.')
        require(parts and all(re.fullmatch(r'[a-zA-Z_][a-zA-Z_0-9]*', part) for part in parts), 'unsafe configuration migration path')
        parent = result
        for part in parts[:-1]:
            parent = parent.setdefault(part, {})
            require(isinstance(parent, dict), 'configuration path is not an object')
        key = parts[-1]
        if 'rename' in operation and key in parent:
            destination = operation['rename']
            require(re.fullmatch(r'[a-zA-Z_][a-zA-Z_0-9]*', destination) and destination not in parent, 'configuration rename conflicts with an existing value')
            parent[destination] = parent.pop(key)
        elif 'default' in operation:
            parent.setdefault(key, operation['default'])
        elif 'profile_default' in operation:
            name = operation['profile_default']
            require(context is not None and name in ('redis_host', 'redis_sentinels', 'redis_primary_name') and name in context,
                    'unknown configuration inventory reference')
            parent.setdefault(key, context[name])
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--release', required=True); parser.add_argument('--trusted-key', required=True)
    args = parser.parse_args()
    print(json.dumps(verify(args.release, args.trusted_key), indent=2))
