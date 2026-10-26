"""Package standard-library node installation tools as a signed release artifact."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import tarfile

ROOT = Path(__file__).resolve().parents[2]


def create(output, version):
    output = Path(output)
    sources = [ROOT / 'tools/deploy' / name for name in ('node_install.py', 'node_release.py', 'delivery.py', 'model.py')]
    sources += [ROOT / 'tools/version.py', ROOT / 'tools/release/verify.sh', ROOT / 'LICENSE']
    with tarfile.open(output, 'w', format=tarfile.PAX_FORMAT) as archive:
        for path in sorted(sources):
            body = path.read_bytes()
            info = tarfile.TarInfo(path.name); info.size = len(body); info.mode = 0o644; info.mtime = 0
            archive.addfile(info, io.BytesIO(body))
    component = {'type': 'file', 'name': 'psim-platform-installer', 'version': version,
        'licenses': [{'license': {'id': 'MIT'}}], 'hashes': [{'alg': 'SHA-256', 'content': hashlib.sha256(output.read_bytes()).hexdigest()}]}
    document = {'bomFormat': 'CycloneDX', 'specVersion': '1.6', 'version': 1,
                'metadata': {'component': component}, 'components': [], 'dependencies': []}
    output.with_name(output.name + '.cdx.json').write_text(json.dumps(document, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('output'); parser.add_argument('version'); args = parser.parse_args()
    create(args.output, args.version)
