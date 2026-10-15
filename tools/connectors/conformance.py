#!/usr/bin/env python3
"""Run a connector executable against the standalone TLS conformance peer."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="debug")
    parser.add_argument("--outage-seconds", type=int, default=600)
    parser.add_argument("client", nargs=argparse.REMAINDER, help="connector executable and arguments")
    args = parser.parse_args()
    if not 1 <= args.outage_seconds <= 600:
        parser.error("outage duration must be between 1 and 600 seconds")
    build = ROOT / "build/debian13" / args.preset
    report = ROOT / "build/connector-conformance" / args.preset
    report.mkdir(parents=True, exist_ok=True)
    for name in ("ready.json", "report.json"):
        (report / name).unlink(missing_ok=True)
    tls = ROOT / ".cache/connector-conformance"
    command = args.client or [str(build / "libs/platform/connector/psim-connector-example")]
    if command[0] == "--":
        command = command[1:]
    if not command:
        parser.error("client executable required after --")
    # Each fixture run gets an exclusive directory; never reuse another connector's ledger.
    with tempfile.TemporaryDirectory(prefix="psim-connector-") as buffer, \
            (report / "peer.log").open("w") as peer_log, (report / "client.log").open("w") as client_log:
        peer = subprocess.Popen([str(build / "tools/connector-conformance/psim-connector-conformance"),
                                 str(tls), str(report), str(args.outage_seconds)], cwd=ROOT,
                                stdout=peer_log, stderr=subprocess.STDOUT)
        client = None
        try:
            deadline = time.monotonic() + 15
            ready = report / "ready.json"
            while not ready.exists():
                if peer.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("conformance peer did not become ready; see peer.log")
                time.sleep(.05)
            endpoint = json.loads(ready.read_text())["endpoint"]
            env = {**os.environ, "PSIM_CONNECTOR_ENDPOINT": endpoint,
                   "PSIM_CONNECTOR_CA_FILE": str(tls / "ca.pem"),
                   "PSIM_CONNECTOR_CERTIFICATE_FILE": str(tls / "client.pem"),
                   "PSIM_CONNECTOR_PRIVATE_KEY_FILE": str(tls / "client.key"),
                   "PSIM_CONNECTOR_BUFFER_DIRECTORY": buffer,
                   "PSIM_CONNECTOR_FLUSH_TIMEOUT_SECONDS": str(args.outage_seconds + 40)}
            client = subprocess.Popen(command, cwd=ROOT, env=env, stdout=client_log, stderr=subprocess.STDOUT)
            deadline = time.monotonic() + args.outage_seconds + 50
            while peer.poll() is None:
                if client.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("connector exited early or exceeded conformance budget; see logs")
                time.sleep(.1)
            assert peer.returncode == 0, (report / "report.json").read_text()
            assert client.wait(timeout=10) == 0, "connector failed; see client.log"
            result = json.loads((report / "report.json").read_text())
            assert result["passed"], result
            print(json.dumps(result, indent=2))
            return 0
        finally:
            for process in (client, peer):
                if process is not None and process.poll() is None:
                    process.terminate()
                    try:
                        process.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        process.kill()
                        process.wait(timeout=5)


if __name__ == "__main__":
    sys.exit(main())
