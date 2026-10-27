"""Validate static operational assets and diagnostics failure semantics."""
import json
from pathlib import Path
import re
import sys
import unittest
import yaml

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / 'deploy/observability'


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
        boards = [(language, json.loads(path.read_text())) for language, directory in
                  [('en', PACKAGE / 'dashboards'), ('ru', PACKAGE / 'ru/dashboards')]
                  for path in directory.glob('*.json')]
        self.assertEqual(len(boards), 14)
        uids = {language: {board['uid'] for lang, board in boards if lang == language} for language in ('en', 'ru')}
        expressions = []
        for language, board in boards:
            self.assertEqual({link['url'].split('/')[-1] for link in board['links'][:-1]}, uids[language])
            alternate = board['links'][-1]
            self.assertEqual(alternate['title'], 'RU' if language == 'en' else 'EN')
            self.assertEqual(alternate['url'], '/monitoring/d/' +
                             (board['uid'] + '-ru' if language == 'en' else board['uid'].removesuffix('-ru')))
            self.assertTrue(alternate['keepTime'] and alternate['includeVars'])
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
                                  ('No data', 'No confirmed copy', 'Backup status unavailable') if language == 'en' else
                                  ('Нет данных', 'Нет подтверждённой копии', 'Состояние копий отсутствует'))
                if panel['datasource']['uid'] == 'prometheus':
                    for target in panel.get('targets', []):
                        expr = target['expr'].replace('$__rate_interval', '1m')
                        expr = re.sub(r'\$[a-z_]+', '.*', expr)
                        expressions.append({'record': 'psim_dashboard_expression_' + str(len(expressions)), 'expr': expr})
        output = ROOT / 'build/observability'; output.mkdir(parents=True, exist_ok=True)
        (output / 'dashboard-expressions.json').write_text(json.dumps({'groups': [{'name': 'dashboard-syntax', 'rules': expressions}]}))

    def test_translation_preserves_queries_filters_and_panel_identity(self):
        for path in (PACKAGE / 'dashboards').glob('*.json'):
            en = json.loads(path.read_text())
            ru = json.loads((PACKAGE / 'ru/dashboards' / path.name).read_text())
            self.assertEqual([panel['id'] for panel in en['panels']], [panel['id'] for panel in ru['panels']])
            for left, right in zip(en['panels'], ru['panels'], strict=True):
                self.assertEqual(left['gridPos'], right['gridPos'])
                self.assertEqual([target.get('expr') for target in left.get('targets', [])],
                                 [target.get('expr') for target in right.get('targets', [])])
            for left, right in zip(en['templating']['list'], ru['templating']['list'], strict=True):
                for field in ('name', 'type', 'query', 'allValue'):
                    self.assertEqual(left.get(field), right.get(field))



if __name__ == '__main__': unittest.main()
