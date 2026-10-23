"""Check the administrative mTLS gRPC boundary with every role and scope class."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import uuid


def main(config):
    count = 0
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        request = root / 'request.json'
        token = root / 'access.token'
        for name, bearer in config['tokens'].items():
            role, scope = name.rsplit('_', 1)
            request.write_text(json.dumps({'requestId': str(uuid.uuid4()), 'action': 'REGISTRATION_ACTION_INSPECT', 'connector': {'connectorId': '22222222-2222-4222-8222-222222222222'}}))
            token.chmod(0o600) if token.exists() else None
            token.write_text(bearer)
            token.chmod(0o400)
            result = subprocess.run([f"build/debian13/{config['preset']}/tools/gateway-admin/psim-gateway-admin", 'connector-gateway:8080', '.cache/event-flow', str(request), str(token)], capture_output=True, text=True, timeout=30)
            allowed = role in ('admin', 'integrator') and scope == 'global'
            assert (result.returncode == 0) == allowed, (name, result.stderr)
            if not allowed:
                assert 'AUTH_FORBIDDEN' in result.stderr, (name, result.stderr)
            count += 1
    print(f'connector authorization: {count} role and scope combinations')


if __name__ == '__main__':
    main(json.loads(Path(sys.argv[1]).read_text()))
