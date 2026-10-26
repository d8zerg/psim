"""Build infrastructure wrappers from pinned bases in minimal temporary contexts."""
import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def build(version):
    images = json.loads((ROOT / 'deploy/topologies/images.json').read_text())
    output = ROOT / 'build/deployment'
    output.mkdir(parents=True, exist_ok=True)
    for name, base in [('postgres', 'postgres'), ('web', 'node'), ('keycloak', 'keycloak'), ('haproxy', 'haproxy'), ('registry', 'registry-build')]:
        with tempfile.TemporaryDirectory(prefix='image-' + name + '-', dir=output) as temporary:
            context = Path(temporary)
            shutil.copytree(ROOT / 'deploy/infrastructure' / name, context, dirs_exist_ok=True)
            if name == 'postgres':
                shutil.copy2(ROOT / 'build/debian13/release/services/audit-service/psim-audit-tool', context / 'psim-audit-tool')
            if name == 'web':
                source = ROOT / 'web/apps/console/dist'
                if not (source / 'index.html').is_file():
                    raise ValueError('build the console before building its runtime image')
                shutil.copytree(source, context / 'public')
            subprocess.run(['docker', 'build', '--build-arg', 'BASE=' + images[base], '-t', 'psim-platform/' + name + ':' + version, str(context)], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--version', required=True)
    build(parser.parse_args().version)
