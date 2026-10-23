"""Verify idle revocation and reauthentication over real WSS connections."""
import asyncio
import json
from pathlib import Path
import runpy
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
Client = runpy.run_path(str(ROOT / 'tools/realtime/peer.py'))['Client']
request = runpy.run_path(str(ROOT / 'tools/env/keycloak_security.py'))['request']


async def main(config):
    if config.get('mode') == 'outage':
        client = await Client.open(config['token'])
        Path(config['output'], 'outage-ready').touch()
        try:
            message = await client.receive(timeout=8)
            raise AssertionError(('outage did not close connection', message))
        except (EOFError, OSError, asyncio.IncompleteReadError):
            pass
        finally:
            await client.close()
        return
    base = 'http://keycloak:8080'
    auth = request(base, '/realms/master/protocol/openid-connect/token', data={'grant_type': 'password', 'client_id': 'admin-cli', 'username': 'admin', 'password': 'psim-dev-keycloak-admin'}, form=True)['access_token']
    user_path = '/admin/realms/psim/users/' + config['subject']
    user = request(base, user_path, auth)
    original = json.loads(json.dumps(user))
    results = []
    try:
        for subscribed in (False, True):
            user = original | {'enabled': True}
            request(base, user_path, auth, user, method='PUT')
            await asyncio.sleep(0.7)
            idle = await Client.open(config['token'])
            other = await Client.open(config['token'], host='api-2')
            # A replacement bearer with unchanged identity and rights preserves the connection.
            await idle.send({'authenticate': {'accessToken': config['token']}})
            assert 'authenticated' in await idle.receive()
            if subscribed:
                import ssl
                token = config['token']
                req = urllib.request.Request('https://api-gateway:8443/v1/feed/incidents', headers={'Authorization': 'Bearer ' + token})
                with urllib.request.urlopen(req, context=ssl.create_default_context(cafile=ROOT / '.cache/event-flow/ca.pem')) as response:
                    resume = json.load(response)['resumeToken']
                await idle.subscribe(resume)
            changed = original | ({'enabled': False} if subscribed else {'attributes': {'psim_tenant': [config['tenant']], 'psim_scopes': ['site:00000000-0000-4000-8000-000000000000']}})
            start = time.monotonic()
            request(base, user_path, auth, changed, method='PUT')
            for client in (idle, other):
                try:
                    value = await client.receive(timeout=3)
                    raise AssertionError(('revoked connection delivered data', value))
                except (EOFError, OSError, asyncio.IncompleteReadError):
                    pass
                finally:
                    await client.close()
            elapsed = time.monotonic() - start
            assert elapsed < 3, elapsed
            results.append({'subscribed': subscribed, 'revocationSeconds': elapsed})
    finally:
        request(base, user_path, auth, original, method='PUT')
    Path(config['output'], 'websocket.json').write_text(json.dumps(results, indent=2))


if __name__ == '__main__':
    asyncio.run(main(json.loads(Path(sys.argv[1]).read_text())))
