"""Exercise public deployment APIs with fresh installation credentials and verified TLS."""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import ssl
import struct
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zlib

CASE = Path('/case')
STATE = CASE / 'scenario.json'


def png(width=64, height=64):
    def chunk(name, body):
        return struct.pack('>I', len(body)) + name + body + struct.pack('>I', zlib.crc32(name + body))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress((b'\0' + b'\xff' * width * 4) * height)) + chunk(b'IEND', b''))


class API:
    def __init__(self, origin):
        self.origin = origin
        self.context = ssl.create_default_context(cafile=str(CASE / 'ca.pem'))
        body = urllib.parse.urlencode({'grant_type': 'client_credentials', 'client_id': 'deployment-admin',
                                      'client_secret': (CASE / 'identity.secret').read_text().strip()}).encode()
        request = urllib.request.Request(origin + '/identity/realms/psim/protocol/openid-connect/token', data=body)
        with urllib.request.urlopen(request, context=self.context, timeout=15) as response:
            self.token = json.load(response)['access_token']

    def request(self, method, path, body=None, version=None, content_type='application/json'):
        headers = {'Authorization': 'Bearer ' + self.token}
        if method in ('POST', 'PUT', 'PATCH', 'DELETE'): headers['Idempotency-Key'] = str(uuid.uuid4())
        if version is not None: headers['If-Match'] = '"' + str(version) + '"'
        if body is not None:
            headers['Content-Type'] = content_type
            if not isinstance(body, bytes): body = json.dumps(body).encode()
        request = urllib.request.Request(self.origin + path, data=body, headers=headers, method=method)
        try:
            with urllib.request.urlopen(request, context=self.context, timeout=30) as response:
                value = response.read(); media = response.headers.get_content_type()
        except urllib.error.HTTPError as error:
            raise RuntimeError(method + ' ' + path + ': HTTP ' + str(error.code) + ' ' + error.read(300).decode(errors='replace')) from None
        return json.loads(value) if media == 'application/json' else value

    def location(self, kind, parent=None):
        body = {'locationId': str(uuid.uuid4()), 'kind': 'LOCATION_KIND_' + kind,
                'name': 'Qualification ' + kind.lower(), 'code': uuid.uuid4().hex[:16]}
        if parent: body['parentId'] = parent['locationId']
        if kind == 'SITE': body['timezone'] = 'UTC'
        return self.request('POST', '/v1/locations', body)['location']


def save(value):
    if STATE.exists(): STATE.chmod(0o600)
    STATE.write_text(json.dumps(value, indent=2) + '\n'); STATE.chmod(0o400)


def floor(api):
    if STATE.exists():
        print('Qualification floor already initialized'); return
    site = api.location('SITE'); building = api.location('BUILDING', site); location = api.location('FLOOR', building)
    image = png()
    location = api.request('PUT', '/v1/locations/' + location['locationId'] + '/floor-plan?width=64&height=64',
                           image, version=location['version'], content_type='image/png')['location']
    actual = api.request('GET', '/v1/locations/' + location['locationId'] + '/floor-plan')
    if hashlib.sha256(actual).digest() != hashlib.sha256(image).digest(): raise RuntimeError('floor plan content differs after upload')
    save({'site': site['locationId'], 'building': building['locationId'], 'floor': location['locationId'],
          'version': location['version'], 'sha256': hashlib.sha256(image).hexdigest(),
          'object_key': location['floorPlan']['file']['objectKey']})
    print('Public floor creation, private S3 upload and authorized download verified')


def read(api):
    state = json.loads(STATE.read_text())
    actual = api.request('GET', '/v1/locations/' + state['floor'] + '/floor-plan')
    if hashlib.sha256(actual).hexdigest() != state['sha256']: raise RuntimeError('current floor plan differs during fault')
    print('Current floor plan remains readable with its original checksum')


def upload(api):
    state = json.loads(STATE.read_text()); location = api.request('GET', '/v1/locations/' + state['floor'])['location']
    image = png(65, 64)
    location = api.request('PUT', '/v1/locations/' + state['floor'] + '/floor-plan?width=65&height=64', image,
                           version=location['version'], content_type='image/png')['location']
    if int(location['version']) <= int(state['version']): raise RuntimeError('floor plan version did not advance')
    state.update(version=location['version'], sha256=hashlib.sha256(image).hexdigest(), object_key=location['floorPlan']['file']['objectKey'])
    save(state); read(api)
    print('New immutable floor plan version uploaded after recovery')


