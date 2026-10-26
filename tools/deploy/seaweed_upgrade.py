"""Exercise an exact SeaweedFS version transition on isolated quorum, volume and filer processes."""
import argparse
import datetime
import hashlib
import hmac
import json
import os
from pathlib import Path
import secrets
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

sys.path.insert(0, str(Path(__file__).resolve().parent))
ROOT = Path(__file__).resolve().parents[2]
PROJECT = 'psim-seaweed-upgrade-qualification'


def require(value, message):
    if not value: raise ValueError(message)


def request(configuration, method, path, body=b'', anonymous=False):
    host = 'proxy:8333'
    now = datetime.datetime.now(datetime.timezone.utc)
    day, timestamp = now.strftime('%Y%m%d'), now.strftime('%Y%m%dT%H%M%SZ')
    content = hashlib.sha256(body).hexdigest()
    headers = {'host': host, 'x-amz-content-sha256': content, 'x-amz-date': timestamp}
    names = ';'.join(sorted(headers))
    canonical = '\n'.join([method, path, '', ''.join(key + ':' + headers[key] + '\n' for key in sorted(headers)), names, content])
    scope = day + '/us-east-1/s3/aws4_request'
    value = '\n'.join(['AWS4-HMAC-SHA256', timestamp, scope, hashlib.sha256(canonical.encode()).hexdigest()])
    key = ('AWS4' + configuration['secret']).encode()
    for part in (day, 'us-east-1', 's3', 'aws4_request'): key = hmac.new(key, part.encode(), hashlib.sha256).digest()
    if not anonymous:
        headers['Authorization'] = 'AWS4-HMAC-SHA256 Credential=' + configuration['access'] + '/' + scope + ', SignedHeaders=' + names + ', Signature=' + hmac.new(key, value.encode(), hashlib.sha256).hexdigest()
    with urllib.request.urlopen(urllib.request.Request('http://' + host + path, data=body if method == 'PUT' else None,
                                headers=headers, method=method), timeout=5) as response:
        return response.status, response.read()


def probe():
    case = Path('/case')
    configuration = json.loads((case / 'client.json').read_text())
    body = b'psim-floor-plan-content' * 1024
    deadline = time.monotonic() + 90
    while True:
        try:
            try: request(configuration, 'PUT', '/psim-plans')
            except urllib.error.HTTPError as error:
                if error.code != 409: raise
            request(configuration, 'PUT', '/psim-plans/current.png', body)
            break
        except (OSError, urllib.error.URLError):
            if time.monotonic() >= deadline: raise
            time.sleep(0.5)
    (case / 'ready').touch()
    checks = errors = 0
    while not (case / 'stop').exists():
        try:
            status, actual = request(configuration, 'GET', '/psim-plans/current.png')
            require(status == 200 and hashlib.sha256(actual).digest() == hashlib.sha256(body).digest(), 'floor object changed')
        except Exception as error:
            errors += 1
            print('floor read failed: ' + type(error).__name__, flush=True)
        checks += 1; time.sleep(0.05)
    request(configuration, 'PUT', '/psim-plans/next.png', body + b'next')
    require(request(configuration, 'GET', '/psim-plans/next.png')[1] == body + b'next', 'new object differs after upgrade')
    try:
        request(configuration, 'GET', '/psim-plans/current.png', anonymous=True)
        raise ValueError('anonymous read unexpectedly succeeded')
    except urllib.error.HTTPError as error:
        require(error.code == 403, 'anonymous access returned an unexpected status')
    (case / 'result.json').write_text(json.dumps({'reads': checks, 'errors': errors, 'new_object': True, 'anonymous_status': 403}))
    require(checks > 0 and errors == 0, 'floor reads failed during the vendor version transition')


