"""Verify isolated lab placement without changing replicated deployment defaults."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from model import Profile
from render import render
from kafka import bootstrap

ROOT = Path(__file__).resolve().parents[2]


class IsolatedLab(unittest.TestCase):
    def setUp(self):
        self.value = json.loads((ROOT / 'deploy/topologies/local-isolated.json').read_text())

    def test_render_separates_brokers_databases_and_applications(self):
        profile = Profile.parse(self.value)
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'bundle'
            render(profile, output)
            services = {node.name: json.loads((output / node.name / 'compose.json').read_text())['services'] for node in profile.nodes}
            self.assertEqual([name for name, items in services.items() if 'kafka' in items], ['node-3'])
            for database in ('postgres', 'clickhouse', 'valkey'):
                self.assertEqual([name for name, items in services.items() if database in items], ['node-4'])
            for name in ('node-1', 'node-2'):
                self.assertIn('normalizer', services[name])
                config = json.loads((output / name / 'services/normalizer.json').read_text())
                self.assertEqual(config['settings']['brokers'], 'node-3:9092')
            self.assertIn('keycloak', services['node-1'])
            self.assertIn('registry', services['node-1'])
            self.assertIn('grafana', services['node-2'])
            self.assertNotIn('normalizer', services['node-3'])
            self.assertNotIn('normalizer', services['node-4'])
            proxy = (output / 'material.json').read_text()
            self.assertIn('node-4:5432', proxy)
            self.assertIn('node-1:18181', proxy)
            self.assertNotIn('node-4:18181', proxy)
        script = bootstrap(profile)
        self.assertIn('--bootstrap-server node-3:9092', script)
        self.assertIn('--replication-factor 1', script)
        self.assertIn('min.insync.replicas=1', script)
        self.assertIn('--partitions 48', script)

    def test_cluster_and_single_profiles_keep_their_replication(self):
        for file, replicas in [('local-vm.json', 3), ('cluster.json', 3), ('local-single.json', 1)]:
            with self.subTest(file=file):
                profile = Profile.load(ROOT / 'deploy/topologies' / file)
                self.assertEqual(profile.select('kafka'), profile.select('infrastructure'))
                self.assertIn('--replication-factor ' + str(replicas), bootstrap(profile))

    def test_isolated_profile_rejects_shared_roles_and_missing_nodes(self):
        for role in ('application', 'infrastructure'):
            value = copy.deepcopy(self.value)
            value['nodes'][2]['roles'].append(role)
            with self.subTest(role=role), self.assertRaises(ValueError):
                Profile.parse(value)
        value = copy.deepcopy(self.value)
        value['nodes'].pop()
        with self.assertRaises(ValueError):
            Profile.parse(value)
        value = copy.deepcopy(self.value)
        value['topology'] = 'cluster'
        with self.assertRaises(ValueError):
            Profile.parse(value)


if __name__ == '__main__':
    unittest.main()
