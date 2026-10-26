"""Exercise archived NSS identities and PostgreSQL initialization with a numeric UID."""
import os
from pathlib import Path
import secrets
import subprocess
import unittest

from archive import FILE_MTIME


class PostgresIdentityTest(unittest.TestCase):
    def test_archived_identity_survives_sanitized_child_environment(self):
        directory = Path('/etc/psim-platform/postgres/secrets')
        uid, gid = os.getuid(), os.getgid()
        for name, body in {
            'passwd': f'postgres:x:{uid}:{gid}:psim-platform:/case:/usr/sbin/nologin\n',
            'group': f'postgres:x:{gid}:\n',
        }.items():
            path = directory / name
            path.write_text(body)
            path.chmod(0o400)
            os.utime(path, (FILE_MTIME, FILE_MTIME))
        account = subprocess.check_output(['/usr/local/bin/psim-postgres-env', 'getent', 'passwd', str(uid)], text=True)
        self.assertEqual(account.split(':')[0], 'postgres')
        password = directory / 'admin.secret'
        password.write_text(secrets.token_urlsafe(32))
        password.chmod(0o400)
        environment = {key: value for key, value in os.environ.items() if key != 'LD_PRELOAD'}
        result = subprocess.run([
            '/opt/psim/postgres-bin/initdb', '-D', '/case/data', '--username=postgres',
            '--auth-local=peer', '--auth-host=scram-sha-256', '--pwfile=' + str(password),
        ], env=environment, capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(Path('/case/data/PG_VERSION').is_file())


if __name__ == '__main__':
    unittest.main()
