"""Package only one lab node's material; authority private keys never enter the payload."""
import argparse
import gzip
import hashlib
import io
import json
import os
from pathlib import Path
import sys
import tarfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
from material import verify_bundle
from model import require

# NSS wrapper treats an initial zero mtime as an unchanged, empty cache.
FILE_MTIME = 1


def package(bundle, material, node, output, owner, external=False):
    bundle = Path(bundle); material = Path(material); output = Path(output)
    profile = verify_bundle(bundle)
    require(node in {n.name for n in profile.nodes}, 'unknown deployment node')
    state = json.loads((material / 'installation.json').read_text())
    require(state['lab'] is (not external) and state['name'] == profile.name, 'private material mode or installation differs from the requested payload')
    require(state.get('complete') is True, 'private material generation is incomplete')
    require(state['public_manifest_sha256'] == hashlib.sha256((bundle / 'manifest.json').read_bytes()).hexdigest(), 'material belongs to another public bundle')
    compose = json.loads((bundle / node / 'compose.json').read_text())
    requirements = json.loads((bundle / 'material.json').read_text())['files']
    require(not output.is_symlink(), 'node payload must not be a symlink')
    output.parent.mkdir(parents=True, exist_ok=True)
    files = {}
    directories = {}
    def add(name, body, uid=0, mode=0o644):
        require(name not in files, 'duplicate payload member')
        files[name] = (body, uid, mode)
    for path in sorted(bundle.rglob('*')):
        if not path.is_file(): continue
        relative = path.relative_to(bundle)
        if len(relative.parts) == 1 or relative.parts[0] in (node, 'support'):
            add('config/' + str(relative), path.read_bytes())
    for relative, entry in requirements.items():
        if entry['node'] != node: continue
        source = material / 'files' / relative
        require(not source.is_symlink() and source.is_file(), 'missing private material file')
        require(source.stat().st_uid == entry['uid'] and source.stat().st_mode & 0o777 == 0o400, 'private material permissions differ from the inventory')
        add('material/' + relative, source.read_bytes(), entry['uid'], 0o400)
        directories['material/' + str(Path(relative).parent)] = entry['uid']
    for service in compose['services'].values():
        for volume in service['volumes']:
            source = volume.split(':', 1)[0]
            if source.startswith(profile.prefix + '/data/' + node + '/') and not volume.endswith(':ro'):
                directories[source[len(profile.prefix) + 1:]] = int(service['user'].split(':')[0])
    metadata = {'format': 1, 'lab': state['lab'], 'material_id': state['id'], 'profile': profile.document(), 'node': node,
        'files': {name: {'sha256': hashlib.sha256(body).hexdigest(), 'uid': uid, 'mode': mode} for name, (body, uid, mode) in files.items()},
        'directories': directories}
    add('payload.json', (json.dumps(metadata, indent=2) + '\n').encode(), mode=0o400)
    if output.exists():
        require(output.is_file() and output.stat().st_uid == owner and output.stat().st_mode & 0o777 == 0o600,
                'unsafe cached node payload')
        with tarfile.open(output, 'r:gz') as cached:
            members = cached.getmembers()
            require(len(members) == len(files) and {member.name for member in members} == set(files), 'cached node payload member set differs')
            for member in members:
                body, uid, mode = files[member.name]
                require(member.isfile() and member.size == len(body) and member.uid == uid and member.gid == uid and member.mode == mode and member.mtime == FILE_MTIME,
                        'cached node payload metadata differs')
                require(hashlib.sha256(cached.extractfile(member).read()).digest() == hashlib.sha256(body).digest(),
                        'cached node payload belongs to different material or configuration')
        print('private node payload verified: ' + node)
        return
    try:
        with output.open('xb') as raw, gzip.GzipFile(filename='', mode='wb', fileobj=raw, mtime=0) as compressed:
            with tarfile.open(fileobj=compressed, mode='w|') as archive:
                for name, (body, uid, mode) in sorted(files.items()):
                    info = tarfile.TarInfo(name); info.size = len(body); info.uid = uid; info.gid = uid; info.mode = mode; info.mtime = FILE_MTIME
                    archive.addfile(info, io.BytesIO(body))
        output.chmod(0o600); os.chown(output, owner, owner)
    except BaseException:
        output.unlink(missing_ok=True)
        raise
    print('private node payload created: ' + node)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True); parser.add_argument('--material', required=True)
    parser.add_argument('--node', required=True); parser.add_argument('--output', required=True)
    parser.add_argument('--owner', type=int, required=True)
    parser.add_argument('--external', action='store_true')
    args = parser.parse_args()
    os.umask(0o077)
    package(args.bundle, args.material, args.node, args.output, args.owner, args.external)
