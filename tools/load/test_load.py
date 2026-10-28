"""Negative qualification evidence must never pass the load gate."""
import copy
import sys
from pathlib import Path
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
from evaluate import evaluate, percentile, stable_resources
from sizing import calculate
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'deploy'))
import scenario


class CatalogDistributionTest(unittest.TestCase):
    def fixture(self, missing=False):
        connectors = ['connector' + str(index) for index in range(100)]
        rows = {str(site): [dict(deviceId=str(site) + '-' + str(device), deviceTypeId='type',
            siteId=str(site), zoneId='zone', connectorId=connectors[0], externalId='load-' + str(site) + '-' + str(device),
            name='Device', version=7, sources=[dict(externalRef='load/' + str(site) + '/' + str(device),
            sourceId='old-source', kind='alarm', name='Alarm')]) for device in range(1000) if site or device]
            for site in range(10)}
        if missing: rows['9'].pop()
        writes = []
        class API:
            def request(self, method, path, body=None, version=None):
                if method == 'GET':
                    site = scenario.urllib.parse.parse_qs(path.split('?', 1)[1])['siteId'][0]
                    return {'devices': rows[site]}
                writes.append((path, body, version))
        return API(), dict(connector=connectors[0], load_sites=list(rows)), connectors, writes

    def test_redistribution_preserves_devices_and_versions(self):
        api, state, connectors, writes = self.fixture()
        with patch.object(scenario, 'save') as save:
            scenario.redistribute_load(api, state, connectors)
        self.assertEqual(len(writes), 9900)
        self.assertEqual(len({body['deviceId'] for _, body, _ in writes}), 9900)
        for path, body, version in writes:
            self.assertEqual(path, '/v1/devices/' + body['deviceId'])
            self.assertEqual(version, 7)
            self.assertEqual(body['connectorId'], connectors[int(body['externalId'].rsplit('-', 1)[1]) % 100])
            self.assertNotIn('sourceId', body['sources'][0])
        save.assert_called_once_with(state)

    def test_incomplete_catalog_is_rejected_before_any_update(self):
        api, state, connectors, writes = self.fixture(missing=True)
        with self.assertRaisesRegex(RuntimeError, 'incomplete'):
            scenario.redistribute_load(api, state, connectors)
        self.assertEqual(writes, [])


