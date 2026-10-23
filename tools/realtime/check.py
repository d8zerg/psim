"""Exercise scoped realtime delivery with Kafka, two Gateways and 500 WSS clients."""
import json
import os
from pathlib import Path
import re
import runpy
import sys

ROOT = Path(__file__).resolve().parents[2]
api = runpy.run_path(str(ROOT / 'tools/api-gateway/check.py'))
Base, uid, helpers = api['Check'], api['uid'], api['helpers']

class Check(Base):
    def __init__(self, project, preset):
        super().__init__(project, preset)
        self.peer_output = ROOT / 'build/realtime' / preset
        self.peer_output.mkdir(parents=True, exist_ok=True)
        config = json.loads((self.output / 'api.json').read_text())
        config['settings']['ip_rate_per_second'] = 10000
        config['settings']['ip_rate_burst'] = 10000
        config['settings']['incidents_port'] = '8080'
        (self.output / 'api.json').write_text(json.dumps(config))
        content = self.override.read_text().replace('mem_limit: 128m', 'mem_limit: 1024m')
        command = json.dumps(['/repo/build/debian13/' + preset + '/services/api-gateway/psim-api-gateway', '--config', '/repo/build/resource-catalog/' + preset + '/api.json'])
        content += f'  api-2:\n    image: debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5\n    user: "{os.getuid()}:{os.getgid()}"\n    read_only: true\n    cap_drop: [ALL]\n    security_opt: [no-new-privileges:true' + (', seccomp=unconfined' if preset == 'tsan' else '') + f']\n    volumes: ["{ROOT}:/repo:ro", "{ROOT}/build/debian13/{preset}/profiles:/profiles"]\n    environment: {{LLVM_PROFILE_FILE: "/profiles/%p-%m.profraw", PSIM__SERVICE__INSTANCE: realtime-2}}\n    tmpfs: [/tmp]\n    mem_limit: 1024m\n    command: {command}\n'
        self.override.write_text(content)

    def principal_cases(self):
        return super().principal_cases() | {'foreign_operator': ('operator', [self.foreign_site], helpers['TENANT']), 'tenant_operator': ('operator', ['*'], helpers['OTHER_TENANT']), 'short': ('operator', [self.site_id], helpers['TENANT'])}

    def ready(self, service):
        def probe():
            try:
                self.env_command("python3 -c 'import urllib.request; assert urllib.request.urlopen(\"http://" + service + ":9100/health/ready\").status==200'")
                return True
            except RuntimeError:
                return False
        self.wait(probe, 90)

    def response_owner(self, incidents):
        self.compose('up', '-d', '--force-recreate', 'incident-service')
        self.ready('incident-service')
        for record in incidents:
            incident = record['value']['incidentCreated']['incident']
            self.sql("INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) VALUES('" + helpers['TENANT'] + "','" + incident['incidentId'] + "','realtime-" + incident['incidentId'] + "',0,'" + json.dumps(incident).replace("'", "''") + "'::jsonb)")

    def event(self):
        identity = uid()
        incident = {'incidentId': identity, 'incidentTypeId': uid(), 'siteId': self.site_id, 'zoneId': self.zones[0]['locationId'], 'state': 'INCIDENT_STATE_NEW', 'priority': 'PRIORITY_P1', 'title': 'Realtime verification', 'createdAt': helpers['utc'](), 'version': '1'}
        envelope = {'messageId': uid(), 'tenantId': helpers['TENANT'], 'sequence': '1', 'occurredAt': helpers['utc'](), 'producedAt': helpers['utc'](), 'schemaVersion': '1', 'actor': {'kind': 'ACTOR_KIND_SERVICE', 'id': 'realtime-probe'}}
        return {'topic': 'psim.incidents.events.v1', 'key': identity, 'value': {'envelope': envelope, 'incidentCreated': {'incident': incident}}}

    def duty(self, version='1'):
        return {'topic': 'psim.response.events.v1', 'key': self.site_id, 'value': {'envelope': self.event()['value']['envelope'], 'siteDutyChanged': {'siteId': self.site_id, 'userId': uid(), 'fallbackUserId': uid(), 'version': version, 'updatedAt': helpers['utc']()}}}

    def peer(self, mode, records, token=None, clients=500, extra=None):
        config = {'preset': self.preset, 'output': '/repo/' + str(self.peer_output.relative_to(ROOT)), 'mode': mode, 'token': self.token('operator'), 'resumeToken': token or self.request('GET', '/v1/feed/incidents', role='operator')['resumeToken'], 'records': records, 'clients': clients}
        config.update(extra or {})
        path = self.peer_output / 'input.json'
        path.write_text(json.dumps(config))
        self.env_command('python3 -I tools/realtime/peer.py /repo/' + str(path.relative_to(ROOT)))
        return json.loads((self.peer_output / 'peer-result.json').read_text())

    def protocol(self):
        body = self.device(101)
        body.pop('position')
        device = self.request('POST', '/v1/devices', body)['device']
        path = '/v1/feed/locations/' + self.floor['locationId'] + '/status'
        def ready():
            value = self.request('GET', path, role='operator')
            return value if any(row['deviceId'] == device['deviceId'] for row in value['devices']) else None
        snapshot = self.wait(ready, 30)
        self.runtime_lease = {'tenant': helpers['TENANT'], 'connector': self.connector, 'session': uid(), 'ttlSeconds': 2}
        state = {'topic': 'psim.ingest.state.v1', 'key': self.connector, 'value': {'envelope': self.event()['value']['envelope'], 'connectorId': self.connector, 'sessionId': self.runtime_lease['session'], 'stateReport': {'snapshot': True, 'complete': True, 'observations': [{'cursor': {'sourceRef': device['sources'][0]['externalRef'], 'sourceEpoch': '1', 'sourceSeq': '1'}, 'observedAt': helpers['utc'](), 'availability': 'DEVICE_AVAILABILITY_ONLINE', 'values': {'armed': 'true'}}]}}}
        own, other, foreign, tenant = [self.event() for _ in range(4)]
        other['value']['incidentCreated']['incident']['zoneId'] = self.zones[1]['locationId']
        foreign['value']['incidentCreated']['incident']['siteId'] = self.foreign_site
        tenant['value']['envelope']['tenantId'] = helpers['OTHER_TENANT']
        self.response_owner([own, foreign])
        private = {'topic': 'psim.response.events.v1', 'key': uid(), 'value': {'envelope': self.event()['value']['envelope'], 'notificationRequested': {'requestId': uid(), 'incidentId': own['key'], 'siteId': self.site_id}}}
        resolved = [{'topic': 'psim.response.events.v1', 'key': record['key'], 'value': {'envelope': self.event()['value']['envelope'], 'planUnavailable': {'incidentId': record['key']}}} for record in (own, foreign)]
        response = self.duty()
        result = self.peer('protocol', [own, other, foreign, tenant], extra={'foreignToken': self.token('foreign_operator'), 'tenantToken': self.token('tenant_operator'), 'otherZone': self.zones[1]['locationId'], 'response': response, 'resolved': resolved, 'private': private, 'marker': self.event(), 'stateToken': snapshot['resumeToken'], 'location': self.floor['locationId'], 'device': device['deviceId'], 'stateReport': state, 'lease': self.runtime_lease})
        for check in result['checks']:
            self.mark(check)

    def realtime(self):
        self.compose('up', '-d', '--force-recreate', 'api-2')
        self.ready('api-2')
        load = self.peer('load', [self.event() for _ in range(20)])
        self.report['load'] = load | {'resumeToken': 'redacted'}
        self.mark('500 simultaneous WSS clients on two Gateway instances with measured p99')
        self.compose('stop', '-t', '30', 'api-2')
        gap = [self.event() for _ in range(5)] + [self.duty('2')]
        self.probe(gap)
        self.compose('start', 'api-2')
        self.ready('api-2')
        replay = self.peer('replay', gap, load['resumeToken'], extra={'emitRecords': False})
        self.report['replay'] = replay | {'resumeToken': 'redacted'}
        self.mark('resume after reconnecting to another restarted Gateway without a new snapshot')

    def boundaries(self):
        admin = self.keycloak('POST', '/realms/master/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': 'admin-cli', 'username': 'admin', 'password': 'psim-dev-keycloak-admin'}, form=True)['access_token']
        path = '/admin/realms/psim/clients'
        client = self.keycloak('GET', path + '?clientId=' + self.tokens['short']['client'], admin)[0]
        client['attributes'] = client.get('attributes', {}) | {'access.token.lifespan': '5'}
        self.keycloak('PUT', path + '/' + client['id'], admin, client)
        large = [self.event() for _ in range(64)]
        for record in large:
            record['value']['incidentCreated']['incident']['title'] = 'x' * 160000
        result = self.peer('boundaries', large, extra={'shortToken': self.token('short')})
        for check in result['checks']:
            self.mark(check)

    def cleanup(self):
        self.compose('stop', '-t', '30', 'incident-service')
        self.compose('stop', '-t', '30', 'api-2')
        logs = self.compose('logs', '--no-color', 'api-2')
        (self.peer_output / 'api-2.log').write_text(logs)
        if self.report['passed']:
            identity = self.compose('ps', '-aq', 'api-2').strip()
            state = json.loads(self.run(['docker', 'inspect', identity]))[0]['State']
            assert state['ExitCode'] == 0, state
            assert not re.search(r'AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:', logs)
        super().cleanup()


def main():
    check = Check(*sys.argv[1:])
    try:
        check.prepare()
        check.security()
        check.basic()
        check.protocol()
        check.realtime()
        check.boundaries()
        check.report['passed'] = True
    finally:
        try:
            check.cleanup()
        except Exception:
            check.report['passed'] = False
            raise
        finally:
            (check.peer_output / 'result.json').write_text(json.dumps(check.report, indent=2) + '\n')
    print(json.dumps(check.report, indent=2))

if __name__ == '__main__':
    main()
