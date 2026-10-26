"""Prepare controlled additions to an existing installation without replacing existing identities."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time


def require(value, message):
    if not value: raise ValueError(message)


def run(*arguments, **kwargs):
    return subprocess.check_output(arguments, text=True, stderr=subprocess.STDOUT, **kwargs).strip()


def write(path, body, uid, mode):
    require(not path.is_symlink(), 'credential destination is a symlink')
    if path.exists():
        require(path.read_text() == body, 'existing credential differs from the requested addition')
        return
    descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, mode)
    with os.fdopen(descriptor, 'w') as stream:
        stream.write(body); stream.flush(); os.fsync(stream.fileno())
    os.chown(path, uid, uid); path.chmod(mode)


def prepare(prefix, node, action):
    prefix = Path(prefix)
    require(os.geteuid() == 0 and not prefix.is_symlink(), 'preparation requires root on the installed node')
    installed = json.loads((prefix / 'installation.json').read_text())
    require(installed['node'] == node, 'preparation target differs from the installed node')
    profile = json.loads((prefix / 'config/profile.json').read_text())
    compose = json.loads((prefix / 'config' / node / 'compose.json').read_text())
    def container(service):
        value = run('docker', 'compose', '--file', str(prefix / 'config' / node / 'compose.json'), 'ps', '--quiet', service)
        require(value, 'required preparation process is not running: ' + service)
        return value
    if action == 'proxy':
        path = prefix / 'material' / node / 'haproxy/haproxy.cfg'
        original = path.read_text()
        value = original
        if 'stats socket /tmp/psim-haproxy.sock' not in value:
            value = value.replace('global\n', 'global\n  stats socket /tmp/psim-haproxy.sock mode 600 level admin\n', 1)
            require(value != original, 'proxy global configuration is missing')
        if 'retry-on conn-failure empty-response 503' not in value:
            value = value.replace('backend public-api\n  mode http\n', 'backend public-api\n  mode http\n  retries 2\n  option redispatch\n  retry-on conn-failure empty-response 503\n  http-request disable-l7-retry unless { method GET HEAD }\n')
        if value != original:
            path.write_text(value)
            try:
                run('docker', 'exec', container('haproxy'), '/usr/local/sbin/haproxy', '-c', '-f', '/etc/psim-platform/haproxy/secrets/haproxy.cfg')
            except BaseException:
                path.write_text(original); raise
            run('docker', 'kill', '--signal', 'USR2', container('haproxy'))
        return
    if action == 'credentials':
        secrets = json.load(sys.stdin)
        require(set(secrets) == {'valkey', 'sentinel'} and all(len(value) >= 32 and '\n' not in value for value in secrets.values()), 'invalid fresh API credential set')
        if 'api-gateway' in compose['services']:
            uid = int(compose['services']['api-gateway']['user'].split(':')[0])
            directory = prefix / 'material' / node / 'api-gateway'
            for name, value in secrets.items(): write(directory / (name + '.secret'), value + '\n', uid, 0o400)
        for role, commands, patterns in [('valkey', '+ping +role +client|setname +eval +time +hmget +hset +pexpire', '~psim:11:api-gateway*6:bucket*'),
                                          ('sentinel', '+ping +client|setname +sentinel|get-master-addr-by-name', '')]:
            if role not in compose['services']: continue
            path = prefix / 'material' / node / role / 'users.acl'
            body = path.read_text()
            entry = 'user api-gateway on #' + hashlib.sha256(secrets[role].encode()).hexdigest() + ' ' + patterns + ' ' + commands
            previous = next((line for line in body.splitlines() if line.startswith('user api-gateway ')), None)
            require(previous is None or previous.split() == entry.split(), 'existing API ACL differs from the requested addition')
            if previous is None:
                path.write_text(body.rstrip() + '\n' + entry + '\n')
                with path.open('r') as stream: os.fsync(stream.fileno())
        return
    require(action == 'acl-load', 'unknown preparation action')
    operator = prefix / 'material' / node / 'deployment-admin'
    require(operator.is_dir(), 'ACL reload requires the installation operator identity')
    for target in [item for item in profile['nodes'] if 'infrastructure' in item['roles']]:
        for role, port in [('valkey', '6379'), ('sentinel', '26379')]:
            password = (operator / (role + '.secret')).read_text().strip()
            command = ['docker', 'exec', '-i', container('valkey'), 'sh', '-c',
                       'IFS= read -r VALKEYCLI_AUTH; export VALKEYCLI_AUTH; exec valkey-cli --no-auth-warning "$@"', 'sh',
                       '--user', 'deployment-admin', '--tls', '--cacert', '/etc/psim-platform/valkey/secrets/ca.pem',
                       '--cert', '/etc/psim-platform/valkey/secrets/identity.pem', '--key', '/etc/psim-platform/valkey/secrets/identity.key',
                       '-h', target['address'], '-p', port, 'ACL', 'LOAD']
            require(run(*command, input=password + '\n') == 'OK', 'ACL reload failed')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('action', choices=['proxy', 'credentials', 'acl-load'])
    parser.add_argument('--prefix', required=True); parser.add_argument('--node', required=True)
    args = parser.parse_args()
    prepare(args.prefix, args.node, args.action)
