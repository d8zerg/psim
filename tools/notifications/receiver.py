"""TLS SMTP and signed webhook fixtures with persistent receipts and injected failures."""
from email import policy
from email.parser import BytesParser
import hashlib
import hmac
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import socket
import sqlite3
import ssl
import signal
import sys
import threading
import time

DIRECTORY = Path(sys.argv[1])
signal.signal(signal.SIGTERM, lambda *_: sys.exit(0))
TLS = Path('/repo/.cache/event-flow')
SECRET = (DIRECTORY / 'hmac.secret').read_bytes()
LOCK = threading.Lock()


def receipt(channel, identity, payload):
    with LOCK, sqlite3.connect(DIRECTORY / 'receipts.sqlite') as db:
        db.execute('CREATE TABLE IF NOT EXISTS receipts(channel TEXT, id TEXT, payload TEXT, calls INTEGER, PRIMARY KEY(channel,id))')
        old = db.execute('SELECT payload,calls FROM receipts WHERE channel=? AND id=?', (channel, identity)).fetchone()
        if old and old[0] != payload:
            raise ValueError('identity reused')
        db.execute('INSERT INTO receipts VALUES(?,?,?,1) ON CONFLICT(channel,id) DO UPDATE SET calls=calls+1', (channel, identity, payload))
        db.commit()
        return old is None


class Webhook(BaseHTTPRequestHandler):
    def do_POST(self):
        if (DIRECTORY / 'webhook-reject').exists():
            self.send_error(403)
            return
        if (DIRECTORY / 'webhook-down').exists():
            self.send_error(503)
            return
        size = int(self.headers.get('Content-Length', '0'))
        if size < 1 or size > 65536:
            self.send_error(413)
            return
        payload = self.rfile.read(size)
        timestamp = self.headers.get('X-PSIM-Timestamp', '')
        if not timestamp.isdecimal() or abs(time.time() - int(timestamp)) > 300:
            self.send_error(403)
            return
        identity_header = self.headers.get('Idempotency-Key', '')
        signed = timestamp.encode() + b'.' + identity_header.encode() + b'.' + payload
        expected = 'sha256=' + hmac.new(SECRET, signed, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, self.headers.get('X-PSIM-Signature', '')):
            self.send_error(403)
            return
        body = json.loads(payload)
        identity = body['notificationId']
        if self.headers.get('Idempotency-Key') != identity:
            self.send_error(400)
            return
        first = receipt('webhook', identity, payload.decode())
        if (DIRECTORY / 'webhook-lost-all').exists() or (first and (DIRECTORY / 'webhook-lost').exists()):
            self.connection.close()
            return
        while first and (DIRECTORY / 'webhook-hold').exists():
            time.sleep(0.05)
        result = json.dumps({'notificationId': identity, 'accepted': True, 'receiptId': 'webhook-' + identity}).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(result)))
        self.end_headers()
        try:
            self.wfile.write(result)
        except OSError:
            pass

    def log_message(self, *_):
        pass


def context():
    result = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    result.minimum_version = ssl.TLSVersion.TLSv1_3
    result.load_cert_chain(TLS / 'notification-server.pem', TLS / 'notification-server.key')
    return result


def smtp_session(connection):
    try:
        with context().wrap_socket(connection, server_side=True) as tls:
            stream = tls.makefile('rwb')
            if (DIRECTORY / 'smtp-down').exists():
                stream.write(b'421 unavailable\r\n'); stream.flush()
                return
            stream.write(b'220 fixture\r\n'); stream.flush()
            while True:
                line = stream.readline(16384)
                if not line:
                    return
                if line.startswith(b'EHLO'):
                    stream.write(b'250-fixture\r\n250 SIZE 65536\r\n')
                elif line.startswith((b'MAIL FROM:', b'RCPT TO:')):
                    stream.write(b'250 OK\r\n')
                elif line == b'DATA\r\n':
                    stream.write(b'354 continue\r\n'); stream.flush()
                    message = bytearray()
                    while (line := stream.readline(65536)) != b'.\r\n':
                        if not line or len(message) + len(line) > 131072:
                            return
                        message.extend(line)
                    identity = next(line.split(b'<', 1)[1].split(b'@', 1)[0].decode() for line in message.split(b'\r\n') if line.startswith(b'Message-ID:'))
                    parsed = BytesParser(policy=policy.default).parsebytes(bytes(message))
                    content = parsed.get_payload(decode=True).decode('utf-8')
                    if parsed.get_content_type() != 'text/plain' or not content.strip():
                        stream.write(b'550 identity mismatch\r\n'); stream.flush()
                        return
                    receipt('smtp', identity, message.decode())
                    if (DIRECTORY / 'smtp-lost').exists():
                        return
                    stream.write(b'250 accepted\r\n')
                else:
                    stream.write(b'500 invalid\r\n')
                stream.flush()
    except (OSError, ssl.SSLError):
        pass


def smtp():
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(('0.0.0.0', 8465))
    listener.listen(32)
    while True:
        connection, _ = listener.accept()
        threading.Thread(target=smtp_session, args=(connection,), daemon=True).start()


threading.Thread(target=smtp, daemon=True).start()
server = ThreadingHTTPServer(('0.0.0.0', 8443), Webhook)
server.socket = context().wrap_socket(server.socket, server_side=True)
server.serve_forever()
