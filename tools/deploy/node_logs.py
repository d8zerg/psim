"""Read one owned service's logs and redact local credential material before output."""
import argparse
import json
from pathlib import Path
import re
import subprocess


def logs(prefix, node, service, tail=100):
    prefix = Path(prefix)
    state = json.loads((prefix / 'installation.json').read_text())
    if state['node'] != node or not re.fullmatch(r'[a-z][a-z0-9-]+', service):
        raise ValueError('diagnostic identity mismatch')
    container = state['name'] + '-' + node + '-' + service + '-1'
    metadata = json.loads(subprocess.check_output(['docker', 'inspect', container], text=True))[0]
    if metadata['Config']['Labels'].get('psim.deployment') != state['name']:
        raise ValueError('diagnostic container ownership mismatch')
    if not 1 <= tail <= 100000: raise ValueError('invalid diagnostic line limit')
    result = subprocess.run(['docker', 'logs', '--tail', str(tail), container], text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output = result.stdout
    for path in (prefix / 'material' / node).rglob('*'):
        if path.is_file() and not path.is_symlink() and path.suffix in ('.secret', '.access'):
            value = path.read_text().strip()
            if value: output = output.replace(value, '<redacted>')
    output = re.sub(r'-----BEGIN (?:EC |RSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----.*?-----END (?:EC |RSA |OPENSSH |ENCRYPTED )?PRIVATE KEY-----', '<redacted-private-key>', output, flags=re.S)
    output = re.sub(r"(?i)(PASSWORD\s+)'[^']*'", r"\1'<redacted>'", output)
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--prefix', required=True); parser.add_argument('--node', required=True); parser.add_argument('--service', required=True)
    parser.add_argument('--tail', type=int, default=100)
    args = parser.parse_args(); logs(args.prefix, args.node, args.service, args.tail)
