"""Render Kafka topics and scoped ACLs from the shared topic registry."""
from pathlib import Path
import shlex
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'contracts'))
import contracts
from model import SERVICES

GROUPS = {
    'connector-gateway': ['gateway-commands-'],
    'resource-catalog': ['catalog-'],
    'normalizer': ['psim-normalizer'],
    'event-history': ['psim-event-history', 'psim-history-facts', 'psim-history-retention'],
    'correlation-engine': ['psim-correlation-router', 'psim-correlation-engine', 'correlation-restore-'],
    'incident-service': ['psim-incident-service', 'psim-incident-response'],
    'response-engine': ['psim-response-engine', 'psim-response-catalog', 'psim-response-results'],
    'command-service': ['psim-command-service', 'psim-command-catalog', 'psim-command-incidents', 'psim-command-requests'],
    'notification-service': ['psim-notification-service'],
    'projection-service': ['psim-projection-service', 'console-models', 'console-rebuild-'],
    'audit-service': ['psim-audit-service'],
    'api-gateway': ['realtime', 'realtime-replay'],
    'rule-replay': ['rule-replay-'],
}
TRANSACTIONS = {
    'normalizer': ['normalizer-'],
    'correlation-engine': ['correlation-', 'correlation-router-'],
    'event-history': ['event-history-', 'history-facts-'],
    'projection-service': ['incident-facts-'],
}
DLQ_OWNERS = {
    'catalog': 'resource-catalog', 'normalizer': 'normalizer',
    'correlation-router': 'correlation-engine', 'correlation': 'correlation-engine',
    'history': 'event-history', 'event-history': 'event-history', 'history-retention': 'event-history',
    'incident': 'incident-service', 'incident-response': 'incident-service',
    'response': 'response-engine', 'response-catalog': 'response-engine', 'response-results': 'response-engine',
    'command': 'command-service', 'command-requests': 'command-service', 'notification': 'notification-service',
    'audit': 'audit-service', 'projection': 'projection-service', 'realtime': 'api-gateway',
}


def bootstrap(profile):
    root = Path(__file__).resolve().parents[2]
    registry = contracts.load_yaml(root / 'contracts/topics/topics.yaml')
    topics = {topic['name']: topic for topic in registry['topics']}
    deployment = profile.topology
    replication = registry['defaults']['replication_factor'][deployment]
    minimum = registry['defaults']['min_insync_replicas'][deployment]
    broker = profile.select('infrastructure')[0].name + ':9092'
    commands = ['#!/bin/sh', 'set -eu']
    def invoke(tool, *args):
        commands.append(shlex.join(['/opt/kafka/bin/kafka-' + tool + '.sh', '--bootstrap-server', broker,
                                    '--command-config', '/operator/kafka.properties', *map(str, args)]))
    def topic(name, partitions, retention, cleanup):
        invoke('topics', '--create', '--if-not-exists', '--topic', name, '--partitions', partitions,
               '--replication-factor', replication, '--config', 'min.insync.replicas=' + str(minimum),
               '--config', 'cleanup.policy=' + cleanup, '--config', 'retention.ms=' + str(contracts.duration_ms(retention)),
               '--config', 'compression.type=' + registry['defaults']['compression'])
    reads, writes = {}, {}
    for item in topics.values():
        topic(item['name'], contracts.topic_partitions(item, topics, deployment), item['retention'], item['cleanup'])
        for operation, target in [('consumers', reads), ('producers', writes)]:
            for owner in item[operation]:
                for name in SERVICES if owner == 'all-services' else [owner]:
                    target.setdefault(name, set()).add(item['name'])
    for stage in registry['dlq']['stages']:
        name = 'psim.dlq.' + stage + '.v1'
        topic(name, registry['dlq']['partitions'][deployment], registry['dlq']['retention'], 'delete')
        writes.setdefault(DLQ_OWNERS[stage], set()).add(name)
    writes.setdefault('rule-management', set()).update(['psim.config.v1', 'psim.audit.v1'])
    reads['rule-replay'] = {'psim.events.normalized.v1', 'psim.catalog.v1', 'psim.config.v1', 'psim.audit.v1'}
    def acl(owner, operation, resource, names, prefix=False):
        args = ['--add', '--allow-principal', 'User:' + owner, '--operation', operation]
        if resource == '--cluster': args.append(resource)
        for name in sorted(names): args += [resource, name]
        if prefix: args += ['--resource-pattern-type', 'prefixed']
        invoke('acls', *args)
    for operation, permissions in [('Read', reads), ('Write', writes)]:
        for owner, names in sorted(permissions.items()):
            acl(owner, operation, '--topic', names)
    for owner in sorted(writes): acl(owner, 'IdempotentWrite', '--cluster', [])
    for owner, groups in GROUPS.items():
        for prefix in (False, True):
            names = [group for group in groups if group.endswith('-') == prefix]
            if names: acl(owner, 'Read', '--group', names, prefix)
    for owner, names in TRANSACTIONS.items(): acl(owner, 'Write', '--transactional-id', names, True)
    commands.append('echo "Kafka topics and service ACLs configured"')
    return '\n'.join(commands) + '\n'
