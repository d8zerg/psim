"""Evaluate complete load evidence without accepting shortened runs or drained throughput."""
import math
from collections import Counter


PROFILES = {
    'steady': (50000, 600),
    'soak': (50000, 600),
    'peak': (75000, 300),
    'nightly': (5000, 60),
}


def percentile(values, fraction=0.99):
    if not values or any(not math.isfinite(value) or value < 0 for value in values):
        raise ValueError('latency samples must be finite, nonnegative and nonempty')
    return sorted(values)[math.ceil(len(values) * fraction) - 1]


def evaluate(profile, publisher, observation, queries, reconciliation, recovery_seconds, browser, resources, expected_clients=100):
    rate, duration = PROFILES[profile]
    checks = {}
    checks['duration'] = publisher['seconds'] >= duration
    checks['acknowledged_rate'] = publisher['acknowledged_before_drain'] / publisher['seconds'] >= rate
    checks['sdk_drained'] = publisher['published'] == publisher['acknowledged'] and publisher['pending'] == publisher['rejected'] == 0
    clients = publisher.get('clients', [])
    checks['independent_clients'] = (expected_clients > 1 and publisher.get('client_count') == expected_clients
        and len(clients) == len({row['connector'] for row in clients}) == expected_clients
        and {row['client'] for row in clients} == set(range(expected_clients)))
    checks['client_accounting'] = (sum(row['target_rate'] for row in clients) == rate
        and all(sum(row[field] for row in clients) == publisher[field]
                for field in ('published', 'acknowledged', 'acknowledged_before_drain', 'pending', 'rejected')))
    checks['client_throughput'] = bool(clients) and all(row['seconds'] >= duration and
        row['acknowledged_before_drain'] / row['seconds'] >= row['target_rate'] and
        row['published'] == row['acknowledged'] and row['pending'] == row['rejected'] == 0 for row in clients)
    checks['client_queues'] = bool(clients) and all(stable_client_queue(row['intervals']) for row in clients)
    owners = Counter(row.get('connector') for row in publisher['source_ranges'].values())
    checks['source_ownership'] = (len(publisher['source_ranges']) == 10000 and
        set(owners) == {row['connector'] for row in clients} and
        all(owners[row['connector']] == row['virtual_devices'] == 10000 // expected_clients for row in clients) and
        sum(row['count'] for row in publisher['source_ranges'].values()) == publisher['published'])
    checks['reconciliation'] = reconciliation
    checks['signals_and_incidents'] = (observation['signals'] == observation['incidents'] == publisher['signals_expected']
                                      and observation['signals'] >= duration - 1 and observation['pending'] == observation['duplicates'] == 0)
    distribution = publisher['site_background_events']
    checks['profile'] = (publisher['virtual_devices'] == 10000 and publisher['sites'] == 10
                         and len(publisher['source_ranges']) == 10000 and len(distribution) == 10
                         and sum(distribution) == publisher['published'] - publisher['signals_expected']
                         and all(value > 0 for value in distribution)
                         and abs(distribution[0] / sum(distribution) - 0.3) <= 0.001)
    checks['peak_recovery'] = recovery_seconds <= 300
    if profile != 'peak':
        windows, seconds, acknowledged = [], 0, 0
        for interval in publisher['intervals']:
            seconds += interval['duration_seconds']
            acknowledged += interval['acknowledged']
            if seconds >= 10:
                windows.append(acknowledged / seconds)
                seconds, acknowledged = 0, 0
        checks['sustained_windows'] = len(windows) >= duration // 11 - 1 and all(value >= rate for value in windows)
        checks['signal_latency'] = observation['signal_p99_ms'] <= 300 and observation['maximum_window_signal_p99_ms'] <= 300
        checks['incident_latency'] = observation['incident_p99_ms'] <= 1000 and observation['maximum_window_incident_p99_ms'] <= 1000
    for name, limit in [('console', 200), ('history', 500), ('report', 3000)]:
        samples = [row['milliseconds'] for row in queries if row['kind'] == name and row['ok']]
        checks[name + '_requests'] = len(samples) >= max(30, duration // 10)
        checks[name + '_errors'] = all(row['ok'] for row in queries if row['kind'] == name)
        checks[name + '_latency'] = bool(samples) and percentile(samples) <= limit
    origins = observation['incident_origins_ms']
    renders = {}
    for key, at in browser['renders'].items():
        identity = key.rsplit(':', 1)[0]
        renders[identity] = min(renders.get(identity, at), at)
    delays = [renders[identity] - at for identity, at in origins.items() if identity in renders]
    checks['browser_completeness'] = len(delays) == len(origins) == publisher['signals_expected']
    checks['browser_rendering'] = bool(delays) and percentile(delays) <= 1000
    checks['consoles'] = browser['clients'] == 500 and browser['authenticated'] and not browser['errors']
    expected_messages = set(observation['incident_message_ids'].values())
    deliveries = [row for row in browser['realtime'] if row['event'] in expected_messages]
    realtime = [row['milliseconds'] for row in deliveries]
    delivered = {(row['client'], row['event']) for row in deliveries}
    checks['realtime_samples'] = (len(expected_messages) == publisher['signals_expected']
                                  and len(deliveries) == len(delivered) == len(expected_messages) * 499
                                  and all(0 <= client < 499 for client, _ in delivered))
    checks['realtime_latency'] = bool(realtime) and percentile(realtime) <= 200
    if profile == 'soak':
        checks['memory_and_lag_stability'] = stable_resources(resources, duration)
    return {'checks': checks, 'passed': all(checks.values()), 'browser_p99_ms': percentile(delays) if delays else None}


def stable_client_queue(intervals):
    if len(intervals) < 20:
        return False
    early = intervals[:10]
    late = intervals[-10:]
    return sum(row['pending'] for row in late) / len(late) <= sum(row['pending'] for row in early) / len(early) + 2000


def stable_resources(resources, duration):
    if len(resources) < duration // 15 or resources[-1]['seconds'] - resources[0]['seconds'] < duration - 30:
        return False
    early, late = resources[:len(resources) // 3], resources[-len(resources) // 3:]
    names = set(resources[0]['rss_bytes'])
    if not names or any(set(row['rss_bytes']) != names for row in resources):
        return False
    for name in names:
        before = sum(row['rss_bytes'][name] for row in early) / len(early)
        after = sum(row['rss_bytes'][name] for row in late) / len(late)
        if after > before * 1.1 + 64 * 1024 * 1024:
            return False
    before = sum(row['lag'] for row in early) / len(early)
    after = sum(row['lag'] for row in late) / len(late)
    return after <= before + 50000
