"""Stage the native monitoring executable in a minimal pinned infrastructure image."""
import json
from pathlib import Path
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
base = json.loads((ROOT / 'deploy/topologies/images.json').read_text())['monitoring-build']
with tempfile.TemporaryDirectory(prefix='monitoring-image-') as directory:
    destination = Path(directory)
    shutil.copyfile(ROOT / 'deploy/infrastructure/monitoring/Dockerfile', destination / 'Dockerfile')
    shutil.copy2(ROOT / 'build/debian13/release/tools/monitoring-agent/psim-monitoring-agent', destination / 'psim-monitoring-agent')
    subprocess.run(['docker', 'build', '--build-arg', 'BASE=' + base, '-t', 'psim-platform/monitoring:0.1.0', directory], check=True)
