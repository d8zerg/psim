"""Read application RSS and Kafka lag inside one owned VM without exporting secrets."""
import json
from pathlib import Path
import subprocess
import sys
import urllib.request


def sample(host, names):
    result = {'rss_bytes': {}, 'lag': 0, 'lag_samples': 0}
    for index, service in enumerate(names):
        identifiers = subprocess.check_output(['docker', 'ps', '--filter', 'label=com.docker.compose.service=' + service,
                                               '--format', '{{.ID}}'], text=True).splitlines()
        if len(identifiers) != 1:
            raise RuntimeError('expected exactly one running application container: ' + service)
        pid = subprocess.check_output(['docker', 'inspect', '--format', '{{.State.Pid}}', identifiers[0]], text=True).strip()
        rss = next(line.split()[1] for line in Path('/proc/' + pid + '/status').read_text().splitlines() if line.startswith('VmRSS:'))
        result['rss_bytes'][service] = int(rss) * 1024
        with urllib.request.urlopen('http://' + host + ':' + str(19100 + index) + '/metrics', timeout=5) as response:
            for line in response.read().decode().splitlines():
                if line.startswith('psim_kafka_consumer_lag{'):
                    result['lag'] += float(line.split()[-1])
                    result['lag_samples'] += 1
    if not result['lag_samples']:
        raise RuntimeError('Kafka lag samples are missing')
    print(json.dumps(result))


if __name__ == '__main__':
    sample(sys.argv[1], sys.argv[2:])
