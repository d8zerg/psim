"""Run asset, query and alert validation in the pinned Linux containers."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]


def run(*command):
    subprocess.run(command, cwd=ROOT, check=True)


def main():
    images = json.loads((ROOT / 'deploy/topologies/images.json').read_text())
    image = 'psim-platform/toolchain:debian13-' + hashlib.sha256((ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    mount = str(ROOT) + ':/repo'
    run('docker', 'run', '--rm', '--network', 'none', '--user', str(os.getuid()) + ':' + str(os.getgid()),
        '--volume', mount, '--workdir', '/repo', image, 'python3', '-I', 'tools/observability/test_package.py')
    for path in ['deploy/observability/rules.json', 'build/observability/dashboard-expressions.json']:
        run('docker', 'run', '--rm', '--network', 'none', '--volume', mount + ':ro', '--entrypoint', 'promtool',
            images['prometheus'], 'check', 'rules', '/repo/' + path)
    run('docker', 'run', '--rm', '--network', 'none', '--volume', mount + ':ro', '--entrypoint', 'promtool',
        '--workdir', '/repo/deploy/observability', images['prometheus'], 'test', 'rules', 'rule-tests.yaml')
    run('docker', 'run', '--rm', '--network', 'none', '--volume', mount + ':ro', '--entrypoint', '/bin/amtool',
        images['alertmanager'], 'check-config', '/repo/deploy/observability/alertmanager.yaml')
    run('docker', 'run', '--rm', '--network', 'none', '--volume', mount + ':ro', images['loki'],
        '-config.file=/repo/deploy/observability/loki.yaml', '-verify-config=true')
    run('docker', 'run', '--rm', '--network', 'none', '--volume', mount + ':ro', images['tempo'],
        '-config.file=/repo/deploy/observability/tempo.yaml', '-config.verify=true')
    name = 'psim-observability-sql-' + uuid.uuid4().hex[:10]
    run('docker', 'run', '--rm', '-d', '--network', 'none', '--name', name, '--volume', mount + ':ro',
        '--env', 'POSTGRES_HOST_AUTH_METHOD=trust', '--env', 'POSTGRES_DB=psim', images['postgres'])
    try:
        for _ in range(60):
            result = subprocess.run(['docker', 'exec', name, 'pg_isready', '--username=postgres', '--dbname=psim'], capture_output=True)
            if result.returncode == 0: break
            time.sleep(1)
        run('docker', 'exec', name, 'psql', '--no-psqlrc', '--set', 'ON_ERROR_STOP=1', '--username=postgres', '--dbname=psim',
            '--file=/repo/tools/observability/business-test.sql')
    finally:
        run('docker', 'stop', '--time', '5', name)


if __name__ == '__main__': main()
