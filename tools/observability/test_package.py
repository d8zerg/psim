"""Validate static operational assets and diagnostics failure semantics."""
import importlib.util
import io
import json
from pathlib import Path
import re
import sys
import tempfile
import time
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / 'deploy/observability'
spec = importlib.util.spec_from_file_location('agent', ROOT / 'deploy/infrastructure/monitoring/agent.py')
agent = importlib.util.module_from_spec(spec); spec.loader.exec_module(agent)


class Package(unittest.TestCase):
    def test_tenant_roles_do_not_grant_infrastructure_access(self):
        sys.path.insert(0, str(ROOT / 'tools/deploy'))
        from model import Profile
        import policies
        document = json.loads(policies.realm(Profile.load(ROOT / 'deploy/topologies/single.json'), lambda _: 'fixture', 'tenant'))
        roles = {role['name']: role for role in document['roles']['realm']}
        self.assertTrue({'psim-observability-admin', 'psim-observability-viewer'} <= roles.keys())
        for name in ('psim-observability-admin', 'psim-observability-viewer'):
            self.assertIn('mfa-required', roles[name]['composites']['realm'])
        for role in document['roles']['client']['psim']:
            self.assertFalse(any(name.startswith('psim-observability-') for name in role.get('composites', {}).get('realm', [])))
        client = next(client for client in document['clients'] if client['clientId'] == 'psim-grafana')
        mapper = client['protocolMappers'][0]['config']
        self.assertEqual(mapper['claim.name'], 'psim_observability_roles')
        self.assertEqual(mapper['id.token.claim'], 'true')
        config = (PACKAGE / 'grafana.ini').read_text()
        self.assertIn('role_attribute_strict = true', config)
        self.assertIn('contains(psim_observability_roles[*]', config)
        self.assertNotIn('resource_access.psim.roles', config)

    def test_datasources_are_provisioned_as_yaml(self):
        paths = list((PACKAGE / 'datasources').glob('*.yaml'))
        self.assertEqual(len(paths), 1)
        sources = yaml.safe_load(paths[0].read_text())['datasources']
        self.assertEqual({source['uid'] for source in sources}, {'prometheus', 'loki', 'tempo', 'alertmanager'})
        self.assertFalse(list((PACKAGE / 'datasources').glob('*.json')))

    def test_alerts_and_chaos_have_real_destinations(self):
        rules = json.loads((PACKAGE / 'rules.json').read_text())['groups'][0]['rules']
        alerts = {rule['alert']: rule for rule in rules if 'alert' in rule}
        for name, rule in alerts.items():
            self.assertTrue((PACKAGE / 'runbooks' / (name + '.html')).is_file(), name)
            self.assertEqual(rule['annotations']['runbook_url'], '/operations/runbooks/' + name)
            board = rule['annotations']['dashboard_url'].removeprefix('/monitoring/d/psim-')
            self.assertTrue((PACKAGE / 'dashboards' / (board + '.json')).is_file(), name)
        coverage = json.loads((PACKAGE / 'chaos-coverage.json').read_text())
        self.assertEqual(set(coverage), {f'C-{index:02}' for index in range(1, 16)})
        for entries in coverage.values():
            self.assertTrue(entries)
            for entry in entries:
                rule = alerts[entry['alert']]
                self.assertEqual(entry['dashboard'], rule['annotations']['dashboard_url'])
                self.assertEqual(entry['runbook'], rule['annotations']['runbook_url'])

    def test_dashboards_navigation_units_and_layout(self):
        boards = [json.loads(path.read_text()) for path in (PACKAGE / 'dashboards').glob('*.json')]
        self.assertEqual(len(boards), 7)
        uids = {board['uid'] for board in boards}
        expressions = []
        for board in boards:
            self.assertEqual({link['url'].split('/')[-1] for link in board['links']}, uids)
            self.assertEqual(board['timezone'], 'browser')
            self.assertEqual(len({panel['id'] for panel in board['panels']}), len(board['panels']))
            self.assertTrue({'deployment', 'node'} <= {v['name'] for v in board['templating']['list']})
            positions = set()
            for panel in board['panels']:
                if panel['type'] == 'row': continue
                pos = panel['gridPos']
                self.assertLessEqual(pos['x'] + pos['w'], 24)
                for x in range(pos['x'], pos['x'] + pos['w']):
                    for y in range(pos['y'], pos['y'] + pos['h']):
                        self.assertNotIn((x, y), positions); positions.add((x, y))
                if panel['type'] not in ('logs', 'table'):
                    self.assertTrue(panel['description'])
                    self.assertTrue(panel['fieldConfig']['defaults']['unit'])
                    self.assertIn(panel['fieldConfig']['defaults']['noValue'],
                                  ('Нет данных', 'Нет подтверждённой копии', 'Состояние копий отсутствует'))
                if panel['datasource']['uid'] == 'prometheus':
                    for target in panel.get('targets', []):
                        expr = target['expr'].replace('$__rate_interval', '1m')
                        expr = re.sub(r'\$[a-z_]+', '.*', expr)
                        expressions.append({'record': 'psim_dashboard_expression_' + str(len(expressions)), 'expr': expr})
        output = ROOT / 'build/observability'; output.mkdir(parents=True, exist_ok=True)
        (output / 'dashboard-expressions.json').write_text(json.dumps({'groups': [{'name': 'dashboard-syntax', 'rules': expressions}]}))

    def test_backups_never_invent_success(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'state.json'
            missing = agent.backup_metrics(path, ['plans'])
            self.assertIn('psim_backup_status_valid 0.0', missing)
            self.assertIn('psim_backup_last_success_seconds{backup="plans"} 0.0', missing)
            now = time.time()
            valid = {'updated_at_seconds': now, 'backups': {'plans': {'last_success_seconds': now - 100, 'size_bytes': 1024}}}
            path.write_text(json.dumps(valid))
            self.assertIn('psim_backup_status_valid 1.0', agent.backup_metrics(path, ['plans']))
            for value in [-1, now + 10000, float('nan')]:
                valid['backups']['plans']['last_success_seconds'] = value; path.write_text(json.dumps(valid))
                result = agent.backup_metrics(path, ['plans'])
                self.assertIn('psim_backup_status_valid 0.0', result)
                self.assertIn('psim_backup_last_success_seconds{backup="plans"} 0.0', result)

    def test_resp_and_metric_input_are_bounded(self):
        self.assertEqual(agent.resp(io.BytesIO(b'*2\r\n$3\r\nfoo\r\n:7\r\n')), ['foo', 7])
        for value in [b'$100000000\r\n', b'*100000000\r\n', b'-password hidden\r\n', b'$3\r\nx']:
            with self.assertRaises(ValueError): agent.resp(io.BytesIO(value))
        with self.assertRaises(ValueError): agent.sample('value', float('inf'))
        self.assertIn('name="line\\nquote\\\""', agent.sample('value', 1, name='line\nquote"'))

    def test_failed_probe_retains_target_and_never_reports_healthy(self):
        result = agent.probe({'component': 'postgres', 'kind': 'tcp', 'host': '127.0.0.1', 'port': 1, 'tls': None}, {})
        self.assertIn('psim_probe_success{component="postgres",target="127.0.0.1:1"} 0.0', result)
        self.assertNotIn('secret', result)


if __name__ == '__main__': unittest.main()