class EvidenceTest(unittest.TestCase):
    def setUp(self):
        self.publisher = {'seconds': 600, 'acknowledged_before_drain': 30000000, 'published': 30000000,
                          'acknowledged': 30000000, 'pending': 0, 'rejected': 0, 'signals_expected': 600,
                          'virtual_devices': 10000, 'sites': 10,
                          'source_ranges': {str(index): {'connector': 'connector' + str(index % 100), 'count': 3000} for index in range(10000)},
                          'site_background_events': [8999820] + [2333287] * 6 + [2333286] * 3,
                          'intervals': [{'duration_seconds': 10, 'acknowledged': 500000}] * 60}
        self.publisher['client_count'] = 100
        self.publisher['clients'] = [dict(client=index, connector='connector' + str(index), target_rate=500, virtual_devices=100,
            seconds=600, published=300000, acknowledged=300000, acknowledged_before_drain=300000,
            pending=0, rejected=0, intervals=[{'pending': 0}] * 60) for index in range(100)]
        self.observation = {'signals': 600, 'incidents': 600, 'pending': 0, 'duplicates': 0, 'signal_p99_ms': 100,
                            'maximum_window_signal_p99_ms': 100,
                            'incident_p99_ms': 200, 'maximum_window_incident_p99_ms': 200,
                            'incident_message_ids': {str(index): 'message' + str(index) for index in range(600)},
                            'incident_origins_ms': {'event' + str(index): 1000 for index in range(600)}}
        self.queries = [{'kind': kind, 'ok': True, 'milliseconds': 50} for kind in ('console', 'history', 'report') for _ in range(60)]
        self.browser = {'renders': {'event' + str(index) + ':1': 1250 for index in range(600)},
                        'clients': 500, 'errors': [], 'authenticated': True,
                        'realtime': [{'milliseconds': 50, 'client': client, 'event': 'message' + str(index)}
                                     for client in range(499) for index in range(600)]}

    def result(self):
        return evaluate('steady', self.publisher, self.observation, self.queries, True, 10, self.browser, [])

    def test_complete_evidence(self):
        self.assertTrue(self.result()['passed'])

    def test_drain_cannot_repair_rate(self):
        self.publisher['acknowledged_before_drain'] -= 1
        self.assertFalse(self.result()['passed'])

    def test_shortened_interval(self):
        self.publisher['seconds'] = 599
        self.assertFalse(self.result()['passed'])

    def test_stalled_window(self):
        self.publisher['intervals'] = [{'duration_seconds': 10, 'acknowledged': 0}] + self.publisher['intervals'][1:]
        self.assertFalse(self.result()['passed'])

    def test_missing_browser_event(self):
        self.browser['renders'] = {}
        self.assertFalse(self.result()['passed'])

    def test_later_incident_version_keeps_first_render(self):
        self.browser['renders']['event0:2'] = 900000
        self.assertTrue(self.result()['passed'])

    def test_duplicate_delivery_cannot_replace_a_missing_delivery(self):
        self.browser['realtime'][0] = self.browser['realtime'][1]
        self.assertFalse(self.result()['passed'])

    def test_empty_queries(self):
        self.queries.clear()
        self.assertFalse(self.result()['passed'])

    def test_error_is_not_excluded_from_gate(self):
        self.queries.append({'kind': 'history', 'ok': False, 'milliseconds': 30001})
        self.assertFalse(self.result()['passed'])

    def test_slow_window_is_not_hidden_by_global_percentile(self):
        self.observation['maximum_window_incident_p99_ms'] = 1001
        self.assertFalse(self.result()['passed'])

    def test_soak_growth_and_missing_samples(self):
        rows = [{'seconds': index * 10, 'rss_bytes': {'normalizer': 100000000}, 'lag': 0} for index in range(60)]
        self.assertTrue(stable_resources(rows, 600))
        self.assertFalse(stable_resources(rows[:10], 600))
        rows[-1]['rss_bytes'] = {}
        self.assertFalse(stable_resources(rows, 600))
        rows[-1]['rss_bytes'] = {'normalizer': 100000000}
        for row in rows[40:]:
            row['lag'] = 50001
        self.assertFalse(stable_resources(rows, 600))

    def test_single_connection_is_not_a_multi_client_profile(self):
        self.publisher['clients'] = self.publisher['clients'][:1]
        self.publisher['client_count'] = 1
        self.assertFalse(self.result()['passed'])

    def test_duplicate_connector_identity_is_rejected(self):
        self.publisher['clients'][1]['connector'] = 'connector0'
        self.assertFalse(self.result()['passed'])

    def test_fast_client_cannot_hide_a_stalled_client(self):
        self.publisher['clients'][0]['acknowledged_before_drain'] -= 10000
        self.publisher['clients'][1]['acknowledged_before_drain'] += 10000
        self.assertFalse(self.result()['checks']['client_throughput'])

    def test_client_queue_growth_is_rejected(self):
        self.publisher['clients'][0]['intervals'] = [{'pending': 0}] * 50 + [{'pending': 2001}] * 10
        self.assertFalse(self.result()['checks']['client_queues'])

    def test_missing_source_owner_is_rejected(self):
        self.publisher['source_ranges']['0']['connector'] = 'unknown'
        self.assertFalse(self.result()['checks']['source_ownership'])

    def test_client_sources_cannot_be_reassigned_in_the_report(self):
        self.publisher['source_ranges']['0']['connector'] = 'connector1'
        self.assertFalse(self.result()['checks']['source_ownership'])

    def test_distribution_cannot_hide_missing_events(self):
        self.publisher['site_background_events'][1] -= 1
        self.assertFalse(self.result()['checks']['profile'])

    def test_wrong_site_weight_is_rejected(self):
        self.publisher['site_background_events'][0] += 100000
        self.publisher['site_background_events'][1] -= 100000
        self.assertFalse(self.result()['checks']['profile'])

    def test_invalid_latency(self):
        for values in ([], [-1], [float('nan')], [float('inf')]):
            with self.assertRaises(ValueError):
                percentile(values)


class SizingTest(unittest.TestCase):
    def test_replication_backups_and_catalog_serialization(self):
        spec = dict(events_per_second=50000, event_compressed_bytes=100, event_uncompressed_bytes=500,
                    event_disk_bytes=120, history_days=30, clickhouse_replicas=2, kafka_bytes_per_event=1000,
                    kafka_days=1, kafka_replicas=3, sites=10, floors_per_site=10, plan_bytes=1000000,
                    plan_versions=3, seaweed_copies=3, backup_full_copies=2, backup_incremental_days=7,
                    backup_daily_changed_bytes=1000000000, postgres_bytes=10000000000, postgres_replicas=3,
                    catalog_transaction_ms=20, catalog_batch_rows=100, measured_events_per_second=50000,
                    measured_application_instances=2, maximum_utilization=0.7)
        result = calculate(spec)
        self.assertEqual(result['storage_bytes']['seaweedfs'], 900000000)
        self.assertEqual(result['storage_bytes']['external_backup'], (50000 * 86400 * 30 * 120 + 300000000 + 10000000000) * 2 + 7000000000)
        self.assertEqual(result['catalog_rows_per_second_per_tenant_upper_bound'], 5000)
        self.assertEqual(result['application_instances_linear_estimate'], 3)
        self.assertFalse(result['validated_scale_out'])
        for key, value in [('event_disk_bytes', 0), ('maximum_utilization', 1), ('events_per_second', float('nan'))]:
            invalid = copy.deepcopy(spec)
            invalid[key] = value
            with self.assertRaises(ValueError):
                calculate(invalid)


if __name__ == '__main__':
    unittest.main()
