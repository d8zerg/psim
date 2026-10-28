"""Compute explicit storage estimates; one measured host does not validate scale-out."""
import json
import math
from pathlib import Path
import sys


def calculate(spec):
    positive = ('events_per_second', 'event_compressed_bytes', 'event_uncompressed_bytes', 'event_disk_bytes',
                'history_days', 'clickhouse_replicas', 'kafka_bytes_per_event', 'kafka_days', 'kafka_replicas',
                'sites', 'floors_per_site', 'plan_bytes', 'plan_versions', 'seaweed_copies', 'backup_full_copies',
                'backup_incremental_days', 'backup_daily_changed_bytes', 'postgres_bytes', 'postgres_replicas',
                'catalog_transaction_ms', 'catalog_batch_rows', 'measured_events_per_second', 'measured_application_instances')
    for key in positive:
        if not isinstance(spec.get(key), (int, float)) or isinstance(spec[key], bool) or not math.isfinite(spec[key]) or spec[key] <= 0:
            raise ValueError('positive finite sizing input required: ' + key)
    utilization = spec.get('maximum_utilization', 0.7)
    if not 0 < utilization < 1:
        raise ValueError('maximum_utilization must be between zero and one')
    rows = spec['events_per_second'] * 86400 * spec['history_days']
    history_logical = rows * spec['event_disk_bytes']
    plans_logical = spec['sites'] * spec['floors_per_site'] * spec['plan_bytes'] * spec['plan_versions']
    kafka = spec['events_per_second'] * 86400 * spec['kafka_days'] * spec['kafka_bytes_per_event'] * spec['kafka_replicas']
    backup = ((history_logical + plans_logical + spec['postgres_bytes']) * spec['backup_full_copies']
              + spec['backup_incremental_days'] * spec['backup_daily_changed_bytes'])
    storage = {'clickhouse': history_logical * spec['clickhouse_replicas'], 'kafka': kafka,
               'seaweedfs': plans_logical * spec['seaweed_copies'], 'postgres': spec['postgres_bytes'] * spec['postgres_replicas'],
               'external_backup': backup}
    return {'assumptions': spec, 'validated_scale_out': False,
            'history_rows': rows, 'compression_ratio': spec['event_uncompressed_bytes'] / spec['event_compressed_bytes'],
            'storage_bytes': storage, 'provisioned_bytes': {key: math.ceil(value / utilization) for key, value in storage.items()},
            'application_instances_linear_estimate': math.ceil(spec['events_per_second'] / spec['measured_events_per_second']
                                                               * spec['measured_application_instances'] / utilization),
            'catalog_rows_per_second_per_tenant_upper_bound': spec['catalog_batch_rows'] * 1000 / spec['catalog_transaction_ms'],
            'catalog_transactions_per_second_per_tenant_upper_bound': 1000 / spec['catalog_transaction_ms']}


if __name__ == '__main__':
    result = calculate(json.loads(Path(sys.argv[1]).read_text()))
    Path(sys.argv[2]).write_text(json.dumps(result, indent=2) + '\n')
