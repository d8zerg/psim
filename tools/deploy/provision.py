"""Prepare external PKI requests, import signed identities and export private node payloads."""
import argparse
import os
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local
from material import verify_bundle
from model import require


def run(args):
    bundle = Path(args.bundle).resolve(); private = Path(args.private).resolve()
    profile = verify_bundle(bundle)
    require(not private.is_relative_to(bundle), 'private material cannot be placed inside a public deployment bundle')
    private.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
    require(private.parent.stat().st_uid == os.getuid(), 'private material parent belongs to another account')
    command = ['docker', 'run', '--rm', '--network', 'none', '--read-only', '--user', '0:0',
        '--cap-drop', 'ALL', '--cap-add', 'CHOWN', '--cap-add', 'DAC_OVERRIDE', '--security-opt', 'no-new-privileges:true',
        '--env', 'LD_PRELOAD=', '--tmpfs', '/tmp:rw,nosuid,size=32m,mode=1777',
        '--mount', 'type=bind,src=' + str(local.ROOT) + ',dst=/repo,readonly',
        '--mount', 'type=bind,src=' + str(bundle) + ',dst=/bundle,readonly',
        '--mount', 'type=bind,src=' + str(private.parent) + ',dst=/private', '--entrypoint', 'python3']
    image = 'psim-platform/postgres:' + profile.image_version
    material = '/private/' + private.name
    base = [image, '/repo/tools/deploy/material.py', '--bundle', '/bundle', '--output', material]
    if args.action in ('request', 'export'):
        require(args.requests, 'request export directory is required')
        output = Path(args.requests).resolve()
        require(not output.exists(), 'request export destination exists')
        output.mkdir(parents=True, mode=0o700)
        if args.action == 'request': local.vm.run(command + base + ['--external-request'])
        export = '''import os,pathlib,shutil,sys
source=pathlib.Path(sys.argv[1])/'requests'; target=pathlib.Path('/requests'); uid=int(sys.argv[2])
assert source.is_dir() and not any(target.iterdir())
for path in source.rglob('*'):
 assert not path.is_symlink()
 if not path.is_file(): continue
 assert path.suffix=='.csr'
 destination=target/path.relative_to(source); destination.parent.mkdir(parents=True,exist_ok=True,mode=0o700)
 shutil.copyfile(path,destination); destination.chmod(0o400)
for path in [target,*target.rglob('*')]: os.chown(path,uid,uid)
print('Public certificate requests exported; no private keys exported')
'''
        command[2:2] = ['--mount', 'type=bind,src=' + str(output) + ',dst=/requests']
        local.vm.run(command + [image, '-c', export, material, str(os.getuid())])
    elif args.action == 'complete':
        require(args.trust and args.signed, 'trust and signed certificate directories are required')
        for source, target in ((args.trust, '/trust'), (args.signed, '/signed')):
            path = Path(source).resolve(); require(path.is_dir(), 'certificate input directory is absent')
            command[2:2] = ['--mount', 'type=bind,src=' + str(path) + ',dst=' + target + ',readonly']
        local.vm.run(command + base + ['--external-complete', '--trust-directory', '/trust', '--signed-directory', '/signed'])
    else:
        require(args.payloads, 'node payload destination is required')
        output = Path(args.payloads).resolve(); output.mkdir(parents=True, exist_ok=True, mode=0o700)
        require(not output.is_relative_to(bundle), 'private node payloads cannot be placed inside a public bundle')
        command[2:2] = ['--mount', 'type=bind,src=' + str(output) + ',dst=/payloads']
        for node in profile.nodes:
            local.vm.run(command + [image, '/repo/tools/deploy/archive.py', '--bundle', '/bundle', '--material', material,
                '--node', node.name, '--output', '/payloads/' + node.name + '.tar.gz', '--owner', str(os.getuid()), '--external'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['request', 'export', 'complete', 'package'])
    parser.add_argument('--bundle', required=True); parser.add_argument('--private', required=True)
    parser.add_argument('--requests'); parser.add_argument('--trust'); parser.add_argument('--signed'); parser.add_argument('--payloads')
    run(parser.parse_args())
