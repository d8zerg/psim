"""Verify webhook freshness, identity binding and signature without accepted effects."""
import hashlib
import hmac
import json
from pathlib import Path
import ssl
import sys
import time
import urllib.error
import urllib.request
import uuid

DIRECTORY = Path(sys.argv[1])
SECRET = (DIRECTORY / 'hmac.secret').read_bytes()
CA = ssl.create_default_context(cafile='/repo/.cache/event-flow/ca.pem')
identity = str(uuid.uuid4())
payload = json.dumps({'notificationId': identity}).encode()
now = str(int(time.time()))
for timestamp, key, actual_body, actual_id in [(str(int(now) - 301), SECRET, payload, identity),
        (now, b'wrong-key', payload, identity), (now, SECRET, payload + b' ', identity),
        (now, SECRET, payload, str(uuid.uuid4()))]:
    signed = timestamp.encode() + b'.' + identity.encode() + b'.' + payload
    signature = 'sha256=' + hmac.new(key, signed, hashlib.sha256).hexdigest()
    request = urllib.request.Request('https://notification-fixture:8443/hook', data=actual_body,
        headers={'Content-Type': 'application/json', 'Idempotency-Key': actual_id,
                 'X-PSIM-Timestamp': timestamp, 'X-PSIM-Signature': signature})
    try:
        with urllib.request.urlopen(request, context=CA, timeout=5):
            raise AssertionError('invalid webhook accepted')
    except urllib.error.HTTPError as error:
        assert error.code == 403, error.code
print('webhook: stale timestamp, wrong key, changed body and changed identity rejected')
