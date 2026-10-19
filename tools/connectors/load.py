#!/usr/bin/env python3
"""Measure durable simulator generation against a TLS protocol receiver and reconcile every ordinal."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="release")
    parser.add_argument("--count", type=int, default=500000)
    parser.add_argument("--minimum-rate", type=float, default=75000)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / "build/debian13" / args.preset
    tls = root / ".cache/connector-conformance"
    report = root / "build/reference-connectors" / args.preset / "load"
    report.mkdir(parents=True, exist_ok=True)
    for name in ("ready.json", "report.json"):
        (report / name).unlink(missing_ok=True)
    with tempfile.TemporaryDirectory(prefix="psim-simulator-load-") as buffer, \
            (report / "peer.log").open("w") as peer_log, (report / "simulator.log").open("w") as client_log:
        peer = subprocess.Popen([str(build / "tools/connector-conformance/psim-connector-load-peer"),
                                 str(tls), str(report), str(args.count)], stdout=peer_log, stderr=subprocess.STDOUT)
        client = None
        try:
            deadline = time.monotonic() + 15
            while not (report / "ready.json").exists():
                if peer.poll() is not None or time.monotonic() >= deadline:
                    raise RuntimeError("load receiver did not start")
                time.sleep(.05)
            endpoint = json.loads((report / "ready.json").read_text())["endpoint"]
            env = {**os.environ, "PSIM_CONNECTOR_ENDPOINT": endpoint,
                   "PSIM_CONNECTOR_CA_FILE": str(tls / "ca.pem"),
                   "PSIM_CONNECTOR_CERTIFICATE_FILE": str(tls / "client.pem"),
                   "PSIM_CONNECTOR_PRIVATE_KEY_FILE": str(tls / "client.key"),
                   "PSIM_CONNECTOR_BUFFER_DIRECTORY": buffer}
            client = subprocess.Popen([str(build / "connectors/simulator/psim-simulator"),
                                       "--count", str(args.count), "--devices", "10000", "--batch", "1000", "--run-id", "load-check"],
                                      env=env, stdout=client_log, stderr=subprocess.STDOUT)
            assert client.wait(timeout=180) == 0, (report / "simulator.log").read_text()
            assert peer.wait(timeout=10) == 0, (report / "peer.log").read_text()
            result = json.loads((report / "simulator.log").read_text().splitlines()[-1])
            receiver = json.loads((report / "report.json").read_text())
            result["receiver"] = receiver
            result["minimum_generation_rate"] = args.minimum_rate
            result["passed"] = (receiver["passed"] and result["acknowledged"] == args.count
                                and result["pending"] == 0 and result["rejected"] == 0
                                and result["generation_events_per_second"] >= args.minimum_rate)
            (report / "result.json").write_text(json.dumps(result, indent=2) + "\n")
            print(json.dumps(result, indent=2))
            assert result["passed"], "simulator throughput or reconciliation failed"
        finally:
            for process in (client, peer):
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait()


if __name__ == "__main__":
    main()
