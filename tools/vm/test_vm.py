"""Check VM namespace ownership and untrusted profile boundaries without a hypervisor."""
import copy
import hashlib
import io
import json
import os
import subprocess
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parent))
import vm


class Boundaries(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.cache = patch.object(vm, 'CACHE', self.root / 'cache')
        self.cache.start()
        self.value = json.loads((vm.ROOT / 'deploy/vm/local.json').read_text())

    def tearDown(self):
        self.cache.stop()
        self.temporary.cleanup()

    def load(self, value):
        path = self.root / 'profile.json'
        path.write_text(json.dumps(value))
        return vm.profile(path)

    def test_profile_rejects_network_escape_and_resource_mistakes(self):
        self.assertEqual(len(self.load(self.value)['nodes']), 3)
        for key, bad in [('cpus', True), ('memory_mib', 1), ('disk_gib', -1), ('ssh_port', 22), ('address', '10.92.0.11')]:
            value = copy.deepcopy(self.value)
            value['nodes'][0][key] = bad
            with self.subTest(key=key), self.assertRaises(ValueError):
                self.load(value)
        for name in ('../foreign', '--privileged', None):
            with self.subTest(name=name), self.assertRaises(ValueError):
                self.load(self.value | {'name': name})
        with self.assertRaises(ValueError):
            self.load(self.value | {'nodes': [self.value['nodes'][0]] * 2})
        with self.assertRaises(ValueError):
            self.load(self.value | {'network': '198.51.100.0/24'})

    def test_destroy_refuses_a_symlink_before_calling_docker(self):
        foreign = self.root / 'foreign'
        foreign.mkdir()
        marker = foreign / 'preserve'
        marker.write_text('foreign data')
        vm.CACHE.mkdir()
        (vm.CACHE / 'psim-platform-vm').symlink_to(foreign, target_is_directory=True)
        with patch.object(vm, 'container', side_effect=AssertionError('Docker must not be called')):
            with self.assertRaisesRegex(ValueError, 'symlink'):
                vm.destroy('psim-platform-vm')
        self.assertEqual(marker.read_text(), 'foreign data')

    def test_destroy_refuses_foreign_ownership_or_location(self):
        state = vm.directory('psim-platform-vm')
        state.mkdir()
        valid = {'managed': 1, 'name': 'psim-platform-vm', 'uid': os.getuid(), 'host_state': str(state)}
        for invalid in (valid | {'uid': os.getuid() + 1}, valid | {'host_state': str(self.root / 'foreign')}, valid | {'managed': 0}):
            (state / 'manifest.json').write_text(json.dumps(invalid))
            with patch.object(vm, 'container', side_effect=AssertionError('Docker must not be called')):
                with self.assertRaisesRegex(ValueError, 'mismatch'):
                    vm.destroy('psim-platform-vm')
            self.assertTrue(state.is_dir())

    def test_foreign_container_cannot_be_removed(self):
        result = json.dumps([{'Config': {'Labels': {'foreign': 'yes'}}}])
        with patch.object(vm, 'capture', side_effect=['foreign-id', result]):
            with self.assertRaisesRegex(ValueError, 'another resource'):
                vm.container('psim-platform-vm')

    def test_docker_query_failure_never_allows_disk_removal(self):
        state = vm.directory('psim-platform-vm')
        state.mkdir()
        vm.write_manifest(state, {'managed': 1, 'name': 'psim-platform-vm', 'uid': os.getuid(), 'host_state': str(state)})
        marker = state / 'preserve'
        marker.write_text('guest data')
        with patch.object(vm, 'capture', side_effect=subprocess.CalledProcessError(1, ['docker'])):
            with self.assertRaises(subprocess.CalledProcessError):
                vm.destroy('psim-platform-vm')
        self.assertEqual(marker.read_text(), 'guest data')

    def test_failed_manifest_replacement_preserves_original_private_state(self):
        state = vm.directory('psim-platform-vm')
        state.mkdir()
        vm.write_manifest(state, {'instance': 'original'})
        with patch.object(vm.os, 'replace', side_effect=OSError('interrupted replacement')):
            with self.assertRaises(OSError):
                vm.write_manifest(state, {'instance': 'replacement'})
        self.assertEqual(json.loads((state / 'manifest.json').read_text()), {'instance': 'original'})
        self.assertEqual((state / 'manifest.json').stat().st_mode & 0o777, 0o600)
        self.assertFalse(list(state.glob('*.tmp')))

    def test_failed_image_download_never_becomes_a_cached_image(self):
        (self.root / 'deploy/vm').mkdir(parents=True)
        data = b'valid image'
        spec = {'sha512': hashlib.sha512(data).hexdigest(), 'url': 'https://cloud.debian.org/images/cloud/trixie/fixture.qcow2', 'size_bytes': len(data)}
        (self.root / 'deploy/vm/image.json').write_text(json.dumps(spec))
        for bad in (b'truncated', b'wrong image', data + b'oversized'):
            with self.subTest(size=len(bad)), patch.object(vm, 'ROOT', self.root):
                with patch.object(vm.urllib.request, 'urlopen', return_value=io.BytesIO(bad)):
                    with self.assertRaisesRegex(ValueError, 'size|checksum'):
                        vm.base_image()
                self.assertFalse(list((vm.CACHE / 'images').glob('*.qcow2')))
                self.assertFalse(list((vm.CACHE / 'images').glob('*.part')))


if __name__ == '__main__':
    unittest.main()
