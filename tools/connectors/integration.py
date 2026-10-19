#!/usr/bin/env python3
"""Run both reference executables through the registered gateway and reconcile their Kafka events."""
import argparse
import http.client
import json
import os
from pathlib import Path
import secrets
import signal
import ssl
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="debug")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / "build/debian13" / args.preset
    tls = root / ".cache/event-flow"
    report = root / "build/reference-connectors" / args.preset / "integration"
    report.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="psim-reference-integration-") as directory:
        directory = Path(directory)
        env = {**os.environ, "PSIM_CONNECTOR_ENDPOINT": "connector-gateway:8080",
               "PSIM_CONNECTOR_CA_FILE": str(tls / "ca.pem"),
               "PSIM_CONNECTOR_CERTIFICATE_FILE": str(tls / "client.pem"),
               "PSIM_CONNECTOR_PRIVATE_KEY_FILE": str(tls / "client.key"),
               "PSIM_CONNECTOR_BUFFER_DIRECTORY": str(directory / "simulator")}
        manifest = directory / "ids"
        result = subprocess.run([str(build / "connectors/simulator/psim-simulator"), "--count", "100",
                                 "--devices", "1000", "--scenario", "alarm", "--manifest", str(manifest)],
                                env=env, text=True, capture_output=True, timeout=150)
        (report / "simulator.log").write_text(result.stdout + result.stderr)
        assert result.returncode == 0, result.stderr
        simulator = json.loads(result.stdout.splitlines()[-1])
        assert simulator["acknowledged"] == 100 and simulator["rejected"] == 0
        token = secrets.token_hex(32)
        secret = directory / "token"
        secret.write_text(token)
        secret.chmod(0o400)
        env.update({"PSIM_CONNECTOR_BUFFER_DIRECTORY": str(directory / "webhook"),
                    "PSIM_WEBHOOK_TOKEN_FILE": str(secret), "PSIM_WEBHOOK_PORT": "0",
                    "PSIM_WEBHOOK_CERTIFICATE_FILE": str(tls / "server.pem"),
                    "PSIM_WEBHOOK_PRIVATE_KEY_FILE": str(tls / "server.key")})
        context = ssl.create_default_context(cafile=str(tls / "ca.pem"))
        context.minimum_version = ssl.TLSVersion.TLSv1_3
        with (report / "webhook.log").open("w+") as log:
            process = subprocess.Popen([str(build / "connectors/webhook/psim-webhook")], env=env,
                                       stdout=log, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 15
                while True:
                    log.seek(0)
                    line = log.readline()
                    if line:
                        port = json.loads(line)["port"]
                        break
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError("reference webhook did not start")
                    time.sleep(.05)
                ids = manifest.read_text().splitlines()
                for index in range(20):
                    payload = json.dumps({"source_ref": "reference-http", "raw_code": "ALARM",
                                          "occurred_at": "2020-01-01T00:00:00Z"})
                    receipt = None
                    for _ in range(2):
                        connection = http.client.HTTPSConnection("localhost", port, context=context, timeout=10)
                        connection.request("POST", "/v1/events", payload, {"Authorization": "Bearer " + token,
                                           "Content-Type": "application/json", "Idempotency-Key": f"source-{index}"})
                        response = connection.getresponse()
                        body = response.read()
                        assert response.status == 202, body
                        if receipt is not None:
                            assert receipt == body
                        receipt = body
                        connection.close()
                    ids.append(json.loads(receipt)["event_id"])
                process.send_signal(signal.SIGTERM)
                assert process.wait(timeout=15) == 0, "reference webhook did not drain"
                assert len(ids) == len(set(ids)) == 120
                manifest.write_text("\n".join(ids) + "\n")
                result = subprocess.run([str(build / "services/connector-gateway/psim-gateway-probe"), "verify", str(manifest)],
                                        text=True, capture_output=True, timeout=90)
                (report / "reconciliation.log").write_text(result.stdout + result.stderr)
                assert result.returncode == 0, result.stdout + result.stderr
                result = {"passed": True, "simulator_events": 100, "webhook_events": 20,
                          "webhook_requests": 40, "reconciled_kafka_events": len(ids)}
                (report / "report.json").write_text(json.dumps(result, indent=2) + "\n")
                print(json.dumps(result, indent=2))
            finally:
                if process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
