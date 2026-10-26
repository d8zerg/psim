"""Check initial-install boundaries in a throwaway root container."""
import hashlib
import io
import json
from pathlib import Path
import sys
import tarfile
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
import node_install


class InstallBoundaries(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.prefix = self.root / 'installation'

    def tearDown(self):
        self.temporary.cleanup()

    def payload(self, filename='material/node-1/test-owner/identity.secret', body=b'fixture-secret', symlink=False):
        output = self.root / ('payload-' + str(len(list(self.root.glob('*.tar')))) + '.tar')
        metadata = {'format': 1, 'node': 'node-1', 'profile': {'prefix': str(self.prefix), 'name': 'test-lab', 'topology': 'single'},
                    'directories': {'data/node-1/cache': 61234}, 'files': {filename: {'sha256': hashlib.sha256(body).hexdigest(), 'uid': 61234, 'mode': 0o400}}}
        with tarfile.open(output, 'w:gz') as archive:
            for name, value in [('payload.json', json.dumps(metadata).encode()), (filename, body)]:
                info = tarfile.TarInfo(name); info.size = len(value)
                if symlink and name != 'payload.json':
                    info.type = tarfile.SYMTYPE; info.linkname = '/etc/passwd'; info.size = 0
                    archive.addfile(info)
                else: archive.addfile(info, io.BytesIO(value))
        return output, hashlib.sha256(output.read_bytes()).hexdigest()

    def test_checksum_failure_never_creates_an_installation(self):
        payload, _ = self.payload()
        with self.assertRaisesRegex(ValueError, 'checksum'):
            node_install.install(payload, '0' * 64, 'node-1')
        self.assertFalse(self.prefix.exists())

    def test_unsafe_members_are_rejected_before_extraction(self):
        for filename, symlink in [('../outside', False), ('/outside', False), ('material/node-1/test-owner/identity.secret', True)]:
            payload, digest = self.payload(filename, symlink=symlink)
            with self.subTest(filename=filename), self.assertRaisesRegex(ValueError, 'unsafe'):
                node_install.install(payload, digest, 'node-1')
            self.assertFalse(self.prefix.exists())

    def test_foreign_installation_directory_is_preserved(self):
        self.prefix.mkdir(); marker = self.prefix / 'preserve'; marker.write_text('operator data')
        payload, digest = self.payload()
        with self.assertRaisesRegex(ValueError, 'already in use'):
            node_install.install(payload, digest, 'node-1')
        self.assertEqual(marker.read_text(), 'operator data')

    def test_new_install_is_private_and_repetition_preserves_runtime_data(self):
        payload, digest = self.payload()
        node_install.install(payload, digest, 'node-1')
        credential = self.prefix / 'material/node-1/test-owner/identity.secret'
        self.assertEqual(credential.stat().st_uid, 61234)
        self.assertEqual(credential.stat().st_mode & 0o777, 0o400)
        marker = self.prefix / 'data/node-1/cache/preserve'; marker.write_text('runtime data')
        node_install.install(payload, digest, 'node-1')
        self.assertEqual(marker.read_text(), 'runtime data')
        changed, changed_digest = self.payload(body=b'changed credential')
        with self.assertRaisesRegex(ValueError, 'differs'):
            node_install.install(changed, changed_digest, 'node-1')
        self.assertEqual(credential.read_bytes(), b'fixture-secret')
        self.assertEqual(marker.read_text(), 'runtime data')


if __name__ == '__main__':
    unittest.main()
