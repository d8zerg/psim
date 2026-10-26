"""Run scoped installation clients inside the owned VM deployment."""
import argparse
import json
from pathlib import Path
import sys
import subprocess

sys.path.insert(0, str(Path(__file__).resolve().parent))
import local
from material import verify_bundle


def bootstrap(bundle, operation, owner='deployment-admin', **kwargs):
    bundle = Path(bundle)
    profile = verify_bundle(bundle)
    node = profile.select('infrastructure')[0]
    images = json.loads((local.ROOT / 'deploy/topologies/images.json').read_text())
    command = ['sudo', 'docker', 'run', '--rm', '--network', 'host', '--read-only', '--cap-drop', 'ALL',
               '--security-opt', 'no-new-privileges:true', '--user', str(profile.uid(owner)) + ':' + str(profile.uid(owner)),
               '--label', 'psim.deployment=' + profile.name, '--tmpfs', '/tmp:rw,nosuid,size=128m,mode=1777', '--env', 'LD_PRELOAD=',
               '--mount', 'type=bind,src=' + profile.prefix + '/material/' + node.name + '/' + owner + ',dst=/operator,readonly',
               '--mount', 'type=bind,src=' + profile.prefix + '/config,dst=/config,readonly']
    for line in (bundle / node.name / 'hosts').read_text().splitlines():
        address, *names = line.split()
        for name in names:
            if name != 'localhost': command += ['--add-host', name + ':' + address]
    if operation == 'kafka':
        command += ['--entrypoint', '/bin/sh', images['kafka'], '/operator/kafka-bootstrap.sh']
    else:
        command += ['--entrypoint', 'python3', 'psim-platform/postgres:' + profile.version, '/config/support/setup.py', operation]
    return local.execute(node.name, command, **kwargs)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--bundle', required=True)
    parser.add_argument('--owner', default='deployment-admin')
    parser.add_argument('operation', choices=['kafka', 'schemas', 'bucket', 'core', 'identity', 'identity-journals', 'identity-audit', 'applications', 'schema-reader'])
    args = parser.parse_args()
    try:
        bootstrap(args.bundle, args.operation, args.owner)
    except subprocess.CalledProcessError as error:
        raise SystemExit(error.returncode) from None
