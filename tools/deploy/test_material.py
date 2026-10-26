"""Check private payload isolation, cache identity and incomplete provisioning boundaries."""
import hashlib
import json
from pathlib import Path
import sys
import shutil
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from archive import FILE_MTIME, package
from material import Material, command
from model import Profile
from render import render


class PrivatePayloadTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(dir='/case')
        cls.root = Path(cls.temporary.name)
        cls.bundle = cls.root / 'bundle'
        cls.private = cls.root / 'material'
        cls.profile = Profile.load(Path(__file__).resolve().parents[2] / 'deploy/topologies/local-single.json')
        render(cls.profile, cls.bundle)
        Material(cls.bundle, cls.private).create_lab()

    @classmethod
    def tearDownClass(cls):
        cls.temporary.cleanup()

    def test_payload_excludes_authority_keys_and_preserves_numeric_identity_permissions(self):
        target = self.root / 'node.tar.gz'
        package(self.bundle, self.private, 'node-1', target, 0)
        before = hashlib.sha256(target.read_bytes()).digest()
        package(self.bundle, self.private, 'node-1', target, 0)
        self.assertEqual(hashlib.sha256(target.read_bytes()).digest(), before)
        with tarfile.open(target) as archive:
            self.assertFalse(any('authority' in Path(member.name).parts for member in archive.getmembers()))
            identity = archive.getmember('material/node-1/postgres/passwd')
            self.assertEqual((identity.uid, identity.gid, identity.mode, identity.mtime),
                             (self.profile.uid('postgres'), self.profile.uid('postgres'), 0o400, FILE_MTIME))
            payload = json.load(archive.extractfile('payload.json'))
            self.assertEqual(payload['directories']['data/node-1/postgres'], self.profile.uid('postgres'))
            self.assertEqual(payload['directories']['data/node-1/clickhouse'], self.profile.uid('clickhouse'))
            public = archive.extractfile('material/node-1/monitoring/incident-service-identity.pem').read()
            self.assertEqual(public, archive.extractfile('material/node-1/incident-service/identity.pem').read())
            self.assertNotIn(b'PRIVATE KEY', public)
            self.assertEqual(archive.extractfile('material/node-1/monitoring/trust-postgres-ca.pem').read(),
                             archive.extractfile('material/node-1/postgres/ca.pem').read())
        replacement = self.root / 'replacement'
        Material(self.bundle, replacement).create_lab()
        with self.assertRaisesRegex(ValueError, 'cached node payload'):
            package(self.bundle, replacement, 'node-1', target, 0)
        self.assertEqual(hashlib.sha256(target.read_bytes()).digest(), before)

    def test_incomplete_material_cannot_produce_a_node_payload(self):
        control = self.private / 'installation.json'
        original = control.read_bytes()
        try:
            value = json.loads(original); value['complete'] = False
            control.write_text(json.dumps(value))
            target = self.root / 'incomplete.tar.gz'
            with self.assertRaisesRegex(ValueError, 'incomplete'):
                package(self.bundle, self.private, 'node-1', target, 0)
            self.assertFalse(target.exists())
        finally:
            control.write_bytes(original)

    def test_cached_payload_symlink_never_changes_its_target(self):
        marker = self.root / 'preserve'; marker.write_text('operator data')
        target = self.root / 'link.tar.gz'; target.symlink_to(marker)
        with self.assertRaisesRegex(ValueError, 'symlink'):
            package(self.bundle, self.private, 'node-1', target, 0)
        self.assertEqual(marker.read_text(), 'operator data')

    def test_external_issuance_rejects_another_key_and_exports_only_node_material(self):
        private = self.root / 'external'
        request = Material(self.bundle, private)
        request.request_external()
        self.assertFalse((private / 'authority').exists())
        self.assertFalse(json.loads((private / 'installation.json').read_text())['complete'])
        signed = self.root / 'signed'; signed.mkdir()
        certificates = [(relative, entry) for relative, entry in request.files.items() if entry['kind'] == 'certificate']
        for relative, entry in certificates:
            output = signed / relative; output.parent.mkdir(parents=True, exist_ok=True)
            authority = self.private / 'authority' / entry['authority']
            command('openssl', 'x509', '-req', '-in', str(private / 'requests' / (relative + '.csr')),
                    '-days', '2', '-set_serial', '0x' + hashlib.sha256(relative.encode()).hexdigest()[:32],
                    '-CA', str(authority) + '.pem', '-CAkey', str(authority) + '.key',
                    '-copy_extensions', 'copy', '-out', str(output))
        relative, _ = certificates[0]
        correct = (signed / relative).read_bytes()
        shutil.copyfile(self.private / 'files' / relative, signed / relative)
        with self.assertRaisesRegex(ValueError, 'private key'):
            Material(self.bundle, private).complete_external(self.private / 'authority', signed)
        self.assertFalse(json.loads((private / 'installation.json').read_text())['complete'])
        (signed / relative).write_bytes(correct)
        Material(self.bundle, private).complete_external(self.private / 'authority', signed)
        self.assertTrue(json.loads((private / 'installation.json').read_text())['complete'])
        self.assertTrue(all(path.suffix == '.pem' for path in (private / 'authority').iterdir()))
        target = self.root / 'external.tar.gz'
        with self.assertRaisesRegex(ValueError, 'mode'):
            package(self.bundle, private, 'node-1', target, 0)
        package(self.bundle, private, 'node-1', target, 0, external=True)
        with tarfile.open(target) as archive:
            self.assertFalse(any('authority' in Path(member.name).parts for member in archive.getmembers()))
            self.assertFalse(json.load(archive.extractfile('payload.json'))['lab'])


if __name__ == '__main__':
    unittest.main()
