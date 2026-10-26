"""Attach lifecycle metadata to an offline release before its checksum inventory is signed."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import hashlib

sys.path.insert(0, str(Path(__file__).resolve().parent))
from delivery import require
from model import SERVICES

ROOT = Path(__file__).resolve().parents[2]


def image_digests(path):
    with tarfile.open(path) as archive:
        def walk(descriptor):
            identity = descriptor['digest']
            require(__import__('re').fullmatch(r'sha256:[0-9a-f]{64}', identity), 'invalid OCI descriptor digest')
            body = archive.extractfile('blobs/' + identity.replace(':', '/')).read()
            require('sha256:' + hashlib.sha256(body).hexdigest() == identity, 'OCI descriptor checksum mismatch')
            value = json.loads(body)
            if 'manifests' in value:
                children = [item for item in value['manifests'] if item.get('platform', {}).get('os') == 'linux'
                            and item.get('platform', {}).get('architecture') == 'amd64']
                require(len(children) == 1, 'OCI release must contain exactly one Linux amd64 image')
                return [identity, *walk(children[0])]
            if 'config' in value and 'layers' in value:
                return [identity, *walk(value['config'])]
            require(value.get('architecture') == 'amd64' and value.get('os') == 'linux', 'unsupported OCI configuration platform')
            return [identity]
        index = json.load(archive.extractfile('index.json'))
        require(len(index['manifests']) == 1, 'image archive has an ambiguous root descriptor')
        return walk(index['manifests'][0])


def create(directory, version, image_tag, revision, contracts, config_format=2):
    directory = Path(directory)
    require(not (directory / 'SHA256SUMS.sig').exists(), 'attach lifecycle metadata before signing the release')
    components = {}
    for name, (binary, _, _) in SERVICES.items():
        image = 'psim-platform/psim-' + binary + ':' + image_tag
        identity = subprocess.check_output(['docker', 'image', 'inspect', '--format', '{{.Id}}', image], text=True).strip()
        images = [path for path in directory.glob('*_image.tar') if ('psim-' + binary + '_') in path.name]
        packages = list(directory.glob('psim-' + binary + '_*_amd64.deb'))
        require(len(images) == len(packages) == 1, 'release requires exactly one image archive and package per binary: ' + binary)
        package_version = packages[0].name.removeprefix('psim-' + binary + '_').removesuffix('_amd64.deb')
        identities = image_digests(images[0])
        require(identity in identities, 'image archive differs from the inspected image')
        components[name] = {'binary': 'psim-' + binary, 'image': {'file': images[0].name, 'id': identity, 'digests': identities, 'tag': image},
                            'deb': {'file': packages[0].name, 'version': package_version}}
    shutil.copyfile(contracts, directory / 'contracts.json')
    manifest = {'format': 1, 'platform': 'debian13-amd64', 'version': version, 'revision': revision,
                'config_format': config_format, 'schema_epoch': config_format, 'protocol_major': 1, 'contracts': 'contracts.json',
                'components': components, 'migrations': [], 'config_migrations': {},
                'infrastructure': json.loads((ROOT / 'deploy/topologies/images.json').read_text())}
    manifest['auxiliary_debs'] = [path.name for path in sorted(directory.glob('*.deb'))
                                 if path.name not in {value['deb']['file'] for value in components.values()}]
    if config_format == 2:
        tools = list(directory.glob('*psimctl*_image.tar'))
        require(len(tools) == 1, 'release requires the migration tools image')
        manifest['migration_tools'] = {'file': tools[0].name, 'tag': 'psim-platform/psimctl:' + image_tag,
                                        'digests': image_digests(tools[0])}
        manifest['config_migrations']['api-gateway'] = [
            {'path': 'settings.' + name, 'profile_default': name}
            for name in ('redis_host', 'redis_sentinels', 'redis_primary_name')]
        manifest['config_migrations']['api-gateway'] += [
            {'path': 'settings.' + name, 'default': value} for name, value in {
                'redis_user': 'api-gateway', 'redis_sentinel_user': 'api-gateway',
                'redis_password_file': '/etc/psim-platform/api-gateway/secrets/valkey.secret',
                'redis_sentinel_password_file': '/etc/psim-platform/api-gateway/secrets/sentinel.secret',
                'redis_ca_file': '/etc/psim-platform/api-gateway/secrets/ca.pem',
                'redis_certificate_file': '/etc/psim-platform/api-gateway/secrets/identity.pem',
                'redis_key_file': '/etc/psim-platform/api-gateway/secrets/identity.key'}.items()]
        plan = {'schema': 'psim_clickhouse', 'phase': 'expand', 'migrations': [{
            'version': 1, 'name': 'migration-reader-floor', 'minimum_reader': 1,
            'expand': ['ALTER TABLE psim_clickhouse.schema_migrations ADD COLUMN IF NOT EXISTS minimum_reader bigint DEFAULT 0']}]}
        (directory / 'postgres-reader-floor.json').write_text(json.dumps(plan, indent=2) + '\n')
        manifest['migrations'] = [{'engine': 'postgres', 'file': 'postgres-reader-floor.json'}]
        service_plan = {'schema': 'incident', 'phase': 'expand', 'migrations': [{
            'version': 1, 'name': 'service-reader-floor', 'minimum_reader': 1,
            'expand': ['ALTER TABLE incident.schema_migrations ADD COLUMN IF NOT EXISTS minimum_reader bigint DEFAULT 0']}]}
        (directory / 'postgres-service-reader-floor.json').write_text(json.dumps(service_plan, indent=2) + '\n')
        manifest['migrations'].append({'engine': 'postgres', 'file': 'postgres-service-reader-floor.json'})
    (directory / 'release.json').write_text(json.dumps(manifest, sort_keys=True, indent=2) + '\n')
    return manifest


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    for name in ('directory', 'version', 'image-tag', 'revision', 'contracts'): parser.add_argument('--' + name, required=True)
    parser.add_argument('--config-format', type=int, default=2)
    args = parser.parse_args()
    create(args.directory, args.version, args.image_tag, args.revision, args.contracts, args.config_format)
