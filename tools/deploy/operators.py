"""Fetch checksum-pinned infrastructure operator charts and render them without a cluster."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
HELM = 'alpine/helm:3.20.1@sha256:9ccb6cb780553282592b8628e78c50cd914976d481d0d8fd1f362887194a9c4b'


def qualify():
    inventory = json.loads((ROOT / 'deploy/operators/operators.json').read_text())
    output = ROOT / 'build/deployment/operators'; output.mkdir(parents=True, exist_ok=True)
    report = []
    for entry in inventory['operators']:
        target = output / (entry['name'] + '-' + entry['version'] + '.tgz')
        if target.is_symlink(): raise ValueError('operator archive cannot be a symlink')
        if not target.exists():
            descriptor, name = tempfile.mkstemp(dir=output, prefix='.operator-')
            temporary = Path(name)
            try:
                with os.fdopen(descriptor, 'wb') as stream, urllib.request.urlopen(entry['url'], timeout=60) as response:
                    stream.write(response.read(32 * 1024**2 + 1))
                if temporary.stat().st_size > 32 * 1024**2: raise ValueError('operator chart exceeds size limit')
                if hashlib.sha256(temporary.read_bytes()).hexdigest() != entry['sha256']: raise ValueError('operator chart checksum mismatch')
                temporary.replace(target)
            finally: temporary.unlink(missing_ok=True)
        if hashlib.sha256(target.read_bytes()).hexdigest() != entry['sha256']: raise ValueError('cached operator chart checksum mismatch')
        command = ['docker', 'run', '--rm', '--network', 'none', '--user', str(os.getuid()) + ':' + str(os.getgid()),
                   '--read-only', '--cap-drop', 'ALL', '--security-opt', 'no-new-privileges:true', '--tmpfs', '/tmp:rw,size=64m',
                   '--mount', 'type=bind,src=' + str(output) + ',dst=/charts,readonly', HELM,
                   'template', 'psim-' + entry['name'], '/charts/' + target.name, '--include-crds', '--kube-version', inventory['kubernetes']]
        rendered = subprocess.check_output(command, text=True)
        if not rendered.strip(): raise ValueError('operator chart rendered no resources')
        (output / (entry['name'] + '.yaml')).write_text(rendered)
        report.append({'name': entry['name'], 'version': entry['version'], 'sha256': entry['sha256'], 'rendered': True})
        print('Operator checksum and rendering verified: ' + entry['name'], flush=True)
    (output / 'qualification.json').write_text(json.dumps({'operators': report, 'live_kubernetes_tested': False}, indent=2) + '\n')


if __name__ == '__main__': qualify()
