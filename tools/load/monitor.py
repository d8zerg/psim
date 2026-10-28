"""Measure public reads concurrently with SDK publication inside the qualification container."""
import datetime as dt
import json
from pathlib import Path
import sys
import time
import urllib.parse

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'deploy'))
from scenario import API, CASE


def run():
    api = API('https://localhost:8443')
    state = json.loads((CASE / 'scenario.json').read_text())
    now = dt.datetime.now(dt.timezone.utc)
    stamp = lambda value: value.isoformat(timespec='milliseconds').replace('+00:00', 'Z')
    paths = {
        'console': '/v1/feed/incidents',
        'history': '/v1/events?' + urllib.parse.urlencode({'from': stamp(now - dt.timedelta(days=1)),
            'to': stamp(now + dt.timedelta(hours=1)), 'siteIds': state['site'], 'pageSize': 100}),
        'report': '/v1/reports/incidents?' + urllib.parse.urlencode({'filter.from': stamp(now - dt.timedelta(days=30)),
            'filter.to': stamp(now + dt.timedelta(hours=1)), 'grouping': 'REPORT_GROUPING_SITE'}),
    }
    refreshed = time.monotonic()
    with (CASE / 'load-queries.jsonl').open('w') as output:
        print('READY', flush=True)
        while not (CASE / 'load-monitor.stop').exists():
            if time.monotonic() - refreshed >= 45:
                api = API(api.origin)
                refreshed = time.monotonic()
            for name, path in paths.items():
                started = time.monotonic()
                row = {'kind': name, 'ok': False, 'started_ms': time.time_ns() // 1000000}
                try:
                    value = api.request('GET', path)
                    if not isinstance(value, dict):
                        raise RuntimeError('expected JSON object')
                    row['ok'] = True
                except (RuntimeError, OSError) as error:
                    row['error'] = str(error)[:300]
                row['milliseconds'] = (time.monotonic() - started) * 1000
                row['finished_ms'] = time.time_ns() // 1000000
                output.write(json.dumps(row) + '\n')
                output.flush()
            time.sleep(0.5)


if __name__ == '__main__':
    run()
