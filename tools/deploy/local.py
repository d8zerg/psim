"""Deliver a lab bundle to the owned KVM guests with pinned SSH identities."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import fcntl
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/vm'))
import vm
sys.path.insert(0, str(Path(__file__).resolve().parent))
from material import verify_bundle
from model import SERVICES, require


def execute(node, command, **kwargs):
    return vm.ssh('psim-platform-vm', node, command, **kwargs)


def images_for(bundle, node):
    compose = json.loads((bundle / node / 'compose.json').read_text())
    return sorted({service['image'] for service in compose['services'].values() if service['image'].startswith('psim-platform/')})


def export_images(references):
    identities = {name: vm.capture(['docker', 'image', 'inspect', '--format', '{{.Id}}', name]) for name in references}
    identity = hashlib.sha256(json.dumps(identities, sort_keys=True).encode()).hexdigest()
    folder = ROOT / 'build/deployment/images'
    folder.mkdir(parents=True, exist_ok=True)
    archive = folder / (identity + '.tar')
    sidecar = archive.with_suffix('.json')
    with archive.with_suffix('.lock').open('w') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        if not archive.exists() or not sidecar.exists():
            temporary = archive.with_suffix('.part')
            try:
                vm.run(['docker', 'image', 'save', '--output', str(temporary), *references])
                temporary.replace(archive)
                sidecar.write_text(json.dumps({'images': identities, 'sha256': hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()}, indent=2) + '\n')
            finally:
                temporary.unlink(missing_ok=True)
        saved = json.loads(sidecar.read_text())
        require(saved['images'] == identities and hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest() == saved['sha256'], 'cached image archive differs from its manifest')
        if 'oci' not in saved:
            with tarfile.open(archive) as stream:
                def blob(digest):
                    body = stream.extractfile('blobs/' + digest.replace(':', '/')).read()
                    require('sha256:' + hashlib.sha256(body).hexdigest() == digest, 'OCI blob checksum mismatch')
                    return json.loads(body)
                def compatible(descriptor):
                    digest = descriptor['digest']; body = blob(digest)
                    if 'manifests' in body:
                        children = [child for child in body['manifests'] if child.get('platform', {}).get('architecture') == 'amd64' and child.get('platform', {}).get('os') == 'linux']
                        require(len(children) == 1, 'OCI index must contain exactly one Linux amd64 image')
                        return [digest, *compatible(children[0])]
                    config_digest = body['config']['digest']; config = blob(config_digest)
                    require(config['architecture'] == 'amd64' and config['os'] == 'linux', 'unsupported image platform')
                    return [digest, config_digest]
                index = json.load(stream.extractfile('index.json'))
                saved['oci'] = {entry['annotations']['io.containerd.image.name'].removeprefix('docker.io/'): compatible(entry) for entry in index['manifests']}
                require(set(saved['oci']) == set(identities), 'exported image reference set differs from the request')
                require(all(identity in saved['oci'][name] for name, identity in identities.items()), 'image changed during export')
            sidecar.write_text(json.dumps(saved, indent=2) + '\n')
    return archive, saved


def images_match(node, expected):
    result = execute(node, ['sudo', 'docker', 'image', 'inspect', '--format', '{{json .}}', *expected], check=False, capture_output=True)
    found = {}
    for line in result.stdout.splitlines():
        if not line.strip():
            continue
        image = json.loads(line)
        for name in image.get('RepoTags') or []:
            found[name] = image['Id']
    return all(found.get(name) in identities for name, identities in expected.items())


def prepare(bundle, material):
    bundle = Path(bundle).resolve(); material = Path(material).resolve()
    profile = verify_bundle(bundle)
    value = vm.profile(ROOT / 'deploy/vm/local.json')
    value['nodes'] = [node for node in value['nodes'] if node['name'] in {item.name for item in profile.nodes}]
    saved = vm.manifest(value['name']) if vm.directory(value['name']).exists() else None
    nodes = {node['name']: node['address'] for node in value['nodes']}
    require(all(nodes.get(node.name) == node.address for node in profile.nodes), 'deployment inventory differs from the owned VM inventory')
    target = profile.virtual_ip.split('/')[0] if profile.virtual_ip else profile.nodes[0].address
    for forward in value['forwards']:
        forward['target_address'] = target
    if saved and saved.get('forwards', []) != value['forwards']:
        vm.down(value['name'])
    vm.up(value)
    spec = importlib.util.spec_from_file_location('psim_vm_bootstrap', ROOT / 'tools/vm/bootstrap.py')
    bootstrap = importlib.util.module_from_spec(spec); spec.loader.exec_module(bootstrap)
    bootstrap.bootstrap(value)
    output = ROOT / 'build/deployment/payloads' / bundle.name
    output.mkdir(parents=True, exist_ok=True)
    archives = {}
    for node in profile.nodes:
        payload = output / (node.name + '.tar.gz')
        vm.run(['docker', 'run', '--rm', '--user', '0:0', '--env', 'LD_PRELOAD=',
                '--volume', str(ROOT) + ':/repo:ro', '--volume', str(output) + ':/output', '--workdir', '/repo',
                '--entrypoint', 'python3', 'psim-platform/postgres:' + profile.version, '-I', 'tools/deploy/archive.py',
                '--bundle', '/repo/' + str(bundle.relative_to(ROOT)), '--material', '/repo/' + str(material.relative_to(ROOT)),
                '--node', node.name, '--output', '/output/' + payload.name, '--owner', str(os.getuid())])
        image_archive, image_manifest = export_images(images_for(bundle, node.name))
        archives[node.name] = (payload, image_archive, image_manifest)
    def deliver(node):
        print('Delivering node payload and images: ' + node.name, flush=True)
        destination = '/home/psim/.cache/psim-delivery'
        execute(node.name, ['install', '-d', '-m', '0700', destination])
        payload, image_archive, image_manifest = archives[node.name]
        missing_images = not images_match(node.name, image_manifest['oci'])
        sources = [payload, ROOT / 'tools/deploy/node_install.py'] + ([image_archive] if missing_images else [])
        for source in sources:
            vm.copy_to('psim-platform-vm', node.name, source, destination + '/' + source.name)
        execute(node.name, ['sudo', 'python3', destination + '/node_install.py', '--archive', destination + '/' + payload.name,
            '--sha256', hashlib.sha256(payload.read_bytes()).hexdigest(), '--node', node.name])
        if missing_images:
            execute(node.name, ['sudo', 'docker', 'image', 'load', '--input', destination + '/' + image_archive.name], capture_output=True)
        require(images_match(node.name, image_manifest['oci']), 'loaded OCI image identity mismatch')
        execute(node.name, ['rm', '-f', destination + '/' + payload.name, destination + '/' + image_archive.name])
        print('Node payload and image identities verified: ' + node.name, flush=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(deliver, profile.nodes))


def compose(profile, node, command):
    return execute(node, ['sudo', 'docker', 'compose', '--parallel', '2', '--file', profile.prefix + '/config/' + node + '/compose.json', *command])


def start(bundle, selected):
    profile = verify_bundle(bundle)
    for node in profile.nodes:
        config = json.loads((Path(bundle) / node.name / 'compose.json').read_text())
        names = [name for name in config['services'] if not selected or name in selected]
        if names:
            for attempt in range(3):
                try:
                    compose(profile, node.name, ['up', '-d', *names])
                    break
                except subprocess.CalledProcessError:
                    if attempt == 2: raise
                    print('Retrying node service startup: ' + node.name, flush=True)
                    time.sleep(2)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True)
    actions = parser.add_subparsers(dest='action', required=True)
    action = actions.add_parser('prepare'); action.add_argument('--material', required=True)
    action = actions.add_parser('start'); action.add_argument('services', nargs='*')
    args = parser.parse_args()
    if args.action == 'prepare': prepare(args.bundle, args.material)
    else: start(args.bundle, set(args.services))
