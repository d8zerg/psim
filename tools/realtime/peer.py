"""Real WSS clients with bounded RFC 6455 framing, replay and load measurements."""
import asyncio
import base64
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import ssl
import socket
import struct
import sys
import time
import uuid
import urllib.request

ROOT = Path(__file__).resolve().parents[2]

class Client:
    @classmethod
    async def open(cls, token, host='api-gateway', types=None, receive_buffer=None):
        self = cls()
        self.reader, self.writer = await asyncio.open_connection(host, 8443, ssl=ssl.create_default_context(cafile=ROOT / '.cache/event-flow/ca.pem'), server_hostname='api-gateway', limit=65536)
        if receive_buffer:
            self.writer.get_extra_info('socket').setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
        key = base64.b64encode(os.urandom(16)).decode()
        self.writer.write((f'GET /v1/realtime HTTP/1.1\r\nHost: api-gateway\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: {key}\r\nOrigin: https://console.example\r\n\r\n').encode())
        headers = await asyncio.wait_for(self.reader.readuntil(b'\r\n\r\n'), 10)
        assert headers.startswith(b'HTTP/1.1 101'), headers
        expected = base64.b64encode(hashlib.sha1((key + '258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        assert expected in headers
        if token is None:
            return self
        await self.send({'authenticate': {'accessToken': token, **({'supportedChangeTypes': types} if types else {})}})
        assert 'authenticated' in await self.receive()
        return self

    async def raw(self, data, opcode=1):
        mask = os.urandom(4)
        size = len(data)
        header = bytes([0x80 | opcode, 0x80 | (size if size < 126 else 126 if size < 65536 else 127)])
        if size >= 126:
            header += struct.pack('!H' if size < 65536 else '!Q', size)
        self.writer.write(header + mask + bytes(value ^ mask[i % 4] for i, value in enumerate(data)))
        await self.writer.drain()

    async def send(self, data):
        await self.raw(json.dumps(data, separators=(',', ':')).encode())

    async def receive(self, timeout=10):
        async def frame():
            payload = bytearray()
            while True:
                first, second = await self.reader.readexactly(2)
                size = second & 127
                if size >= 126:
                    size = struct.unpack('!H' if size == 126 else '!Q', await self.reader.readexactly(2 if size == 126 else 8))[0]
                assert size <= 8 * 1024 * 1024 and not second & 128
                body = await self.reader.readexactly(size)
                opcode = first & 15
                if opcode == 8:
                    raise EOFError('websocket closed: ' + repr(body))
                if opcode == 9:
                    await self.raw(body, 10)
                    continue
                if opcode == 10:
                    continue
                assert opcode in (0, 1)
                payload.extend(body)
                if first & 128:
                    return json.loads(payload)
        return await asyncio.wait_for(frame(), timeout)

    async def subscribe(self, token, identity='feed', stream='STREAM_INCIDENTS', filter=None):
        await self.send({'subscribe': {'subscriptionId': identity, 'stream': stream, 'resumeToken': token, 'filter': filter or {}}})
        reply = await self.receive()
        assert 'subscribed' in reply, reply

    async def close(self):
        try:
            await self.raw(struct.pack('!H', 1000), 8)
        except (OSError, EOFError):
            pass
        self.writer.close()
        try:
            await asyncio.wait_for(self.writer.wait_closed(), 3)
        except (OSError, TimeoutError):
            pass

async def emit(config, records, leases=None):
    now = dt.datetime.now(dt.timezone.utc).isoformat().replace('+00:00', 'Z')
    for record in records:
        record['value']['envelope']['producedAt'] = now
        record['value']['envelope']['occurredAt'] = now
    path = Path(config['output']) / ('emit-' + uuid.uuid4().hex + '.json')
    path.write_text(json.dumps({'records': records, 'leases': leases or [], 'stampProducedAt': True}))
    process = await asyncio.create_subprocess_exec(f"/repo/build/debian13/{config['preset']}/tools/api-gateway/psim-api-probe", str(path), stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
    out, err = await process.communicate()
    assert process.returncode == 0, (out, err)

async def load(config):
    count = config.get('clients', 500)
    semaphore = asyncio.Semaphore(32)
    async def connect(index):
        async with semaphore:
            client = await Client.open(config['token'], host='api-gateway' if index % 2 == 0 else 'api-2')
            await client.subscribe(config['resumeToken'])
            return client
    print(f'load: connecting {count} WSS clients', flush=True)
    clients = await asyncio.gather(*(connect(i) for i in range(count)))
    print(f'load: {len(clients)} WSS subscriptions established', flush=True)
    measured = []
    records = config['records']
    expected = {row['value']['incidentCreated']['incident']['incidentId'] for row in records}
    async def read(client):
        found = set()
        while found != expected:
            message = await client.receive(30)
            delta = message.get('delta')
            assert delta and 'incidentEvent' in delta, message
            event = delta['incidentEvent']
            change = next(value for key, value in event.items() if key != 'envelope')
            identity = change['incident']['incidentId']
            if identity in expected:
                assert identity not in found, 'duplicate live event'
                found.add(identity)
                measured.append((time.time() - dt.datetime.fromisoformat(event['envelope']['producedAt'].replace('Z', '+00:00')).timestamp()) * 1000)
        return delta['resumeToken']
    tasks = [asyncio.create_task(read(client)) for client in clients]
    try:
        for index, record in enumerate(records):
            await emit(config, [record])
            await asyncio.sleep(0.2)
            print(f'load: published {index + 1}/{len(records)}, received {len(measured)}', flush=True)
        tokens = await asyncio.gather(*tasks)
        assert len(measured) == count * len(records)
        ordered = sorted(measured)
        p99 = ordered[math.ceil(len(ordered) * .99) - 1]
        result = {'clients': count, 'samples': len(measured), 'p99Ms': p99, 'maxMs': max(measured), 'resumeToken': tokens[0]}
        Path(config['output'], 'peer-result.json').write_text(json.dumps(result, indent=2))
        if config['preset'] in ('debug', 'release', 'relwithdebinfo'):
            assert p99 <= 200, p99
        return result
    finally:
        for task in tasks:
            task.cancel()
        await asyncio.gather(*(client.close() for client in clients), return_exceptions=True)

async def replay(config):
    if config.get('emitRecords', True):
        await emit(config, config['records'])
    expected = {row['value']['envelope']['messageId'] for row in config['records']}
    semaphore = asyncio.Semaphore(32)
    async def one():
        async with semaphore:
            client = await Client.open(config['token'], host='api-2', types=['incident_event', 'response_event'])
            await client.subscribe(config['resumeToken'])
        try:
            found = set()
            while found != expected:
                delta = (await client.receive())['delta']
                event = delta.get('incidentEvent') or delta.get('responseEvent')
                identity = event['envelope']['messageId']
                if identity in expected:
                    assert identity not in found
                    found.add(identity)
            return delta['resumeToken']
        finally:
            await client.close()
    tokens = await asyncio.gather(*(one() for _ in range(config.get('clients', 1))))
    return {'replayed': len(expected), 'clients': len(tokens), 'deliveries': len(tokens) * len(expected), 'resumeToken': tokens[0]}

async def protocol(config):
    legacy = await Client.open(config['token'])
    modern = await Client.open(config['token'], types=['incident_event', 'response_event', 'location_status', 'incident_operational_updated', 'future_payload'])
    foreign = await Client.open(config['foreignToken'], types=['incident_event', 'response_event'])
    tenant = await Client.open(config['tenantToken'])
    clients = [legacy, modern, foreign, tenant]
    checks = []
    try:
        await legacy.subscribe(config['resumeToken'])
        await modern.subscribe(config['resumeToken'])
        await modern.subscribe(config['resumeToken'], 'zone', filter={'zoneIds': [config['otherZone']]})
        selected = config['records'][0]['value']['incidentCreated']['incident']['incidentId']
        await modern.subscribe(config['resumeToken'], 'incident', 'STREAM_INCIDENT', {'incidentId': selected})
        await foreign.send({'subscribe': {'subscriptionId': 'old-scope', 'stream': 'STREAM_INCIDENTS', 'resumeToken': config['resumeToken']}})
        assert (await foreign.receive())['resyncRequired']['reason'] == 'access scope changed'
        await foreign.subscribe(config['foreignResumeToken'])
        checks.append('resume cursor binds the effective identity and scope; a changed context requires a new snapshot')
        peer = await Client.open(config['token'])
        try:
            await peer.subscribe(config['resumeToken'])
            await peer.send({'authenticate': {'accessToken': config['peerToken']}})
            assert (await peer.receive())['resyncRequired']['subscriptionId'] == 'feed'
            assert 'authenticated' in await peer.receive()
            await peer.send({'subscribe': {'subscriptionId': 'previous-user', 'stream': 'STREAM_INCIDENTS', 'resumeToken': config['resumeToken']}})
            assert (await peer.receive())['resyncRequired']['reason'] == 'access scope changed'
        finally:
            await peer.close()
        checks.append('changing identity with identical roles and scope clears subscriptions and rejects the former cursor')
        await tenant.send({'subscribe': {'subscriptionId': 'wrong-tenant', 'stream': 'STREAM_INCIDENTS', 'resumeToken': config['resumeToken']}})
        assert 'resyncRequired' in await tenant.receive()
        await emit(config, config['records'])
        async def incident(client):
            message = await client.receive()
            delta = message['delta']
            event = delta['incidentEvent']
            value = next(value for key, value in event.items() if key != 'envelope')['incident']
            return delta['subscriptionId'], value['incidentId']
        own = {row['value']['incidentCreated']['incident']['incidentId'] for row in config['records'][:2]}
        assert {await incident(legacy) for _ in range(2)} == {('feed', item) for item in own}
        other = config['records'][1]['value']['incidentCreated']['incident']['incidentId']
        assert {await incident(modern) for _ in range(4)} == {('feed', selected), ('feed', other), ('zone', other), ('incident', selected)}
        assert (await incident(foreign))[1] == config['records'][2]['value']['incidentCreated']['incident']['incidentId']
        checks.append('tenant, site, zone, individual incident and simultaneous independent subscriptions')
        await emit(config, config['resolved'] + [config['private']])
        for _ in range(2):
            delta = (await modern.receive())['delta']
            assert delta['responseEvent']['planUnavailable']['incidentId'] == selected
        assert (await foreign.receive())['delta']['responseEvent']['planUnavailable']['incidentId'] == config['resolved'][1]['key']
        checks.append('response events resolve incident scope through the authorized owner; private delivery intents are excluded')
        await emit(config, [config['response']])
        assert 'responseEvent' in (await modern.receive())['delta']
        await legacy.send({'ping': {}})
        assert 'pong' in await legacy.receive()
        try:
            unexpected = await legacy.receive(.3)
            raise AssertionError(('legacy received an unsupported change', unexpected))
        except TimeoutError:
            pass
        checks.append('legacy clients receive only incident_event; response_event requires negotiation')
        operational = json.loads(json.dumps(config['records'][0]))
        snapshot = operational['value'].pop('incidentCreated')['incident']
        snapshot['version'] = '2'
        snapshot['operational'] = {'active': True, 'conditionKnown': True, 'acknowledged': True}
        operational['value']['envelope']['messageId'] = str(uuid.uuid4())
        operational['value']['envelope']['sequence'] = '2'
        operational['value']['incidentOperationalUpdated'] = {'incident': snapshot, 'action': 'operational.update-alarm'}
        await emit(config, [operational])
        for _ in range(2):
            delta = (await modern.receive())['delta']
            assert delta['incidentEvent']['incidentOperationalUpdated']['incident']['operational']['acknowledged']
        try:
            unexpected = await legacy.receive(.3)
            raise AssertionError(('legacy received operational payload', unexpected))
        except TimeoutError:
            pass
        checks.append('operational alarm images require negotiation and preserve legacy incident consumers')
        await modern.send({'unsubscribe': {'subscriptionId': 'zone'}})
        await modern.send({'unsubscribe': {'subscriptionId': 'incident'}})
        await emit(config, [config['marker']])
        assert (await incident(modern))[0] == 'feed'
        assert (await incident(legacy))[0] == 'feed'
        await modern.subscribe(config['stateToken'], 'state', 'STREAM_LOCATION_STATUS', {'locationId': config['location']})
        initial = (await modern.receive())['delta']['locationStatus']
        assert any(row['deviceId'] == config['device'] for row in initial['devices'])
        await emit(config, [config['stateReport']], [config['lease']])
        async def online(expected):
            while True:
                frame = await modern.receive()
                image = frame['delta']['locationStatus']
                device = next(row for row in image['devices'] if row['deviceId'] == config['device'])
                if device['online'] == expected:
                    return device, frame['delta']['resumeToken']
        fresh, online_token = await asyncio.wait_for(online(True), 10)
        assert fresh['sources'][0]['values'] == {'armed': 'true'}
        stale, _ = await asyncio.wait_for(online(False), 10)
        assert stale['quality'] == 'DATA_QUALITY_STALE'
        resumed = await Client.open(config['token'], types=['location_status'])
        try:
            await resumed.subscribe(online_token, 'state', 'STREAM_LOCATION_STATUS', {'locationId': config['location']})
            current = (await resumed.receive())['delta']['locationStatus']
            device = next(row for row in current['devices'] if row['deviceId'] == config['device'])
            assert not device['online'] and device['quality'] == 'DATA_QUALITY_STALE'
        finally:
            await resumed.close()
        checks.append('authoritative images converge; lease expiry and reconnect with the former online token return stale without new Kafka input')
        await modern.send({'authenticate': {'accessToken': config['foreignToken'], 'supportedChangeTypes': ['incident_event', 'response_event', 'location_status']}})
        revoked = set()
        while True:
            message = await modern.receive()
            if 'resyncRequired' in message:
                revoked.add(message['resyncRequired']['subscriptionId'])
            elif 'authenticated' in message:
                break
        assert revoked == {'feed', 'state'}, revoked
        await modern.send({'subscribe': {'subscriptionId': 'state', 'stream': 'STREAM_LOCATION_STATUS', 'filter': {'locationId': config['location']}, 'resumeToken': config['stateToken']}})
        assert 'resyncRequired' in await modern.receive()
        checks.append('reauthentication clears prior subscriptions and state owner rejects the previous access scope')
        for key, value in [('accessContext', 'different-scope'), ('issuedAtMs', 0), ('offsets', {'unknown-partition': 0}), ('offsets', {'0': 9223372036854775807})]:
            token = json.loads(config['resumeToken'])
            token[key] = value
            await legacy.send({'subscribe': {'subscriptionId': 'invalid', 'stream': 'STREAM_INCIDENTS', 'resumeToken': json.dumps(token)}})
            assert 'resyncRequired' in await legacy.receive()
        await legacy.send({'subscribe': {'subscriptionId': 'unsupported', 'stream': 'STREAM_LOCATION_STATUS', 'filter': {'locationId': config['location']}, 'resumeToken': config['stateToken']}})
        assert 'resyncRequired' in await legacy.receive()
        checks.append('expired, future and unknown partition tokens fail closed; operational streams require negotiation')
        return {'checks': checks}
    finally:
        await asyncio.gather(*(client.close() for client in clients), return_exceptions=True)

def metric(name):
    with urllib.request.urlopen('http://api-gateway:9100/metrics', timeout=5) as response:
        lines = response.read().decode().splitlines()
    return float(next(line.split()[-1] for line in lines if line.startswith(name + ' ')))

async def boundaries(config):
    checks = []
    client = await Client.open(config['shortToken'])
    try:
        try:
            await client.receive(9)
            raise AssertionError('expired JWT connection remained open')
        except (EOFError, OSError, asyncio.IncompleteReadError):
            pass
    finally:
        await client.close()
    for token in (None, 'invalid'):
        client = await Client.open(None)
        try:
            if token:
                await client.send({'authenticate': {'accessToken': token}})
                denied = await client.receive(8)
                assert denied['error']['code'] == 'AUTH_UNAUTHENTICATED' and denied['error']['fatal']
            try:
                await client.receive(8)
                raise AssertionError('unauthenticated connection remained open')
            except (EOFError, OSError, asyncio.IncompleteReadError):
                pass
        finally:
            await client.close()
    checks.append('JWT expiry, invalid credentials and authentication deadline close the connection')
    before = metric('psim_realtime_slow_consumers_total')
    slow = await Client.open(config['token'], receive_buffer=1024)
    try:
        await slow.subscribe(config['resumeToken'])
        await emit(config, config['records'])
        deadline = time.monotonic() + 15
        while True:
            rejected = metric('psim_realtime_slow_consumers_total')
            connections = metric('psim_realtime_connections')
            if rejected > before and connections == 0:
                break
            assert time.monotonic() < deadline, ('slow receiver was not disconnected within its queue/write budget',
                {'before': before, 'rejected': rejected, 'connections': connections, 'records': len(config['records'])})
            await asyncio.sleep(.1)
    finally:
        await slow.close()
    healthy = await Client.open(config['token'])
    try:
        await healthy.send({'ping': {}})
        assert 'pong' in await healthy.receive()
    finally:
        await healthy.close()
    checks.append('a receiver that stops reading is disconnected; the Gateway continues serving healthy clients')
    return {'checks': checks}

async def main(config):
    result = await {'load': load, 'replay': replay, 'protocol': protocol, 'boundaries': boundaries}[config['mode']](config)
    Path(config['output'], 'peer-result.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result))

if __name__ == '__main__':
    asyncio.run(main(json.loads(Path(sys.argv[1]).read_text())))
