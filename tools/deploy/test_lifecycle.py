"""Exercise signed release rejection, mixed-version compatibility and retained operator settings."""
import hashlib
import json
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import delivery
from node_release import Node
import node_release
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import version


class SignedRelease(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.base = Path(self.temporary.name)
        self.root = self.base / 'release'; self.root.mkdir()
        self.key = self.base / 'private.pem'; self.trusted = self.base / 'trusted.pem'
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'ed25519', '-out', str(self.key)], check=True, capture_output=True)
        subprocess.run(['openssl', 'pkey', '-in', str(self.key), '-pubout', '-out', str(self.trusted)], check=True, capture_output=True)
        component = {'binary': 'psim-api-gateway', 'image': {'file': 'api_image.tar', 'id': 'sha256:' + 'a' * 64, 'tag': 'psim-platform/psim-api-gateway:0.1.0'},
                     'deb': {'file': 'api.deb', 'version': '0.1.0'}}
        for name in ('api_image.tar', 'api.deb'):
            (self.root / name).write_bytes(b'fixture-artifact')
            (self.root / (name + '.cdx.json')).write_text(json.dumps({'bomFormat': 'CycloneDX', 'metadata': {'component':
                {'hashes': [{'alg': 'SHA-256', 'content': hashlib.sha256(b'fixture-artifact').hexdigest()}]}}}))
        (self.root / 'contracts.json').write_text('{}')
        self.manifest = {'format': 1, 'platform': 'debian13-amd64', 'version': '0.1.0', 'revision': 'a' * 40,
                         'config_format': 1, 'components': {'api-gateway': component}, 'contracts': 'contracts.json', 'migrations': []}
        (self.root / 'release.json').write_text(json.dumps(self.manifest))
        self.sign()

    def tearDown(self): self.temporary.cleanup()

    def sign(self, extra=''):
        lines = [delivery.digest(path) + '  ' + path.name for path in sorted(self.root.iterdir())
                 if path.name not in ('SHA256SUMS', 'SHA256SUMS.sig', 'psim-signing.pub.pem')]
        (self.root / 'SHA256SUMS').write_text('\n'.join(lines) + '\n' + extra)
        subprocess.run(['openssl', 'pkeyutl', '-sign', '-inkey', str(self.key), '-rawin', '-in', str(self.root / 'SHA256SUMS'),
                        '-out', str(self.root / 'SHA256SUMS.sig')], check=True, capture_output=True)

    def test_signed_inventory_and_separately_trusted_key(self):
        self.assertEqual(delivery.verify(self.root, self.trusted)['version'], '0.1.0')
        other = self.base / 'other.pem'
        subprocess.run(['openssl', 'genpkey', '-algorithm', 'ed25519', '-out', str(other)], check=True, capture_output=True)
        subprocess.run(['openssl', 'pkey', '-in', str(other), '-pubout', '-out', str(self.root / 'psim-signing.pub.pem')], check=True, capture_output=True)
        with self.assertRaisesRegex(ValueError, 'signature'):
            delivery.verify(self.root, self.root / 'psim-signing.pub.pem')
        self.assertEqual(delivery.verify(self.root, self.trusted)['version'], '0.1.0')

    def test_modified_artifact_or_manifest_is_rejected(self):
        for name in ('api.deb', 'release.json'):
            path = self.root / name; original = path.read_bytes(); path.write_bytes(original + b' ')
            with self.subTest(name=name), self.assertRaisesRegex(ValueError, 'checksum'):
                delivery.verify(self.root, self.trusted)
            path.write_bytes(original)

    def test_unlisted_file_duplicate_checksum_traversal_and_symlink_are_rejected(self):
        extra = self.root / 'extra'; extra.write_bytes(b'not signed')
        with self.assertRaisesRegex(ValueError, 'file set'): delivery.verify(self.root, self.trusted)
        extra.unlink()
        self.sign('0' * 64 + '  ../outside\n')
        with self.assertRaisesRegex(ValueError, 'checksum entry'): delivery.verify(self.root, self.trusted)
        self.sign(delivery.digest(self.root / 'api.deb') + '  api.deb\n')
        with self.assertRaisesRegex(ValueError, 'duplicate'): delivery.verify(self.root, self.trusted)
        self.sign(); (self.root / 'extra').symlink_to(self.key)
        with self.assertRaisesRegex(ValueError, 'non-regular'): delivery.verify(self.root, self.trusted)

    def test_signed_but_incorrect_sbom_is_rejected(self):
        (self.root / 'api.deb.cdx.json').write_text('{"bomFormat":"CycloneDX","metadata":{"component":{"hashes":[]}}}')
        self.sign()
        with self.assertRaisesRegex(ValueError, 'SBOM'): delivery.verify(self.root, self.trusted)

    def node_state(self):
        prefix = self.base / 'installation'
        (prefix / 'config/node-1').mkdir(parents=True)
        (prefix / 'config/node-1/compose.json').write_text('{"services":{}}')
        lifecycle = prefix / 'lifecycle'; lifecycle.mkdir()
        (lifecycle / 'releases').mkdir()
        shutil.copyfile(self.trusted, lifecycle / 'trusted.pub.pem')
        legacy = delivery.digest(self.root / 'release.json')
        target = lifecycle / 'releases' / legacy
        shutil.copytree(self.root, target / 'artifacts')
        state = {'format': 1, 'trusted_sha256': delivery.digest(self.trusted), 'contract_floor': None,
                 'components': {'api-gateway': {'release': legacy, 'config': '/operator/custom.json'}}, 'operations': []}
        (lifecycle / 'state.json').write_text(json.dumps(state))
        return Node(prefix, 'node-1'), legacy

    def test_retained_identity_binds_the_entire_signed_inventory(self):
        node, legacy = self.node_state()
        state = node.state()
        first = delivery.digest(self.root / 'SHA256SUMS')
        self.assertEqual(state['components']['api-gateway']['release'], first)
        self.assertEqual(state['components']['api-gateway']['config'], '/operator/custom.json')
        self.assertTrue((node.directory / 'releases' / legacy).is_dir())
        (self.root / 'contracts.json').write_text('{"changed":true}')
        self.sign()
        identity, _, _ = node.stage(self.root)
        self.assertNotEqual(first, identity)
        self.assertEqual(legacy, delivery.digest(self.root / 'release.json'))

    def test_contract_barrier_rejects_rollback_before_process_mutation(self):
        node, _ = self.node_state()
        state = node.state(); state['components']['api-gateway']['release'] = 'newer-release'
        (node.directory / 'state.json').write_text(json.dumps(state))
        node.fence_contract(3)
        with self.assertRaisesRegex(ValueError, 'forbidden after contract'):
            node.replace(self.root, 'api-gateway', rollback=True)


