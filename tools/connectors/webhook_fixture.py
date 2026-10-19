#!/usr/bin/env python3
"""Drive the reference HTTPS connector through its real authenticated event endpoint."""
import http.client
import json
import os
from pathlib import Path
import secrets
import signal
import ssl
import subprocess
import sys
import tempfile
import time


def main():
    preset = sys.argv[1] if len(sys.argv) > 1 else "debug"
    root = Path(__file__).resolve().parents[2]
    tls = root / ".cache/connector-conformance"
    context = ssl.create_default_context(cafile=str(tls / "ca.pem"))
    context.minimum_version = ssl.TLSVersion.TLSv1_3
    token = secrets.token_hex(32)
    with tempfile.TemporaryDirectory(prefix="psim-webhook-") as directory:
        directory = Path(directory)
        secret = directory / "token"
        secret.write_text(token)
        secret.chmod(0o400)
        env = {**os.environ, "PSIM_WEBHOOK_TOKEN_FILE": str(secret),
               "PSIM_WEBHOOK_CERTIFICATE_FILE": str(tls / "server.pem"),
               "PSIM_WEBHOOK_PRIVATE_KEY_FILE": str(tls / "server.key"), "PSIM_WEBHOOK_PORT": "0"}
        with (directory / "server.log").open("w+") as output:
            def start():
                output.seek(0)
                output.truncate()
                process = subprocess.Popen([str(root / f"build/debian13/{preset}/connectors/webhook/psim-webhook")],
                                           env=env, stdout=output, stderr=subprocess.STDOUT)
                deadline = time.monotonic() + 15
                while True:
                    output.seek(0)
                    line = output.readline()
                    if line:
                        return process, json.loads(line)["port"]
                    if process.poll() is not None or time.monotonic() > deadline:
                        process.kill()
                        process.wait()
                        raise RuntimeError("webhook startup failed: " + (directory / "server.log").read_text())
                    time.sleep(.05)

            process, port = start()
            try:
                def request(method, path, body=None, key=None, auth=True):
                    connection = http.client.HTTPSConnection("localhost", port, context=context, timeout=8)
                    headers = {"Content-Type": "application/json"}
                    if auth:
                        headers["Authorization"] = "Bearer " + token
                    if key:
                        headers["Idempotency-Key"] = key
                    connection.request(method, path, body, headers)
                    response = connection.getresponse()
                    data = response.read()
                    status = response.status
                    connection.close()
                    return status, data

                assert request("GET", "/healthz", auth=False)[0] == 401
                assert request("POST", "/v1/events", "{}", "invalid")[0] == 400
                receipts = []
                # Fixed input observation time is intentionally historical.
                occurred = "2020-01-01T00:00:00Z"
                for index in range(20):
                    body = json.dumps({"source_ref": "door-1", "raw_code": "REJECT" if index == 19 else "ALARM",
                                       "occurred_at": occurred})
                    status, receipt = request("POST", "/v1/events", body, f"fixture-{index}")
                    assert status == 202, (status, receipt)
                    assert request("POST", "/v1/events", body, f"fixture-{index}") == (status, receipt)
                    receipts.append(json.loads(receipt)["event_id"])
                    time.sleep(.05)
                    if index == 9:
                        process.kill()
                        process.wait(timeout=5)
                        process, port = start()
                        # The same upstream retry must retain its receipt across SIGKILL.
                        assert request("POST", "/v1/events", body, f"fixture-{index}") == (status, receipt)
                assert len(set(receipts)) == 20
                conflict = json.dumps({"source_ref": "other", "raw_code": "ALARM", "occurred_at": occurred})
                assert request("POST", "/v1/events", conflict, "fixture-0")[0] == 409
                deadline = time.monotonic() + int(os.environ["PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS"])
                while True:
                    status, data = request("GET", "/metrics")
                    assert status == 200
                    metrics = dict(line.split() for line in data.decode().splitlines())
                    if metrics.get("psim_connector_pending_events") == "0" and metrics.get("psim_connector_rejected_events_total") == "1":
                        break
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("webhook did not drain: " + (directory / "server.log").read_text())
                    time.sleep(.1)
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=10) == 0
                print(json.dumps({"https": True, "unique_receipts": 20, "duplicate_requests": 20,
                                  "conflict": True, "sigkill_retry": True, "unsupported_capabilities": ["commands.v1", "snapshot.v1"]}))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
