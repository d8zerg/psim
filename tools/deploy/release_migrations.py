"""Run trusted expand DDL with session serialization and replayable, checksummed progress."""
import argparse
import hashlib
import json
from pathlib import Path
import sys

import psycopg2


def apply(directory, phase='expand', minimum_epoch=None):
    directory = Path(directory)
    manifest = json.loads((directory / 'release.json').read_text())
    if phase == 'contract' and (minimum_epoch is None or minimum_epoch < manifest.get('schema_epoch', 2) + 1):
        raise ValueError('contract requires a later release epoch and drained old readers')
    operator = Path('/operator')
    connection = psycopg2.connect(host='postgres', port=15432, dbname='psim', user='postgres',
        password=(operator / 'postgres.secret').read_text().strip(), connect_timeout=5,
        sslmode='verify-full', sslrootcert=str(operator / 'ca.pem'),
        sslcert=str(operator / 'postgres.pem'), sslkey=str(operator / 'postgres.key'))
    connection.autocommit = True
    try:
        with connection.cursor() as cursor:
            cursor.execute("SELECT pg_try_advisory_lock(hashtextextended('psim-release-migrations',0))")
            if not cursor.fetchone()[0]: raise ValueError('another release migration is running')
            cursor.execute("SET lock_timeout='2s'")
            cursor.execute("SET statement_timeout='30s'")
            cursor.execute('CREATE SCHEMA IF NOT EXISTS psim_release')
            cursor.execute('CREATE TABLE IF NOT EXISTS psim_release.migrations (name text NOT NULL, version bigint NOT NULL, phase text NOT NULL, checksum text NOT NULL, step bigint NOT NULL DEFAULT 0, PRIMARY KEY(name,version,phase))')
            for entry in manifest.get('migrations', []):
                if entry['engine'] != 'postgres': continue
                document = json.loads((directory / entry['file']).read_text())
                for migration in document['migrations']:
                    statements = migration.get(phase, [])
                    if not statements: continue
                    content = json.dumps(statements, separators=(',', ':')).encode()
                    checksum = hashlib.sha256(content).hexdigest()
                    key = (entry['file'], migration['version'], phase)
                    cursor.execute('INSERT INTO psim_release.migrations(name,version,phase,checksum) VALUES (%s,%s,%s,%s) ON CONFLICT DO NOTHING', (*key, checksum))
                    cursor.execute('SELECT checksum,step FROM psim_release.migrations WHERE name=%s AND version=%s AND phase=%s', key)
                    applied, step = cursor.fetchone()
                    if applied != checksum or not 0 <= step <= len(statements): raise ValueError('applied migration plan was modified')
                    for index in range(step, len(statements)):
                        # Autocommit supports CREATE INDEX CONCURRENTLY. Every statement must be replayable.
                        cursor.execute(statements[index])
                        cursor.execute('UPDATE psim_release.migrations SET step=%s WHERE name=%s AND version=%s AND phase=%s', (index + 1, *key))
    finally:
        connection.close()
    print('Release migration phase completed: ' + phase)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--release', required=True); parser.add_argument('--phase', choices=['expand', 'contract'], default='expand')
    parser.add_argument('--minimum-epoch', type=int)
    args = parser.parse_args()
    try: apply(args.release, args.phase, args.minimum_epoch)
    except Exception as error:
        print('release migration failed: ' + type(error).__name__, file=sys.stderr)
        raise SystemExit(1) from None
