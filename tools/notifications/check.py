"""Exercise SLA ownership and notification delivery with real Kafka, PostgreSQL, TLS and identity."""
import json
from pathlib import Path
import re
import runpy
import sqlite3
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(ROOT / 'tools/response/check.py'))
ResponseCheck, uid = helpers['Check'], helpers['uid']


class Check(ResponseCheck):
    def __init__(self, project, preset, count):
        super().__init__(project, preset, count)
        self.log.close()
        self.output = ROOT / 'build/notifications' / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / 'commands.log').open('w')
        self.receiver = self.output / ('receiver-' + self.fixtures)
        self.receiver.mkdir()
        (self.receiver / 'hmac.secret').write_text('psim-fixture-hmac-secret-0000000000000000')
        (self.receiver / 'hmac.secret').chmod(0o400)
        self.notification_config = self.output / 'notification.json'
        services = json.loads(self.override.read_text())['services']
        base = services['response-engine'] | {'command': [f'/repo/build/debian13/{preset}/services/notification-service/psim-notification-service', '--config', '/repo/' + str(self.notification_config.relative_to(ROOT))]}
        services.update({'notification-service': base, 'notification-2': base | {'environment': base['environment'] | {'PSIM__SERVICE__INSTANCE': 'notification-2'}}})
        services['notification-fixture'] = base | {'image': self.toolchain, 'volumes': [str(ROOT) + ':/repo:ro', str(self.receiver) + ':/fixture'], 'ports': [], 'command': ['python3', '-I', '/repo/tools/notifications/receiver.py', '/fixture']}
        old = str(self.override)
        self.override = self.output / 'compose.json'
        self.override.write_text(json.dumps({'services': services}, indent=2))
        self.compose_cmd[self.compose_cmd.index(old)] = str(self.override)
        self.report = {'passed': False, 'checks': []}

    def nq(self, method, path='/v1/me/notifications', body=None, **options):
        return self.request(method, path, body, service=options.pop('service', 'notification-service'), **options)

    def rows(self, channel):
        with sqlite3.connect(self.receiver / 'receipts.sqlite') as db:
            return db.execute('SELECT id,payload,calls FROM receipts WHERE channel=?', (channel,)).fetchall()

    def notifications(self, request):
        raw = self.sql("SELECT COALESCE(json_agg(body),'[]'::json)::text FROM notification.notifications WHERE tenant='" + self.tenant + "' AND body->>'requestId'='" + request + "'")
        return json.loads(raw)

    def emit_notification(self, channels=(1,), user='operator', count=1, request_id=None, template='Notification fixture'):
        ids = [request_id or uid() for _ in range(count)]
        self.emit_records([{'topic': 'psim.response.events.v1', 'key': self.incident_id, 'value': {
            'envelope': {'tenantId': self.tenant, 'actor': {'kind': 'ACTOR_KIND_SERVICE', 'id': 'response-engine'}},
            'notificationRequested': {'requestId': identity, 'incidentId': self.incident_id, 'siteId': self.site_id, 'zoneId': self.zone,
                'params': {'recipients': [{'userId': self.subjects[user]}], 'channels': [f'NOTIFICATION_CHANNEL_{name}' for channel in channels for name in [{1: 'IN_APP', 2: 'EMAIL', 3: 'WEBHOOK'}[channel]]], 'template': template}}}} for identity in ids])
        return ids[0] if count == 1 else ids

    def terminal(self, identity, count=1):
        return self.wait(lambda: values if len(values := self.notifications(identity)) == count and all(value['state'] not in ('NOTIFICATION_STATE_PENDING', 'NOTIFICATION_STATE_UNKNOWN') for value in values) else None, 90)

    def refresh_admin(self):
        self.admin = self.keycloak('POST', '/realms/master/protocol/openid-connect/token', body={'grant_type': 'password', 'client_id': 'admin-cli', 'username': 'admin', 'password': 'psim-dev-keycloak-admin'}, form=True)['access_token']

    def prepare(self):
        super().prepare()
        self.refresh_admin()
        clients = '/admin/realms/psim/clients'
        self.directory_identity = self.keycloak('GET', clients + '?clientId=' + self.directory_client, self.admin)[0]['id']
        self.directory_representation = self.keycloak('GET', clients + '/' + self.directory_identity, self.admin)
        self.directory_representation['secret'] = 'psim-dev-directory+&=%'
        self.keycloak('PUT', clients + '/' + self.directory_identity, self.admin, self.directory_representation)
        self.secret.chmod(0o600)
        self.secret.write_text(self.directory_representation['secret'])
        self.secret.chmod(0o400)
        directory = {key: value for key, value in json.loads(self.config.read_text())['settings'].items() if key.startswith('directory_')}
        response = json.loads(self.response_config.read_text())
        response['settings'].update(directory)
        self.response_config.write_text(json.dumps(response, indent=2))
        self.notification_config.write_text(json.dumps({'service': {'instance': 'notification-1'}, 'runtime': {'shards': 1},
            'telemetry': {'otlp_endpoint': 'otel-collector:4317', 'otlp_tls': False, 'sampling_ratio': 0.001},
            'settings': {'allow_plaintext': True, 'pg_password_file': '/repo/.cache/event-flow/postgres.secret', 'introspection_secret_file': '/repo/.cache/event-flow/introspection.secret', 'listen_port': 8443,
                'issuer': 'http://keycloak:8080/realms/psim', **directory, 'smtp_host': 'notification-fixture', 'smtp_port': '8465', 'smtp_from': 'platform@example.test',
                'delivery_ca_file': '/repo/.cache/event-flow/ca.pem', 'webhook_urls': ['https://notification-fixture:8443/hook', 'https://127.0.0.1:8443/blocked'],
                'fixture_webhook_url': 'https://notification-fixture:8443/hook', 'webhook_secret_file': '/repo/' + str((self.receiver / 'hmac.secret').relative_to(ROOT)),
                'rate_limit': 3, 'rate_window_ms': 10000, 'retry_ms': 100, 'delivery_attempts': 6}}, indent=2))
        self.compose('up', '-d', '--force-recreate', 'incident-service', 'incident-2', 'response-engine', 'response-2', 'notification-service', 'notification-2', 'notification-fixture')
        self.response_ready()
        self.wait_response_consumers()
        for service in ('notification-service', 'notification-2'):
            self.wait(lambda: self.nq('GET', service=service), 90)
        for name in ('operator', 'bob', 'chief'):
            self.nq('PUT', '/v1/notification-contacts/' + self.subjects[name], {'siteId': self.site_id, 'email': name + '@example.test', 'webhookUrl': 'https://notification-fixture:8443/hook'}, role='admin')
        self.mark('two notification processes, real TLS SMTP/webhook fixtures and verified directory recipients')

    def wait_response_consumers(self, incident=None):
        processes = []
        for service in ('response-engine', 'response-2'):
            identity = self.compose('ps', '-aq', service).strip()
            started = self.run(['docker', 'inspect', '-f', '{{.State.StartedAt}}', identity]).strip()
            processes.append((identity, started))
        stable = {'signature': None, 'since': 0}
        def assigned():
            signature = []
            for identity, started in processes:
                logs = self.run(['docker', 'logs', '--since', started, identity])
                records = [json.loads(line) for line in logs.splitlines() if line.startswith('{')]
                changes = [value for value in records if value.get('group') == 'psim-response-engine' and value.get('msg') in ('kafka partitions assigned', 'kafka partitions revoked')]
                owned = sum(value.get('count', 0) * (1 if value['msg'] == 'kafka partitions assigned' else -1) for value in changes)
                if not changes or owned <= 0:
                    stable['signature'] = None
                    return False
                signature.append(changes[-1]['ts'])
            if signature != stable['signature']:
                stable.update(signature=signature, since=time.monotonic())
                return False
            return time.monotonic() - stable['since'] >= 2
        self.wait(assigned, 90)
        # A committed marker proves the tested partition has resumed after cooperative reassignment.
        identity = uid()
        current = self.incident(incident or self.incident_id)
        self.emit_records([{'topic': 'psim.incidents.events.v1', 'key': current['incidentId'], 'value': {
            'envelope': {'messageId': identity, 'tenantId': self.tenant},
            'signalAttached': {'incident': current, 'signalId': current['signalIds'][0]}}}])
        self.wait(lambda: self.sql("SELECT count(*) FROM response.psim_inbox WHERE consumer='incident' AND event_id='" + identity + "'") == '1', 90)

    def access(self):
        self.nq('GET', role=None, expected=401)
        self.nq('GET', '?unknown=x', expected=404)
        self.nq('GET', '/v1/me/notifications?unknown=x', expected=400)
        self.nq('GET', '/v1/me/notifications?pageSize=1001', expected=400)
        self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.site_id, 'email': 'a@b'}, expected=403)
        self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.site_id, 'email': 'a@b\r\nBcc: x@y'}, role='admin', expected=400)
        self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.site_id, 'webhookUrl': 'https://outside.invalid/'}, role='admin', expected=400)
        self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.site_id, 'webhookUrl': 'https://127.0.0.1:8443/blocked'}, role='admin', expected=400)
        self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.foreign_site}, role='admin', expected=403)
        self.nq('PUT', '/v1/notification-contacts/' + uid(), {'siteId': self.site_id}, role='admin', expected=400)
        self.refresh_admin()
        self.directory_representation['enabled'] = False
        directory_path = '/admin/realms/psim/clients/' + self.directory_identity
        self.keycloak('PUT', directory_path, self.admin, self.directory_representation)
        try:
            self.nq('PUT', '/v1/notification-contacts/' + self.subjects['operator'], {'siteId': self.site_id}, role='admin', expected=503)
        finally:
            self.directory_representation['enabled'] = True
            self.keycloak('PUT', directory_path, self.admin, self.directory_representation)
        profile_path = '/admin/realms/psim/users/' + self.subjects['bob']
        profile = self.keycloak('GET', profile_path, self.admin)
        original_attributes = profile['attributes']
        profile['attributes'] = {name: value for name, value in original_attributes.items() if name != 'psim_tenant'}
        self.keycloak('PUT', profile_path, self.admin, profile)
        try:
            self.nq('PUT', '/v1/notification-contacts/' + self.subjects['bob'], {'siteId': self.site_id}, role='admin', expected=400)
        finally:
            profile['attributes'] = original_attributes
            self.keycloak('PUT', profile_path, self.admin, profile)
        path = '/v1/notification-contacts/' + self.subjects['operator']
        contact = {'siteId': self.site_id, 'email': 'operator@example.test', 'webhookUrl': 'https://notification-fixture:8443/hook'}
        self.nq('PUT', path, contact, role='admin', expected=428)
        self.nq('PUT', path, contact, role='admin', version=999, expected=412)
        key = uid()
        saved = self.nq('PUT', path, contact, role='admin', version=1, key=key)
        assert self.nq('PUT', path, contact, role='admin', version=1, key=key) == saved
        self.nq('PUT', path, contact | {'email': 'changed@example.test'}, role='admin', version=1, key=key, expected=422)
        identity = self.emit_notification()
        value = self.terminal(identity)[0]
        assert len(self.nq('GET')['notifications']) == 1
        self.nq('POST', '/v1/me/notifications/' + value['notificationId'] + ':read', {}, role='bob', expected=403)
        self.nq('POST', '/v1/me/notifications/' + value['notificationId'] + ':read', {}, role='tenant', expected=404)
        key = uid()
        result = self.nq('POST', '/v1/me/notifications/' + value['notificationId'] + ':read', {}, key=key)
        assert self.nq('POST', '/v1/me/notifications/' + value['notificationId'] + ':read', {}, key=key, service='notification-2') == result
        assert self.nq('GET', '/v1/me/notifications?unreadOnly=true')['unreadCount'] == 0
        self.emit_notification(request_id=identity)
        assert len(self.notifications(identity)) == 1
        self.mark('owner isolation, tenant and site scope, strict API validation and duplicate request identity')

    def group_scope(self):
        self.refresh_admin()
        groups = '/admin/realms/psim/groups'
        name = 'notification-scopes-' + self.fixtures
        self.keycloak('POST', groups, self.admin, {'name': name, 'attributes': {'psim_scopes': ['site:' + self.site_id]}})
        parent = next(group for group in self.keycloak('GET', groups, self.admin) if group['name'] == name)
        self.keycloak('POST', groups + '/' + parent['id'] + '/children', self.admin, {'name': 'recipients'})
        child = self.keycloak('GET', groups + '/' + parent['id'] + '/children', self.admin)[0]
        user = '/admin/realms/psim/users/' + self.subjects['bob']
        profile = self.keycloak('GET', user, self.admin)
        membership = user + '/groups/' + child['id']
        self.keycloak('PUT', membership, self.admin)
        self.keycloak('PUT', user, self.admin, profile | {'attributes': {'psim_tenant': [self.tenant]}})
        try:
            self.nq('PUT', '/v1/notification-contacts/' + self.subjects['bob'], {'siteId': self.site_id, 'email': 'bob@example.test', 'webhookUrl': 'https://notification-fixture:8443/hook'}, role='admin', version=1)
        finally:
            self.keycloak('PUT', user, self.admin, profile)
            self.keycloak('DELETE', membership, self.admin)
        self.mark('recipient eligibility includes ancestor group scopes without a direct user grant')

    def plan_delivery(self):
        plan = self.publish(self.plan([{'stepId': 'notify', 'title': 'Notify operator', 'notify': {'recipients': [{'userId': self.subjects['operator']}], 'channels': ['NOTIFICATION_CHANNEL_IN_APP'], 'template': 'Plan notification'}}]))
        incident = self.new_incident()
        self.at(incident, state='RUN_STATE_COMPLETED')
        self.wait(lambda: self.sql("SELECT count(*) FROM notification.notifications WHERE body->>'incidentId'='" + incident + "' AND body->>'state'='NOTIFICATION_STATE_DELIVERED'") == '1')
        self.wait(lambda: any(entry['kind'] == 'TIMELINE_ENTRY_KIND_NOTIFICATION' for entry in self.request('GET', '/v1/incidents/' + incident + '/timeline')['entries']))
        self.archive(plan)
        self.mark('response plan intent reaches Notification Service once and records delivery in incident timeline')

    def delivery(self):
        for name in ('smtp-down', 'webhook-down'):
            (self.receiver / name).touch()
        identity = self.emit_notification((2, 3))
        self.wait(lambda: len(values := self.notifications(identity)) == 2 and all(value.get('attempts', 0) >= 1 and value['state'] == 'NOTIFICATION_STATE_PENDING' for value in values))
        self.kill_response('notification-service', 'notification-2')
        for name in ('smtp-down', 'webhook-down'):
            (self.receiver / name).unlink()
        self.compose('up', '-d', 'notification-service', 'notification-2')
        values = self.terminal(identity, 2)
        assert {value['state'] for value in values} == {'NOTIFICATION_STATE_SENT', 'NOTIFICATION_STATE_DELIVERED'}
        for value in values:
            channel = 'smtp' if value['channel'] == 'NOTIFICATION_CHANNEL_EMAIL' else 'webhook'
            assert next(row for row in self.rows(channel) if row[0] == value['notificationId'])[2] == 1
            assert value['receiptId']
            assert int(self.sql("SELECT count(*) FROM notification.psim_outbox WHERE topic='psim.audit.v1' AND position(convert_to('" + value['receiptId'] + "','UTF8') in payload)>0")) >= 1
        self.emit_notification((2, 3), request_id=identity)
        assert len(self.notifications(identity)) == 2
        self.mark('SMTP and webhook outages, concurrent delivery, SIGKILL recovery, stable IDs and accepted receipts without duplicate effects')

    def ambiguous(self):
        (self.receiver / 'smtp-lost').touch()
        identity = self.emit_notification((2,), user='bob')
        value = self.wait(lambda: next((value for value in self.notifications(identity) if value['state'] == 'NOTIFICATION_STATE_UNKNOWN' and any(row[0] == value['notificationId'] for row in self.rows('smtp'))), None))
        self.kill_response('notification-service', 'notification-2')
        self.compose('up', '-d', 'notification-service', 'notification-2')
        for service in ('notification-service', 'notification-2'):
            self.wait(lambda: self.nq('GET', service=service))
        current = self.notifications(identity)[0]
        result = self.nq('POST', '/v1/notifications/' + value['notificationId'] + ':resolve', {'accepted': True, 'receiptId': 'smtp-' + value['notificationId']}, role='admin', version=current['version'])
        assert result['notification']['state'] == 'NOTIFICATION_STATE_SENT'
        assert next(row for row in self.rows('smtp') if row[0] == value['notificationId'])[2] == 1
        (self.receiver / 'smtp-lost').unlink()
        (self.receiver / 'webhook-lost').touch()
        identity = self.emit_notification((3,), user='bob')
        value = self.terminal(identity)[0]
        assert next(row for row in self.rows('webhook') if row[0] == value['notificationId'])[2] == 2
        (self.receiver / 'webhook-lost').unlink()
        self.mark('lost SMTP acceptance requires receipt reconciliation without resend; lost webhook receipt retries the identical signed body and deduplicates at receiver')

    def failures(self):
        (self.receiver / 'webhook-reject').touch()
        identity = self.emit_notification((3,), user='chief')
        value = self.terminal(identity)[0]
        assert value['state'] == 'NOTIFICATION_STATE_FAILED' and value['failureCode'] == 'delivery_rejected'
        (self.receiver / 'webhook-reject').unlink()
        (self.receiver / 'webhook-down').touch()
        identity = self.emit_notification((3,), user='bob')
        value = self.terminal(identity)[0]
        assert value['state'] == 'NOTIFICATION_STATE_FAILED' and value['failureCode'] == 'attempts_exhausted' and value['attempts'] == 6
        (self.receiver / 'webhook-down').unlink()
        self.mark('permanent webhook rejection and bounded retry exhaustion retain failed notifications and audit')

    def webhook_recovery(self):
        (self.receiver / 'webhook-down').touch()
        identity = self.emit_notification((3,), user='operator')
        self.wait(lambda: any(value.get('attempts') == 5 and value['state'] == 'NOTIFICATION_STATE_PENDING' for value in self.notifications(identity)), 90)
        (self.receiver / 'webhook-hold').touch()
        (self.receiver / 'webhook-down').unlink()
        value = self.wait(lambda: next((value for value in self.notifications(identity) if value.get('attempts') == 6 and any(row[0] == value['notificationId'] for row in self.rows('webhook'))), None), 90)
        self.kill_response('notification-service', 'notification-2')
        # Expire the durable lease without waiting for wall time; payload and attempt budget are unchanged.
        self.sql("UPDATE notification.notifications SET due_ms=0 WHERE id='" + value['notificationId'] + "' AND status='sending'")
        (self.receiver / 'webhook-hold').unlink()
        self.compose('up', '-d', 'notification-service', 'notification-2')
        value = self.terminal(identity)[0]
        assert value['state'] == 'NOTIFICATION_STATE_DELIVERED' and value['attempts'] == 6
        assert next(row for row in self.rows('webhook') if row[0] == value['notificationId'])[2] == 2
        self.mark('SIGKILL after webhook acceptance on the final attempt reconciles the original receipt without repeating the remote effect')

    def exhausted_receipts(self):
        (self.receiver / 'webhook-lost-all').touch()
        identity = self.emit_notification((3,), user='chief')
        value = self.wait(lambda: next((value for value in self.notifications(identity) if value.get('attempts') == 6 and value['state'] == 'NOTIFICATION_STATE_UNKNOWN'), None), 90)
        assert next(row for row in self.rows('webhook') if row[0] == value['notificationId'])[2] == 6
        result = self.nq('POST', '/v1/notifications/' + value['notificationId'] + ':resolve', {'accepted': True, 'receiptId': 'webhook-' + value['notificationId']}, role='admin', version=value['version'])
        assert result['notification']['state'] == 'NOTIFICATION_STATE_DELIVERED'
        (self.receiver / 'webhook-lost-all').unlink()
        self.mark('exhausted ambiguous webhook receipts retain UNKNOWN and reconcile with receiver evidence instead of reporting a false rejection')

    def policy(self):
        policy = {'policyId': uid(), 'name': 'Incident SLA', 'sla': [{'priority': 'PRIORITY_P2', 'acknowledgeWithin': '2s', 'resolveWithin': '3s'}],
                  'levels': [{'level': 1, 'delay': '0s', 'recipients': [{'currentDuty': True}], 'channels': ['NOTIFICATION_CHANNEL_IN_APP']},
                             {'level': 2, 'delay': '4s', 'recipients': [{'currentDuty': True}], 'channels': ['NOTIFICATION_CHANNEL_IN_APP']}]}
        self.rq('POST', '/v1/escalation-policies', policy, expected=403)
        invalid = json.loads(json.dumps(policy)); invalid['levels'][1]['delay'] = '0s'
        self.rq('POST', '/v1/escalation-policies', invalid, role='admin', expected=400)
        saved = self.rq('POST', '/v1/escalation-policies', policy, role='admin')['policy']
        self.policy_id = policy['policyId']
        self.rq('PUT', '/v1/escalation-policies/' + self.policy_id, policy, role='admin', expected=428)
        self.rq('PUT', '/v1/escalation-policies/' + self.policy_id, policy, role='admin', version=999, expected=412)
        assert saved in self.rq('GET', '/v1/escalation-policies', role='admin')['policies']
        self.duty_path = '/v1/sites/' + self.site_id + '/duty'
        duty = {'siteId': self.site_id, 'userId': self.subjects['operator'], 'fallbackUserId': self.subjects['chief']}
        self.rq('PUT', self.duty_path, duty, role='operator', expected=403)
        self.rq('PUT', self.duty_path, duty | {'userId': self.subjects['other']}, role='chief', expected=400)
        self.duty = self.rq('PUT', self.duty_path, duty, role='chief')['duty']
        self.type_body['escalationPolicyId'] = self.policy_id
        self.request('PUT', '/v1/incident-types/' + self.type_id, self.type_body, role='admin', version=1)
        incident = self.new_incident()
        self.wait(lambda: self.incident(incident).get('escalationLevel') == 1)
        before = self.sql("SELECT string_agg(id||'='||due_ms::text,',' ORDER BY id) FROM escalation.timers WHERE owner='" + incident + "'")
        self.duty = self.rq('PUT', self.duty_path, duty | {'userId': self.subjects['bob']}, role='chief', version=self.duty['version'])['duty']
        assert self.sql("SELECT string_agg(id||'='||due_ms::text,',' ORDER BY id) FROM escalation.timers WHERE owner='" + incident + "'") == before
        self.coverage_checkpoint()
        self.kill_response('response-engine', 'response-2')
        self.compose('up', '-d', 'response-engine', 'response-2')
        self.response_ready()
        self.wait_response_consumers(incident)
        self.wait(lambda: self.incident(incident).get('escalationLevel') == 2)
        self.wait(lambda: self.sql("SELECT count(DISTINCT recipient) FROM notification.notifications WHERE body->>'incidentId'='" + incident + "' AND channel=1") == '2')
        self.rq('PUT', '/v1/escalation-policies/' + self.policy_id, policy | {'name': 'Updated SLA'}, role='admin', version=1)
        assert json.loads(self.sql("SELECT policy::text FROM escalation.incidents WHERE id='" + incident + "'"))['version'] == '1'
        value = self.incident(incident)
        self.request('POST', '/v1/incidents/' + incident + ':acknowledge', {}, version=value['version'])
        self.wait(lambda: int(self.sql("SELECT count(*) FROM escalation.triggers WHERE incident='" + incident + "' AND kind='resolve'")) >= 1)
        value = self.incident(incident)
        self.request('POST', '/v1/incidents/' + incident + ':start-work', {}, version=value['version'])
        value = self.incident(incident)
        self.request('POST', '/v1/incidents/' + incident + ':resolve', {'resolution': 'RESOLUTION_CONFIRMED', 'note': 'Handled'}, version=value['version'])
        self.duty = self.rq('PUT', self.duty_path, duty | {'userId': ''}, role='chief', version=self.duty['version'])['duty']
        fallback = self.new_incident()
        self.wait(lambda: self.incident(fallback).get('escalationLevel') == 1)
        self.wait(lambda: self.sql("SELECT count(*) FROM notification.notifications WHERE body->>'incidentId'='" + fallback + "' AND recipient='" + self.subjects['chief'] + "'") == '1')
        assert 'current duty unavailable' in self.sql("SELECT body->>'template' FROM notification.notifications WHERE body->>'incidentId'='" + fallback + "' AND recipient='" + self.subjects['chief'] + "'")
        value = self.incident(fallback)
        value = self.request('POST', '/v1/incidents/' + fallback + ':acknowledge', {}, version=value['version'])['incident']
        value = self.request('POST', '/v1/incidents/' + fallback + ':start-work', {}, version=value['version'])['incident']
        self.request('POST', '/v1/incidents/' + fallback + ':resolve', {'resolution': 'RESOLUTION_CONFIRMED'}, version=value['version'])
        assert int(self.sql("SELECT count(*) FROM response.psim_outbox WHERE topic='psim.audit.v1' AND position(convert_to('response.duty.missing','UTF8') in payload)>0")) >= 1
        self.duty = self.rq('PUT', self.duty_path, duty, role='chief', version=self.duty['version'])['duty']
        self.refresh_admin()
        profile_path = '/admin/realms/psim/users/' + self.subjects['operator']
        profile = self.keycloak('GET', profile_path, self.admin)
        self.token('operator')
        profile['enabled'] = False
        self.keycloak('PUT', profile_path, self.admin, profile)
        try:
            self.wait(lambda: self.request('GET', '/v1/incidents', expected=401), 5)
            inactive = self.new_incident(role='chief')
            self.wait(lambda: self.sql("SELECT count(*) FROM notification.notifications WHERE body->>'incidentId'='" + inactive + "' AND recipient='" + self.subjects['chief'] + "'") == '1')
        finally:
            profile['enabled'] = True
            self.keycloak('PUT', profile_path, self.admin, profile)
        value = self.wait(lambda: self.incident(inactive), 5)
        value = self.request('POST', '/v1/incidents/' + inactive + ':acknowledge', {}, version=value['version'])['incident']
        value = self.request('POST', '/v1/incidents/' + inactive + ':start-work', {}, version=value['version'])['incident']
        self.request('POST', '/v1/incidents/' + inactive + ':resolve', {'resolution': 'RESOLUTION_CONFIRMED'}, version=value['version'])
        error = float(self.sql("SELECT COALESCE(max(extract(epoch FROM fired_at)*1000-due_ms),0) FROM escalation.timers WHERE fired_at IS NOT NULL AND owner IN ('" + incident + "','" + fallback + "')"))
        assert error <= 1000, error
        self.report['sla_timer_max_error_ms'] = error
        self.mark('ack/resolve deadlines, multiple levels, pinned policy, changing duty without SLA reset and missing-duty fallback with audit')

    def aggregation(self):
        # Isolate the rate bucket from earlier escalation deliveries.
        self.wait(lambda: time.time() % 10 < 0.4, 12)
        self.sql('DELETE FROM notification.rates')
        identities = self.emit_notification(channels=(1, 2, 3), user='chief', count=10)
        self.wait(lambda: self.sql("SELECT count(*) FROM notification.notifications WHERE body->>'requestId' IN (" + ','.join("'" + value + "'" for value in identities) + ")") == '30')
        values = [value for identity in identities for value in self.notifications(identity)]
        for channel in ('IN_APP', 'EMAIL', 'WEBHOOK'):
            grouped = [value for value in values if value['channel'] == 'NOTIFICATION_CHANNEL_' + channel and value['state'] == 'NOTIFICATION_STATE_AGGREGATED']
            assert len(grouped) == 7, values
            summary = grouped[0]['data']['summaryId']
            state = 'NOTIFICATION_STATE_SENT' if channel == 'EMAIL' else 'NOTIFICATION_STATE_DELIVERED'
            self.wait(lambda: self.sql("SELECT body->>'state' FROM notification.notifications WHERE id='" + summary + "'") == state)
            result = json.loads(self.sql("SELECT body::text FROM notification.notifications WHERE id='" + summary + "'"))
            assert result['data']['count'] == '7' and result['data']['firstTemplate'] == 'Notification fixture'
            assert set(result['aggregatedNotificationIds']) == {value['notificationId'] for value in grouped}
            if channel == 'IN_APP':
                visible = {value['notificationId'] for value in self.nq('GET', role='chief')['notifications']}
                assert summary in visible and not visible.intersection(value['notificationId'] for value in grouped)
            else:
                rows = self.rows('smtp' if channel == 'EMAIL' else 'webhook')
                assert next(row for row in rows if row[0] == summary)[2] == 1
        self.mark('all three channels create one durable summary preserving every child identity; aggregated children do not appear as separate unread in-app notifications')

    def checkpoint(self):
        if self.preset != 'coverage':
            return
        self.compose('stop', '-t', '35', 'notification-service', 'notification-2')
        self.compose('up', '-d', 'notification-service', 'notification-2')
        for service in ('notification-service', 'notification-2'):
            self.wait(lambda: self.nq('GET', service=service))

    def cleanup(self):
        try:
            self.compose('stop', '-t', '35', 'notification-service', 'notification-2', 'notification-fixture')
            logs = self.compose('logs', '--no-color', 'notification-service', 'notification-2', 'notification-fixture')
            (self.output / 'notification-servers.log').write_text(logs)
            assert not re.search(r'AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:', logs), logs[-2000:]
            if self.report['passed']:
                for service in ('notification-service', 'notification-2'):
                    identity = self.compose('ps', '-aq', service).strip()
                    state = json.loads(self.run(['docker', 'inspect', identity]))[0]['State']
                    assert state['ExitCode'] == 0, (service, state)
        except Exception:
            self.report['passed'] = False
            raise
        finally:
            super().cleanup()


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.access()
        check.group_scope()
        check.plan_delivery()
        check.checkpoint()
        check.delivery()
        check.checkpoint()
        check.env_command('python3 -I tools/notifications/probe.py /repo/' + str(check.receiver.relative_to(ROOT)))
        check.mark('webhook freshness window, HMAC, immutable body and notification identity binding reject invalid requests')
        check.ambiguous()
        check.failures()
        check.checkpoint()
        check.webhook_recovery()
        check.exhausted_receipts()
        check.policy()
        check.aggregation()
        check.report['passed'] = True
    finally:
        check.cleanup()


if __name__ == '__main__':
    main()
