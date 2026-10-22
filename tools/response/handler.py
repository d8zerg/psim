"""HTTPS handler fixture with durable call identities and explicit delayed responses."""
import json
from pathlib import Path
import ssl
import sys
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

DIRECTORY = Path(sys.argv[1])


class Handler(BaseHTTPRequestHandler):
    def do_POST(self):
        body = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        with (DIRECTORY / "calls.jsonl").open("a") as output:
            output.write(json.dumps(body) + "\n")
        if self.path == "/hold":
            time.sleep(15)
        self.send_response(500 if self.path == "/fail" else 200)
        self.send_header("Content-Length", "0")
        self.end_headers()

    def log_message(self, *_args):
        pass


server = ThreadingHTTPServer(("0.0.0.0", 8443), Handler)
context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
context.load_cert_chain("/repo/.cache/event-flow/response-server.pem", "/repo/.cache/event-flow/response-server.key")
server.socket = context.wrap_socket(server.socket, server_side=True)
server.serve_forever()