def qualify():
    matrix = json.loads((ROOT / 'deploy/infrastructure/seaweed-upgrade.json').read_text())
    images = json.loads((ROOT / 'deploy/topologies/images.json').read_text())
    directory = ROOT / 'build/deployment/seaweed-upgrade'
    require(not directory.exists(), 'isolated upgrade directory already exists; retain or remove the previous owned report first')
    require(not subprocess.check_output(['docker', 'ps', '--all', '--quiet', '--filter', 'label=com.docker.compose.project=' + PROJECT], text=True).strip(), 'isolated project name is already in use')
    directory.mkdir(mode=0o700)
    case = directory / 'case'; case.mkdir(mode=0o700)
    runtime = directory / 'runtime'; runtime.mkdir(mode=0o700)
    client = {'access': secrets.token_urlsafe(20), 'secret': secrets.token_urlsafe(32)}
    (case / 'client.json').write_text(json.dumps(client)); (case / 'client.json').chmod(0o400)
    (directory / 'postgres.secret').write_text(secrets.token_urlsafe(32)); (directory / 'postgres.secret').chmod(0o400)
    (directory / 'filer.toml').write_text('[postgres2]\nenabled=true\nhostname="postgres"\nport=5432\nusername="seaweed"\npassword="' + (directory / 'postgres.secret').read_text() + '"\ndatabase="seaweed"\nschema="public"\nsslmode="disable"\n')
    (directory / 'filer.toml').chmod(0o400)
    (directory / 'iam.json').write_text(json.dumps({'identities': [{'name': 'qualification',
        'credentials': [{'accessKey': client['access'], 'secretKey': client['secret']}],
        'actions': ['Admin:psim-plans', 'Read:psim-plans', 'Write:psim-plans', 'List:psim-plans']}]}))
    (directory / 'iam.json').chmod(0o400)
    services = {}
    uid = f'{os.getuid()}:{os.getgid()}'
    def service(name, image, command, volumes=()):
        data = directory / ('data-' + name); data.mkdir(mode=0o700)
        services[name] = {'image': image, 'user': uid, 'read_only': True, 'cap_drop': ['ALL'],
            'security_opt': ['no-new-privileges:true'], 'tmpfs': ['/tmp:rw,nosuid,size=128m,mode=1777'],
            'volumes': [str(data) + ':/data', *volumes], 'command': command, 'stop_grace_period': '30s'}
        return services[name]
    pg = service('postgres', images['postgres'], [])
    pg['tmpfs'].append('/var/run/postgresql:rw,nosuid,size=16m,mode=1777')
    pg['volumes'] += [str(directory / 'postgres.secret') + ':/run/postgres.secret:ro']
    pg['environment'] = {'POSTGRES_USER': 'seaweed', 'POSTGRES_DB': 'seaweed', 'POSTGRES_PASSWORD_FILE': '/run/postgres.secret', 'PGDATA': '/data/postgres'}
    pg.pop('command')
    pg['healthcheck'] = {'test': ['CMD', 'pg_isready', '-U', 'seaweed', '-d', 'seaweed'], 'interval': '1s', 'timeout': '3s', 'retries': 60}
    peers = ','.join('master-' + str(index) + ':9333' for index in range(1, 4))
    for index in range(1, 4):
        name = 'master-' + str(index)
        service(name, matrix['baseline'], ['master', '-ip=' + name, '-mdir=/data', '-peers=' + peers, '-defaultReplication=010'])['entrypoint'] = ['/usr/bin/weed']
    for index in range(1, 3):
        name = 'volume-' + str(index)
        service(name, matrix['baseline'], ['volume', '-ip=' + name, '-dir=/data', '-max=4', '-mserver=' + peers,
            '-dataCenter=qualification', '-rack=rack-' + str(index)])['entrypoint'] = ['/usr/bin/weed']
        name = 'filer-' + str(index)
        filer = service(name, matrix['baseline'], ['filer', '-ip=' + name, '-master=' + peers, '-defaultReplicaPlacement=010'],
                        [str(directory / 'filer.toml') + ':/etc/seaweedfs/filer.toml:ro'])
        filer['entrypoint'] = ['/usr/bin/weed']; filer['depends_on'] = {'postgres': {'condition': 'service_healthy'}}
        name = 's3-' + str(index)
        service(name, matrix['baseline'], ['s3', '-port=8333', '-filer=filer-1:8888,filer-2:8888', '-config=/config/iam.json'],
                [str(directory / 'iam.json') + ':/config/iam.json:ro'])['entrypoint'] = ['/usr/bin/weed']
    configuration = directory / 'haproxy.cfg'
    configuration.write_text('global\n  stats socket /runtime/admin.sock mode 600 level admin\nresolvers docker\n  nameserver dns 127.0.0.11:53\n  hold valid 1s\n  hold nx 1s\ndefaults\n  mode http\n  timeout connect 2s\n  timeout client 10s\n  timeout server 10s\nlisten s3\n  bind :8333\n  balance roundrobin\n  server s3-1 s3-1:8333 check inter 200ms fall 1 rise 1 resolvers docker init-addr last,libc,none\n  server s3-2 s3-2:8333 check inter 200ms fall 1 rise 1 resolvers docker init-addr last,libc,none\n')
    service('proxy', images['haproxy'], ['haproxy', '-W', '-db', '-f', '/config/haproxy.cfg'],
            [str(configuration) + ':/config/haproxy.cfg:ro', str(runtime) + ':/runtime'])
    compose = directory / 'compose.json'; compose.write_text(json.dumps({'services': services}, indent=2))
    command = ['docker', 'compose', '--project-name', PROJECT, '--file', str(compose)]
    toolchain = 'psim-platform/toolchain:debian13-' + hashlib.sha256((ROOT / 'tools/toolchain/Dockerfile').read_bytes()).hexdigest()[:12]
    probe_command = ['docker', 'run', '--rm', '--network', PROJECT + '_default', '--user', uid, '--read-only', '--cap-drop', 'ALL',
        '--security-opt', 'no-new-privileges:true', '--volume', str(ROOT) + ':/repo:ro', '--volume', str(case) + ':/case',
        '--workdir', '/repo', toolchain, 'python3', '-I', 'tools/deploy/seaweed_upgrade.py', 'probe']
    process = None
    try:
        subprocess.run(command + ['up', '-d'], check=True)
        with (directory / 'probe.log').open('w') as log:
            process = subprocess.Popen(probe_command, stdout=log, stderr=subprocess.STDOUT, text=True)
            deadline = time.monotonic() + 120
            while not (case / 'ready').exists():
                require(process.poll() is None and time.monotonic() < deadline, 'isolated S3 fixture did not become ready')
                time.sleep(0.5)
            for name in matrix['order']:
                if name.startswith('s3-'):
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                        connection.connect(str(runtime / 'admin.sock')); connection.sendall(('set server s3/' + name + ' state drain\n').encode())
                services[name]['image'] = matrix['candidate']; compose.write_text(json.dumps({'services': services}, indent=2))
                subprocess.run(command + ['up', '-d', '--no-deps', '--force-recreate', name], check=True)
                time.sleep(3)
                require(process.poll() is None, 'isolated floor probe failed during replacement')
                if name.startswith('s3-'):
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
                        connection.connect(str(runtime / 'admin.sock')); connection.sendall(('set server s3/' + name + ' state ready\n').encode())
            (case / 'stop').touch(); require(process.wait(timeout=30) == 0, 'isolated floor probe reported errors')
        report = json.loads((case / 'result.json').read_text()) | {'baseline': matrix['baseline'], 'candidate': matrix['candidate'], 'order': matrix['order']}
        topology = subprocess.run(command + ['exec', '-T', 'master-1', '/usr/bin/weed', 'shell', '-master=' + peers],
                                  input='volume.list\n', capture_output=True, text=True, check=True).stdout
        from storage import volumes
        report['replicated_volumes'] = len(volumes(topology))
        (ROOT / 'build/deployment/seaweed-upgrade-qualification.json').write_text(json.dumps(report, indent=2) + '\n')
    finally:
        if process is not None and process.poll() is None:
            (case / 'stop').touch()
            try: process.wait(timeout=15)
            except subprocess.TimeoutExpired: process.terminate(); process.wait(timeout=10)
        logs = subprocess.run(command + ['logs', '--no-color', '--tail', '50'], text=True, capture_output=True)
        (directory / 'servers.log').write_text(logs.stdout + logs.stderr)
        subprocess.run(command + ['down', '--volumes', '--remove-orphans'], check=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=['qualify', 'probe']); args = parser.parse_args()
    if args.action == 'probe': probe()
    else: qualify()