class Compatibility(unittest.TestCase):
    def snapshot(self):
        return {'messages': {'psim.Event': {'1': {'name': 'id', 'type': 'string'}}}, 'enums': {'psim.Kind': {'NONE': 0}},
                'paths': {'/v1/me': {'get': {'responses': {'200': {}}}}},
                'schemas': {'Me': {'type': 'object', 'properties': {'id': {'type': 'string'}}, 'required': ['id']}},
                'admin_info_fields': ['version']}

    def test_added_optional_properties_and_fields_are_compatible(self):
        old = self.snapshot(); new = self.snapshot()
        new['messages']['psim.Event']['2'] = {'name': 'note', 'type': 'string'}
        new['schemas']['Me']['properties']['note'] = {'type': 'string'}
        new['admin_info_fields'].append('configuration_format')
        delivery.compatible(old, new)

    def test_removed_or_changed_contracts_are_rejected(self):
        mutations = [lambda x: x['messages']['psim.Event']['1'].update(type='int64'),
                     lambda x: x['schemas']['Me']['required'].append('note'),
                     lambda x: x['paths'].clear(), lambda x: x['enums']['psim.Kind'].update(NONE=1),
                     lambda x: x['admin_info_fields'].clear()]
        for mutation in mutations:
            candidate = self.snapshot(); mutation(candidate)
            with self.subTest(mutation=mutation), self.assertRaises(ValueError): delivery.compatible(self.snapshot(), candidate)

    def test_migration_grammar_rejects_destructive_and_disguised_statements(self):
        def plan(sql): return {'migrations': [{'version': 2, 'minimum_reader': 1, 'expand': [sql]}]}
        delivery.migration_plan(plan('ALTER TABLE catalog.locations ADD COLUMN IF NOT EXISTS note text'))
        for statement in ['DROP TABLE catalog.locations', 'ALTER TABLE catalog.locations ADD COLUMN IF NOT EXISTS note text NOT NULL',
                          'CREATE TABLE IF NOT EXISTS example(id text); DROP TABLE catalog.locations',
                          'CREATE TABLE IF NOT EXISTS example(id text) /* comment */']:
            with self.subTest(statement=statement), self.assertRaises(ValueError): delivery.migration_plan(plan(statement))

    def test_operator_values_survive_defaults_and_conflicting_rename_aborts(self):
        original = {'settings': {'limit': 543, 'custom': 'preserve'}}
        changed = delivery.migrate_config(original, 1, 2, [{'path': 'settings.limit', 'rename': 'rate'},
                                                          {'path': 'settings.custom', 'default': 'new default'}])
        self.assertEqual(changed, {'settings': {'rate': 543, 'custom': 'preserve'}})
        self.assertEqual(original['settings']['limit'], 543)
        with self.assertRaisesRegex(ValueError, 'conflicts'):
            delivery.migrate_config({'settings': {'limit': 2, 'rate': 3}}, 1, 2, [{'path': 'settings.limit', 'rename': 'rate'}])
        with self.assertRaisesRegex(ValueError, 'unsupported'): delivery.migrate_config(original, 1, 3, [])


