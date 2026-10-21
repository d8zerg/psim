"""Verify live correlation against an isolated native Docker Compose project."""
import datetime as dt
import json
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('processing_check', ROOT / 'tools/processing/check.py')
processing_check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(processing_check)
ProcessingCheck, TASK = processing_check.Check, processing_check.TASK


def uid():
    return str(uuid.uuid4())


def stamp(value):
    return dt.datetime.fromtimestamp(value / 1000, dt.timezone.utc).isoformat(timespec='milliseconds').replace('+00:00', 'Z')


class Check(ProcessingCheck):
    def __init__(self, project, preset):
        super().__init__(project, preset, 0)
        self.log.close()
        self.output = ROOT / 'build/correlation' / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / 'commands.log').open('w')
        self.apps = ['correlation-engine', 'correlation-2', 'correlation-3']
        self.tenant, self.site, self.rule_id = uid(), uid(), uid()
        self.zones = [uid(), uid()]
        self.sources = [uid(), uid()]
        override = self.output / 'compose.json'
        override.write_text(json.dumps({'services': {name: {'environment': {'PSIM__TELEMETRY__SAMPLING_RATIO': '0.001'}} for name in self.apps}}))
        self.compose_cmd[-1] = str(override)
        self.version = 0
        self.sequence = 0
        self.report = {'passed': False, 'checks': []}

    def envelope(self, at=None):
        at = int(time.time() * 1000) if at is None else at
        return {'tenantId': self.tenant, 'messageId': uid(), 'correlationId': uid(), 'sequence': str(self.sequence),
                'occurredAt': stamp(at), 'receivedAt': stamp(at), 'producedAt': stamp(at),
                'actor': {'kind': 'ACTOR_KIND_SERVICE', 'id': 'correlation-test'}, 'schemaVersion': '1'}

    def probe_run(self, mode, fixtures=None):
        path = self.output / ('fixtures.json' if mode == 'seed' else 'observation.json')
        if fixtures is not None:
            path.write_text(json.dumps(fixtures))
        self.run(self.docker(f'build/debian13/{self.preset}/tools/correlation/psim-correlation-probe', mode, str(path.relative_to(ROOT))))
        return json.loads(path.read_text()) if mode != 'seed' else None

    def publish(self, fixtures):
        self.probe_run('seed', fixtures)

    def observe(self):
        value = self.probe_run('inspect')
        assert value['dlq'] == 0, value
        signals = [entry for entry in value['signals'] if entry['envelope']['tenantId'] == self.tenant]
        ids = [entry['envelope']['messageId'] for entry in signals]
        assert len(ids) == len(set(ids)), 'duplicate committed signal identifiers'
        return signals

    def wait_signals(self, count):
        self.wait(lambda: len(self.observe()) >= count, 90)
        signals = self.observe()
        assert len(signals) == count, f'expected {count} signals, got {len(signals)}'
        return signals

    def config(self, spec):
        self.version += 1
        rule = {'ruleId': self.rule_id, 'version': self.version, 'name': 'Live perimeter', 'state': 'RULE_STATE_ACTIVE',
                'correlationScope': 'CORRELATION_SCOPE_SITE', 'filter': {'types': ['intrusion.*']}, 'window': '1s',
                'allowedLateness': '2s', 'output': {'incidentTypeId': self.rule_id, 'priority': 'PRIORITY_P2'}} | spec
        value = {'envelope': self.envelope(), 'ruleSet': {'ruleSetId': uid(), 'version': str(self.version),
                 'activatedAt': stamp(int(time.time() * 1000)), 'rules': [rule]}}
        self.publish([{'topic': 'psim.config.v1', 'key': 'ruleset/' + self.tenant, 'value': value}])
        time.sleep(0.3)
        return rule

    def events(self, count, restore=False, reverse=False, kind=None):
        origin = int(time.time() * 1000)
        result = []
        for index in range(count):
            self.sequence += 1
            zone = index % 2
            value = {'envelope': self.envelope(origin + index), 'event': {
                'type': kind or ('intrusion.zone.restore' if restore else 'intrusion.zone.alarm'), 'siteId': self.site,
                'zoneId': self.zones[zone], 'deviceId': self.zones[zone], 'sourceId': self.sources[zone],
                'sourceEpoch': 1, 'severity': 'SEVERITY_HIGH'}}
            result.append({'topic': 'psim.events.normalized.v1', 'key': self.site + ':' + self.zones[zone],
                           'partition': zone, 'value': value})
        return list(reversed(result)) if reverse else result

    def catalog(self):
        now = int(time.time() * 1000)
        building, floor = uid(), uid()
        fixtures = []
        locations = [(self.site, '', 'LOCATION_KIND_SITE'), (building, self.site, 'LOCATION_KIND_BUILDING'),
                     (floor, building, 'LOCATION_KIND_FLOOR')] + [(zone, floor, 'LOCATION_KIND_ZONE') for zone in self.zones]
        for resource, parent, kind in locations:
            fixtures.append({'topic': 'psim.catalog.v1', 'key': 'location:' + resource, 'value': {
                'envelope': self.envelope(), 'location': {'locationId': resource, 'siteId': self.site, 'parentId': parent,
                'kind': kind, 'name': 'Test resource', 'code': resource, 'state': 'RESOURCE_STATE_ACTIVE', 'version': '1'}}})
        for index, zone in enumerate(self.zones):
            fixtures.append({'topic': 'psim.catalog.v1', 'key': 'device:' + zone, 'value': {
                'envelope': self.envelope(), 'device': {'deviceId': zone, 'deviceTypeId': uid(), 'connectorId': uid(),
                'siteId': self.site, 'zoneId': zone, 'externalId': zone, 'state': 'RESOURCE_STATE_ACTIVE', 'version': '1',
                'updatedAt': stamp(now), 'sources': [{'sourceId': self.sources[index], 'externalRef': zone}]}}})
        self.publish(fixtures)
        return building

    def state_owner(self):
        observation = self.probe_run('inspect')
        prefix = self.tenant + '/' + self.rule_id + '/' + str(self.version) + '/'
        partitions = [part for key, part in observation['state_partitions'].items() if key.startswith(prefix)]
        assert len(partitions) == 1, partitions
        description = self.compose('exec', '-T', 'kafka-1', '/opt/kafka/bin/kafka-consumer-groups.sh',
                                   '--bootstrap-server', 'kafka-1:19092', '--describe', '--group', 'psim-correlation-engine')
        for row in description.splitlines():
            fields = row.split()
            if 'psim.correlation.repartition.v1' not in fields:
                continue
            column = fields.index('psim.correlation.repartition.v1')
            if int(fields[column + 1]) != partitions[0]:
                continue
            for service, client in [('correlation-engine', 'correlation-event-flow'),
                                    ('correlation-2', 'correlation-correlation-2'),
                                    ('correlation-3', 'correlation-correlation-3')]:
                if fields[-1] == client:
                    return service
        raise RuntimeError('cannot identify the owner of the populated state partition')

    def archive(self):
        self.version += 1
        self.publish([{'topic': 'psim.config.v1', 'key': 'ruleset/' + self.tenant, 'value': {'envelope': self.envelope(),
                      'ruleSet': {'ruleSetId': uid(), 'version': str(self.version), 'activatedAt': stamp(int(time.time() * 1000)), 'rules': []}}}])

    def additional_checks(self):
        building = self.catalog()
        self.config({'correlationScope': 'CORRELATION_SCOPE_BUILDING', 'conjunction': {
            'parts': [{'types': ['intrusion.zone.alarm']}, {'types': ['intrusion.zone.restore']}]}})
        pair = self.events(2)
        pair[1]['value']['event']['type'] = 'intrusion.zone.restore'
        self.publish(pair)
        signals = self.wait_signals(62)
        wide = next(entry['signal'] for entry in signals if entry['signal']['origin']['ruleVersion'] == self.version)
        assert building in wide['correlationKey'] and len(wide['eventIds']) == 2 and not wide.get('zoneId'), wide
        self.report['checks'].append('building-wide conjunction across two zones and catalog hierarchy')
        self.config({'correlationScope': 'CORRELATION_SCOPE_ZONE', 'absence': {'expected': {'types': ['intrusion.zone.alarm']}}})
        self.wait(lambda: len(self.observe()) >= 64, 30)
        self.archive()
        time.sleep(4)
        baseline = len(self.observe())
        remaining = self.probe_run('inspect')['states']
        assert not any(key.startswith(self.tenant + '/' + self.rule_id + '/') for key in remaining), remaining
        periodic = [entry['signal'] for entry in self.observe() if not entry['signal'].get('eventIds')]
        assert len(periodic) >= 2 and all('absenceDeadline' in entry for entry in periodic)
        self.report['checks'].append('periodic absence without input, inventory and rule-set archive')
        now = int(time.time() * 1000)
        maintenance_id = uid()
        policy = {'maintenanceId': maintenance_id, 'siteId': self.site, 'zoneId': self.zones[0],
                  'startsAt': stamp(now), 'activatedAt': stamp(now), 'updatedAt': stamp(now), 'endsAt': stamp(now + 60000),
                  'state': 'MAINTENANCE_STATE_ACTIVE', 'version': '1', 'reason': 'Synthetic maintenance',
                  'responsible': 'Test operator', 'suppression': {'ruleIds': [self.rule_id]}}
        self.publish([{'topic': 'psim.config.v1', 'key': 'maintenance/' + self.tenant + '/' + maintenance_id,
                       'value': {'envelope': self.envelope(), 'maintenanceWindow': policy}}])
        self.config({'single': {}})
        self.publish(self.events(2))
        self.wait_signals(baseline + 1)
        observation = self.probe_run('inspect')
        suppressed = [entry for entry in observation['suppressed'] if entry['envelope']['tenantId'] == self.tenant]
        assert len(suppressed) == 1, suppressed
        assert json.loads(suppressed[0]['auditEntry']['details']['maintenance_versions']) == [
            {'maintenance_id': maintenance_id, 'version': 1}]
        self.config({'single': {}, 'output': {'incidentTypeId': self.rule_id, 'priority': 'PRIORITY_P1'}})
        self.publish(self.events(1))
        self.wait_signals(baseline + 2)
        self.config({'single': {}, 'filter': {'types': ['fire.*']}})
        self.publish(self.events(1, kind='fire.detector.alarm'))
        self.wait_signals(baseline + 3)
        self.report['checks'].append('transactional suppression audit with exact policy version, P1 and fire protection')
        policy['version'] = '2'
        policy['state'] = 'MAINTENANCE_STATE_CANCELLED'
        policy['updatedAt'] = stamp(int(time.time() * 1000))
        policy['completedAt'] = policy['updatedAt']
        self.publish([{'topic': 'psim.config.v1', 'key': 'maintenance/' + self.tenant + '/' + maintenance_id,
                       'value': {'envelope': self.envelope(), 'maintenanceWindow': policy}}])
        self.config({'single': {}, 'filter': {'types': ['intrusion.*'], 'condition': 'event.live_observation'}})
        unknown = self.events(1)
        self.publish(unknown)
        self.wait_signals(baseline + 3)
        fresh = self.events(1)
        fresh[0]['value']['event']['liveObservation'] = True
        fresh[0]['value']['event']['stateObservedAt'] = fresh[0]['value']['envelope']['receivedAt']
        self.publish(fresh)
        self.wait_signals(baseline + 4)
        self.report['checks'].append('unknown observation does not satisfy a current-state condition')

    def execute(self):
        try:
            self.compose('up', '-d', '--wait', '--wait-timeout', '180', 'kafka-1', 'registry', 'otel-collector')
            self.run([sys.executable, '-I', 'tools/env/envctl.py', 'topics', self.project, 'single'])
            self.run([TASK, 'contracts:registry:register', 'PROJECT=' + self.project])
            processing = {'envelope': self.envelope(), 'processing': {'version': '1', 'sourceIdleTimeout': '0.1s',
                          'maxClockSkew': '5s', 'maxBufferReplayAge': '3600s'}}
            self.publish([{'topic': 'psim.config.v1', 'key': 'processing/' + self.tenant, 'value': processing}])
            self.config({'single': {}})
            self.compose('up', '-d', '--force-recreate', *self.apps)
            self.probe.start_probe()
            self.ready(self.apps)
            initial = self.events(30)
            self.publish(initial)
            self.wait_signals(30)
            self.publish(initial)
            self.wait_signals(30)
            self.report['checks'].append('published single rule, cross-zone routing and duplicate input')
            self.config({'threshold': {'count': 2}})
            self.publish(self.events(20, reverse=True))
            self.wait_signals(40)
            self.report['checks'].append('event-time threshold across two normalized input partitions')
            owner = self.state_owner()
            self.compose('kill', '-s', 'SIGKILL', owner)
            started = time.monotonic()
            self.publish(self.events(20))
            self.wait_signals(50)
            self.compose('up', '-d', owner)
            self.ready(self.apps)
            recovery = time.monotonic() - started
            assert recovery <= 30, recovery
            self.publish(self.events(20))
            self.wait_signals(60)
            self.report['recovery_seconds'] = recovery
            self.report['checks'].append('SIGKILL, partition reassignment, cold RocksDB recovery and no duplicate signals')
            self.config({'absence': {'trigger': {'types': ['intrusion.zone.alarm']},
                                    'expected': {'types': ['intrusion.zone.restore']}}})
            self.publish(self.events(1))
            self.wait_signals(61)
            last = next(entry['signal'] for entry in self.observe() if entry['signal']['origin']['ruleVersion'] == self.version)
            assert 'absenceDeadline' in last, last
            self.report['checks'].append('absence deadline advances without new input and survives idle sources')
            self.additional_checks()
            self.report['passed'] = True
        finally:
            self.compose('logs', '--no-color', *self.apps)
            self.probe.stop_probe()
            if self.owned:
                self.compose('--profile', 'correlation-test', '--profile', 'event-flow', 'down', '-v', '--remove-orphans', '--timeout', '10')
            (self.output / 'report.json').write_text(json.dumps(self.report, indent=2) + '\n')
            self.log.close()
        print(json.dumps(self.report, indent=2))


if __name__ == '__main__':
    Check(sys.argv[1], sys.argv[2]).execute()