def anonymous():
    state = json.loads(STATE.read_text()); context = ssl.create_default_context(cafile=str(CASE / 'ca.pem'))
    url = 'https://s3:18446/psim-plans/' + urllib.parse.quote(state['object_key'], safe='/')
    try:
        with urllib.request.urlopen(url, context=context, timeout=10) as response:
            raise RuntimeError('existing floor object is anonymously accessible: ' + str(response.status))
    except urllib.error.HTTPError as error:
        if error.code != 403: raise RuntimeError('unexpected anonymous object response: ' + str(error.code)) from None
    print('Anonymous read of an existing floor object rejected with HTTP 403')


def wait(predicate, message, timeout=90):
    deadline = time.monotonic() + timeout
    unavailable = ''
    while time.monotonic() < deadline:
        try:
            result = predicate()
            if result: return result
        except RuntimeError as error:
            if 'HTTP 503 ' not in str(error): raise
            unavailable = str(error)
        time.sleep(0.5)
    raise RuntimeError(message + (': ' + unavailable if unavailable else ''))


def setup(api):
    floor(api)
    state = json.loads(STATE.read_text())
    if 'device' in state:
        print('Qualification catalog and rule already initialized'); return
    connector = json.loads((CASE / 'identities.json').read_text())[19]
    wait(lambda: any(item['connector']['connectorId'] == connector for item in api.request('GET', '/v1/connectors?pageSize=1000')['connectors']),
         'enrolled connector did not reach the catalog')
    zone = api.location('ZONE', {'locationId': state['floor']})
    kind = api.request('POST', '/v1/device-types', {'name': 'Qualification alarm sensor', 'code': uuid.uuid4().hex,
            'sourceTemplates': [{'kind': 'alarm', 'name': 'Alarm'}]})['deviceType']
    device = api.request('POST', '/v1/devices', {'deviceId': str(uuid.uuid4()), 'deviceTypeId': kind['deviceTypeId'],
            'zoneId': zone['locationId'], 'siteId': state['site'], 'connectorId': connector,
            'externalId': 'qualification-sensor', 'name': 'Qualification sensor',
            'sources': [{'externalRef': 'door/alarm', 'kind': 'alarm', 'name': 'Alarm'}], 'position': {'x': 32, 'y': 32}})['device']
    connector_type = json.loads((CASE / 'config.json').read_text())['connector_type']
    mappings = api.request('GET', '/v1/mappings')['mappingRuleSets']
    version = next((item['version'] for item in mappings if item['connectorType'] == connector_type), 0)
    api.request('PUT', '/v1/mappings/' + connector_type, {'connectorType': connector_type, 'taxonomyVersion': '1',
            'rules': [{'rawCode': 'ALARM*', 'condition': 'raw.raw_code.startsWith("ALARM")', 'type': 'intrusion.zone.alarm'},
                      {'rawCode': 'BACKGROUND', 'condition': 'raw.raw_code == "BACKGROUND"', 'type': 'intrusion.zone.restore'}]}, version)
    incident_type = str(uuid.uuid4()); rule = str(uuid.uuid4())
    api.request('POST', '/v1/incident-types', {'incidentTypeId': incident_type, 'code': uuid.uuid4().hex,
            'name': 'Qualification intrusion', 'defaultPriority': 'PRIORITY_P2', 'groupingWindow': '60s', 'autoCloseAfter': '300s'})
    api.request('POST', '/v1/rules', {'ruleId': rule, 'name': 'Qualification intrusion rule',
            'correlationScope': 'CORRELATION_SCOPE_ZONE', 'filter': {'types': ['intrusion.zone.alarm'], 'siteIds': [state['site']]},
            'window': '1s', 'single': {}, 'output': {'incidentTypeId': incident_type, 'priority': 'PRIORITY_P2',
            'summaryTemplate': 'Alarm ${event.zone_id}'}})
    checked = api.request('POST', '/v1/rules/' + rule + ':validate', {'version': 1}, 1)
    if not checked['valid']: raise RuntimeError('qualification rule failed validation')
    current = api.request('GET', '/v1/rule-sets/active')['ruleSet']['version']
    api.request('POST', '/v1/rule-sets:activate', {'rules': [{'ruleId': rule, 'version': 1}],
            'expectedRuleSetVersion': current}, current)
    state.update(connector=connector, device=device['deviceId'], zone=zone['locationId'], incident_type=incident_type, rule=rule)
    save(state)
    print('Fresh connector, catalog, normalization mapping and correlation rule initialized through public APIs')


