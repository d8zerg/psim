"""Retain a signed baseline from installed image IDs and prepare a signed qualification candidate."""
import argparse
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import SERVICES, require
from release_manifest import create
import lab

ROOT = Path(__file__).resolve().parents[2]


def toolchain(*arguments):
    image = 'psim-platform/toolchain:debian13-' + __import__('hashlib').sha256((ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    return subprocess.run(['docker', 'run', '--rm', '--user', f'{os.getuid()}:{os.getgid()}', '--volume', str(ROOT) + ':/repo',
        '--workdir', '/repo', image, *arguments], check=True)


def prepare(candidate, packages, output):
    state = lab.read_state()
    require(state is not None and state['status'] == 'ready', 'qualification fixtures require an installed baseline')
    bundle, _ = lab.paths(state['topology'])
    profile = json.loads((bundle / 'profile.json').read_text())
    version = profile['version']
    output = Path(output); require(not output.exists(), 'baseline release directory already exists')
    output.mkdir(parents=True)
    candidate = Path(candidate)
    for path in Path(packages).glob('*.deb'): shutil.copy2(path, output / path.name)
    revision = None
    for binary in sorted({value[0] for value in SERVICES.values()}):
        image = 'psim-platform/psim-' + binary + ':' + version
        inspected = json.loads(subprocess.check_output(['docker', 'image', 'inspect', image], text=True))[0]
        require(inspected['Id'] == state['images'][image], 'baseline image differs from the accepted installation')
        current_revision = inspected['Config']['Labels']['org.opencontainers.image.revision']
        current_revision = subprocess.check_output(['git', 'rev-parse', current_revision], text=True).strip()
        require(revision in (None, current_revision), 'baseline images have different source revisions')
        revision = current_revision
        archive = output / ('psim-platform-psim-' + binary + '_' + version + '_image.tar')
        subprocess.run(['docker', 'image', 'save', '--output', str(archive), image], check=True)
        os_bom = ROOT / 'build/debian13/release/images' / ('psim-' + binary) / 'baseline-os.cdx.json'
        trivy = re.search(r'^  TRIVY_IMAGE: (\S+)$', (ROOT / 'Taskfile.yml').read_text(), re.MULTILINE)[1]
        subprocess.run(['docker', 'run', '--rm', '--user', f'{os.getuid()}:{os.getgid()}', '--volume', str(ROOT) + ':/repo',
            '--volume', str(ROOT / '.cache/trivy') + ':/cache', '--workdir', '/repo', trivy, 'image', '--quiet', '--cache-dir', '/cache',
            '--format', 'cyclonedx', '--input', str(archive.relative_to(ROOT)), '--output', str(os_bom.relative_to(ROOT))], check=True)
        toolchain('python3', '-I', 'tools/release/sbom.py', 'artifact', 'build/debian13/release/app.cdx.json',
                  str(archive.relative_to(ROOT)), 'image', image.split(':')[0], version, str(archive.relative_to(ROOT)) + '.cdx.json', str(os_bom.relative_to(ROOT)))
    for archive in output.glob('*.deb'):
        name = archive.name.split('_')[0]
        toolchain('python3', '-I', 'tools/release/sbom.py', 'artifact', 'build/debian13/release/app.cdx.json',
                  str(archive.relative_to(ROOT)), 'deb', name, version, str(archive.relative_to(ROOT)) + '.cdx.json')
    snapshot = json.loads((candidate / 'contracts.json').read_text()); snapshot['admin_info_fields'].remove('configuration_format')
    contract = output.parent / 'baseline-contracts.json'; contract.write_text(json.dumps(snapshot, indent=2) + '\n')
    create(output, version, version, revision, contract, 1)
    key = '/repo/.cache/keys/psim-dev.pem'
    toolchain('sh', 'tools/release/sign.sh', str(output.relative_to(ROOT)), key)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--candidate', required=True); parser.add_argument('--packages', required=True); parser.add_argument('--output', required=True)
    args = parser.parse_args()
    prepare(Path(args.candidate).resolve(), Path(args.packages).resolve(), Path(args.output).resolve())
