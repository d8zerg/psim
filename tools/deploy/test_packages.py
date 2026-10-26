"""Verify real dpkg upgrades preserve edited conffiles and maintainer-script idempotence."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


class PackageLifecycle(unittest.TestCase):
    def test_changed_configuration_survives_upgrade_and_repeated_configuration(self):
        package = next(Path('/packages').glob('psim-api-gateway_*.deb'))
        subprocess.run(['dpkg', '--install', str(package)], check=True, capture_output=True)
        configuration = Path('/etc/psim-platform/defaults/api-gateway.json')
        operator = {'settings': {'rate_per_second': 321, 'rate_burst': 654}}
        configuration.write_text(json.dumps(operator) + '\n')
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / 'next'
            subprocess.run(['dpkg-deb', '--raw-extract', str(package), str(root)], check=True)
            control = root / 'DEBIAN/control'
            current = control.read_text()
            version = next(line.split(': ', 1)[1] for line in current.splitlines() if line.startswith('Version: '))
            control.write_text(current.replace('Version: ' + version, 'Version: ' + version + '+configtest.1'))
            (root / 'etc/psim-platform/defaults/api-gateway.json').write_text('{"settings":{"rate_per_second":999}}\n')
            next_package = Path(temporary) / 'next.deb'
            subprocess.run(['dpkg-deb', '--build', '--root-owner-group', str(root), str(next_package)], check=True, capture_output=True)
            subprocess.run(['dpkg', '--force-confold', '--install', str(next_package)], check=True, capture_output=True)
            self.assertEqual(json.loads(configuration.read_text()), operator)
            self.assertTrue(configuration.with_suffix('.json.dpkg-dist').exists())
            subprocess.run(['dpkg', '--configure', '--pending'], check=True, capture_output=True)
            self.assertEqual(json.loads(configuration.read_text()), operator)
        subprocess.run(['dpkg', '--remove', 'psim-api-gateway'], check=True, capture_output=True)
        self.assertEqual(json.loads(configuration.read_text()), operator)


if __name__ == '__main__': unittest.main()
