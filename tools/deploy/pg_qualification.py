"""Check confirmed PostgreSQL commits across a real VM power loss."""
import argparse
import json
from pathlib import Path
import time
import uuid

import psycopg2

CASE = Path('/case')


def connect():
    return psycopg2.connect(host='postgres', port=15432, dbname='psim', user='postgres',
        password=(CASE / 'postgres.secret').read_text().strip(), connect_timeout=2, tcp_user_timeout=5000,
        sslmode='verify-full', ssl_min_protocol_version='TLSv1.3', sslrootcert=str(CASE / 'ca.pem'),
        sslcert=str(CASE / 'postgres.pem'), sslkey=str(CASE / 'postgres.key'),
        options='-c statement_timeout=3000 -c lock_timeout=2000 -c synchronous_commit=on')


def run():
    identity = str(uuid.uuid4())
    (CASE / 'pg-run.json').write_text(json.dumps({'run': identity}))
    connection = connect(); connection.autocommit = True
    with connection.cursor() as cursor:
        cursor.execute('CREATE SCHEMA IF NOT EXISTS psim_qualification AUTHORIZATION postgres')
        cursor.execute('REVOKE ALL ON SCHEMA psim_qualification FROM PUBLIC')
        cursor.execute('CREATE TABLE IF NOT EXISTS psim_qualification.commits (run uuid NOT NULL, sequence bigint NOT NULL, PRIMARY KEY(run,sequence))')
        cursor.execute('SELECT host(inet_server_addr())'); primary = cursor.fetchone()[0]
    sequence = 0
    deadline = time.monotonic() + 150
    try:
        while not (CASE / 'pg-stop').exists():
            if time.monotonic() >= deadline: raise RuntimeError('power-loss qualification deadline exceeded')
            try:
                if connection.closed:
                    connection = connect(); connection.autocommit = True
                    with connection.cursor() as cursor:
                        cursor.execute('SELECT host(inet_server_addr())'); primary = cursor.fetchone()[0]
                with connection.cursor() as cursor:
                    cursor.execute('INSERT INTO psim_qualification.commits VALUES (%s,%s) ON CONFLICT DO NOTHING', (identity, sequence))
                print(json.dumps({'ack': sequence, 'primary': primary, 'monotonic': time.monotonic()}), flush=True)
                sequence += 1
            except psycopg2.Error as error:
                connection.close()
                print(json.dumps({'retry': type(error).__name__}), flush=True)
            time.sleep(0.25)
    finally:
        connection.close()
    (CASE / 'pg-run.json').write_text(json.dumps({'run': identity, 'acknowledged': sequence}))


def verify():
    state = json.loads((CASE / 'pg-run.json').read_text())
    with connect() as connection, connection.cursor() as cursor:
        cursor.execute('SELECT sequence FROM psim_qualification.commits WHERE run=%s ORDER BY sequence', (state['run'],))
        actual = [row[0] for row in cursor.fetchall()]
        if actual != list(range(state['acknowledged'])): raise RuntimeError('confirmed PostgreSQL commits were lost or changed')
        cursor.execute('DELETE FROM psim_qualification.commits WHERE run=%s', (state['run'],))
    print(json.dumps({'acknowledged': state['acknowledged'], 'reconciled': len(actual), 'missing': 0}))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=['run', 'verify']); args = parser.parse_args()
    if args.action == 'run': run()
    else: verify()