class StreamHandoff(unittest.TestCase):
    def handoff(self, fail=False):
        from types import SimpleNamespace
        with tempfile.TemporaryDirectory() as directory:
            node = object.__new__(Node)
            node.prefix = Path(directory); node.node = 'node-1'; node.state_file = node.prefix / 'state.json'
            candidate = {'release': 'a' * 64}; previous = {'container': 'old-container'}
            state = {'components': {'normalizer': previous}}
            calls = []
            node.execute = lambda *args: calls.append('launch') or 'new-container'
            def assignment(*args):
                calls.append('assignment')
                if fail: raise ValueError('assignment failed')
            node.stream_groups = assignment
            node.readiness = lambda *args, **kwargs: calls.append('ready')
            node.stream_admin = lambda *args: calls.append('proxy')
            with patch.object(node_release.subprocess, 'run', return_value=SimpleNamespace(returncode=1)), \
                    patch.object(node_release, 'run', side_effect=lambda *args: calls.append('stop-old') or ''):
                if fail:
                    with self.assertRaisesRegex(ValueError, 'assignment failed'):
                        node.stream_handoff('normalizer', candidate, previous, state, {}, '1.2.3')
                    self.assertEqual(state['components']['normalizer'], previous)
                else: node.stream_handoff('normalizer', candidate, previous, state, {}, '1.2.3')
            return calls

    def test_old_replica_stops_only_after_the_new_replica_is_assigned_and_ready(self):
        self.assertEqual(self.handoff(), ['launch', 'assignment', 'ready', 'stop-old', 'proxy'])

    def test_failed_assignment_keeps_the_old_replica_running(self):
        self.assertEqual(self.handoff(fail=True), ['launch', 'assignment'])


class Versions(unittest.TestCase):
    def test_invalid_semver_and_distinct_package_ordering(self):
        for value in ('01.2.3', '1.2', '1.2.3-dev.01', '1.2.3+empty..field'):
            with self.subTest(value=value), self.assertRaises(ValueError): version.validate(value)
        version.validate('1.2.3-rc.1+g1234.dirty')
        self.assertEqual(subprocess.run(['dpkg', '--compare-versions', '1.2.3~rc.1', 'lt', '1.2.3']).returncode, 0)

    def test_clean_tag_and_development_versions_have_one_source(self):
        responses = {('rev-parse', 'HEAD'): 'a' * 40, ('status', '--porcelain', '--untracked-files=normal'): '',
                     ('tag', '--merged', 'HEAD', '--list'): 'v1.2.3\ninvalid-tag',
                     ('cat-file', '-t', 'refs/tags/v1.2.3'): 'tag',
                     ('describe', '--tags', '--long', '--abbrev=12', '--match', 'v1.2.3'): 'v1.2.3-0-gaaaaaaaaaaaa'}
        def fake(root, *arguments): return responses[arguments]
        with tempfile.TemporaryDirectory() as directory, patch.object(version, 'git', fake):
            root = Path(directory)
            self.assertEqual(version.derive(root)['version'], '1.2.3')
            responses[('status', '--porcelain', '--untracked-files=normal')] = ' M source.cpp'
            value = version.derive(root)
            self.assertEqual(value['version'], '1.2.4-dev.0+gaaaaaaaaaaaa.dirty')
            self.assertEqual(value['image'], '1.2.4-dev.0_gaaaaaaaaaaaa.dirty')
            self.assertEqual(value['debian'], '1.2.4~dev.0+gaaaaaaaaaaaa.dirty')

    def test_missing_tag_fails_without_a_file_or_implicit_base(self):
        responses = {('rev-parse', 'HEAD'): 'a' * 40,
                     ('status', '--porcelain', '--untracked-files=normal'): '',
                     ('tag', '--merged', 'HEAD', '--list'): ''}
        with patch.object(version, 'git', lambda root, *arguments: responses[arguments]):
            with self.assertRaisesRegex(ValueError, 'no reachable SemVer Git tag'):
                version.derive()
            self.assertEqual(version.derive(override='1.2.3-dev.1')['version'], '1.2.3-dev.1')
            self.assertFalse(version.derive(override='1.2.3-dev.1')['tagged'])

    def test_prerelease_and_lightweight_tags_define_the_base(self):
        responses = {('rev-parse', 'HEAD'): 'b' * 40,
                     ('status', '--porcelain', '--untracked-files=normal'): '',
                     ('tag', '--merged', 'HEAD', '--list'): 'v0.9.2-alpha.1',
                     ('describe', '--tags', '--long', '--abbrev=12', '--match', 'v0.9.2-alpha.1'):
                         'v0.9.2-alpha.1-3-gbbbbbbbbbbbb',
                     ('cat-file', '-t', 'refs/tags/v0.9.2-alpha.1'): 'commit'}
        with patch.object(version, 'git', lambda root, *arguments: responses[arguments]):
            self.assertEqual(version.derive()['version'], '0.9.2-alpha.1.dev.3+gbbbbbbbbbbbb')
            responses[('describe', '--tags', '--long', '--abbrev=12', '--match', 'v0.9.2-alpha.1')] = 'v0.9.2-alpha.1-0-gbbbbbbbbbbbb'
            value = version.derive()
            self.assertEqual(value['version'], '0.9.2-alpha.1')
            self.assertFalse(value['tagged'])


if __name__ == '__main__': unittest.main()