def setup_load(api):
    state = json.loads(STATE.read_text())
    if state.get('load_devices') == 10000:
        print('LOAD-50K catalog already prepared'); return
    device_type = api.request('GET', '/v1/devices/' + state['device'])['device']['deviceTypeId']
    zones = [(state['site'], state['zone'])]
    for _ in range(9):
        site = api.location('SITE'); building = api.location('BUILDING', site)
        floor_location = api.location('FLOOR', building); zone = api.location('ZONE', floor_location)
        zones.append((site['locationId'], zone['locationId']))
    jobs = []
    for site_index, (site, zone) in enumerate(zones):
        rows = [{'deviceId': str(uuid.uuid4()), 'deviceTypeId': device_type,
                 'zoneId': zone, 'siteId': site, 'connectorId': state['connector'],
                 'externalId': 'load-' + str(site_index) + '-' + str(index), 'name': 'Load device',
                 'sources': [{'externalRef': 'load/' + str(site_index) + '/' + str(index), 'kind': 'alarm', 'name': 'Load source'}]}
                for index in range(1000) if site_index != 0 or index != 0]
        for begin in range(0, len(rows), 500):
            jobs.append(api.request('POST', '/v1/imports?format=json', rows[begin:begin + 500])['importJob']['importId'])
    deadline = time.monotonic() + 1200
    pending = set(jobs)
    refreshed = time.monotonic()
    while pending:
        if time.monotonic() - refreshed > 60:
            api = API(api.origin); refreshed = time.monotonic()
        for identifier in list(pending):
            report = api.request('GET', '/v1/imports/' + identifier)['importJob']
            if report['state'] == 'IMPORT_STATE_COMPLETED':
                if int(report.get('rowsFailed', 0)) or not int(report.get('rowsCreated', 0)):
                    raise RuntimeError('LOAD-50K catalog import failed: ' + json.dumps(report))
                pending.remove(identifier)
        if time.monotonic() >= deadline: raise RuntimeError('LOAD-50K catalog import timed out')
        if pending: time.sleep(1)
    state.update(load_devices=10000, load_sites=[site for site, _ in zones], load_imports=jobs)
    save(state)
    print('LOAD-50K catalog prepared: 10000 devices on 10 sites')


def verify(api):
    state = json.loads(STATE.read_text()); event = json.loads((CASE / 'event.json').read_text())['event_id']
    now = dt.datetime.now(dt.timezone.utc)
    query = urllib.parse.urlencode({'from': (now - dt.timedelta(minutes=10)).isoformat(),
            'to': (now + dt.timedelta(minutes=5)).isoformat(), 'siteIds': state['site'],
            'deviceIds': state['device'], 'pageSize': 100})
    wait(lambda: any(item['eventId'] == event for item in api.request('GET', '/v1/events?' + query)['events']),
         'SDK event did not reach normalized event history')
    incident = wait(lambda: next((item for item in api.request('GET', '/v1/incidents?siteIds=' + state['site'])['incidents']
                    if item['incidentTypeId'] == state['incident_type']), None), 'correlated incident did not reach the incident API')
    wait(lambda: any(item['incidentId'] == incident['incidentId'] for item in
                    api.request('GET', '/v1/feed/incidents?siteIds=' + state['site'])['incidents']),
         'correlated incident did not reach the shared console projection')
    read(api)
    print('Connector SDK -> raw Kafka -> normalized history -> correlated incident -> console projection verified')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('action', choices=['floor', 'read', 'upload', 'anonymous', 'setup', 'setup-load', 'verify'])
    parser.add_argument('--origin', default='https://localhost:8443'); args = parser.parse_args()
    if args.action == 'anonymous': anonymous()
    else: {'floor': floor, 'read': read, 'upload': upload, 'setup': setup, 'setup-load': setup_load, 'verify': verify}[args.action](API(args.origin))
