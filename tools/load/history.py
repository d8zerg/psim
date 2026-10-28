"""Reconcile independent SDK source ranges and measure active event storage on one replica."""
import json
import hashlib
from pathlib import Path
import ssl
import time
import urllib.request
import uuid

CASE = Path('/case')


def query(statement):
    context = ssl.create_default_context(cafile=str(CASE / 'ca.pem'))
    context.load_cert_chain(str(CASE / 'reader.pem'), str(CASE / 'reader.key'))
    endpoint = json.loads((CASE / 'config.json').read_text())['load_clickhouse_endpoint']
    request = urllib.request.Request(endpoint, data=(statement + ' FORMAT JSONEachRow').encode(),
        headers={'X-ClickHouse-User': 'deployment-admin', 'X-ClickHouse-Key': (CASE / 'clickhouse.secret').read_text().strip()})
    with urllib.request.urlopen(request, context=context, timeout=120) as response:
        return [json.loads(line) for line in response.read().decode().splitlines() if line]


def source_id(connector, reference):
    # UUIDv5 component encoding matches the ingestion identity contract.
    def component(value):
        length, prefix = len(value.encode()), bytearray()
        while length >= 128:
            prefix.append((length & 127) | 128)
            length >>= 7
        prefix.append(length)
        return bytes(prefix) + value.encode()
    digest = hashlib.sha1(uuid.UUID('728d47ed-1c69-5809-a2df-6e8b98e48ca6').bytes + component(connector) + component(reference)).digest()
    return str(uuid.UUID(bytes=digest[:16], version=5))


def reconcile():
    publisher = json.loads((CASE / 'rolling-result.json').read_text())
    scenario = json.loads((CASE / 'scenario.json').read_text())
    connectors = {str(uuid.UUID(row.get('connector', scenario['connector']))) for row in publisher['source_ranges'].values()}
    expected = {source_id(str(uuid.UUID(row.get('connector', scenario['connector']))), reference):
                {key: int(row[key]) for key in ('first', 'last', 'count', 'epoch')}
                for reference, row in publisher['source_ranges'].items()}
    epochs = {row['epoch'] for row in expected.values()}
    if len(epochs) != 1:
        raise RuntimeError('one source epoch required for an isolated load phase')
    statement = ("SELECT source_id, min(source_sequence) AS first, max(source_sequence) AS last, count() AS count, "
        "uniqExact(source_sequence) AS sequences, uniqExact(event_id) AS identifiers FROM events FINAL "
        "WHERE connector_id IN (" + ','.join("'" + connector + "'" for connector in sorted(connectors))
        + ") AND source_epoch=" + str(next(iter(epochs))) + ' GROUP BY source_id')
    deadline = time.monotonic() + 300
    started = time.monotonic()
    while True:
        actual = {row.pop('source_id'): {key: int(value) for key, value in row.items()} for row in query(statement)}
        valid = set(actual) == set(expected) and all(
            actual[key] == {field: row[field] for field in ('first', 'last', 'count')} | {'sequences': row['count'], 'identifiers': row['count']}
            for key, row in expected.items())
        if valid or time.monotonic() >= deadline:
            break
        time.sleep(2)
    rows = query("SELECT sum(rows) AS rows, sum(data_compressed_bytes) AS compressed, sum(data_uncompressed_bytes) AS uncompressed, "
                 "sum(bytes_on_disk) AS disk FROM system.parts WHERE active AND database='default' AND table='events'")
    sizes = {key: int(value) for key, value in rows[0].items()}
    result = {'reconciled': valid, 'source_count': len(expected), 'events': sum(row['count'] for row in expected.values()),
              'wait_seconds': time.monotonic() - started, 'storage': sizes,
              'event_compressed_bytes': sizes['compressed'] / sizes['rows'] if sizes['rows'] else None,
              'event_uncompressed_bytes': sizes['uncompressed'] / sizes['rows'] if sizes['rows'] else None,
              'event_disk_bytes': sizes['disk'] / sizes['rows'] if sizes['rows'] else None}
    (CASE / 'load-history.json').write_text(json.dumps(result, indent=2) + '\n')
    if not valid:
        raise RuntimeError('ClickHouse source ranges, uniqueness or counts differ from the SDK ledger')


if __name__ == '__main__':
    reconcile()
