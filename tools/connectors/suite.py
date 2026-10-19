#!/usr/bin/env python3
"""Run isolated connector protocol fixtures concurrently, each with its own report and journal."""
import argparse
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--preset", default="debug")
    parser.add_argument("--outage-seconds", type=int, default=600)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    build = root / "build/debian13" / args.preset
    clients = [("example", [str(build / "libs/platform/connector/psim-connector-example")]),
               ("simulator", [str(build / "connectors/simulator/psim-simulator"), "--conformance"]),
               ("webhook", [sys.executable, "-I", str(root / "tools/connectors/webhook_fixture.py"), args.preset])]

    def run(fixture):
        name, command = fixture
        result = subprocess.run([sys.executable, "-I", str(root / "tools/connectors/conformance.py"),
                                 "--preset", args.preset, "--outage-seconds", str(args.outage_seconds),
                                 "--name", name, "--", *command], cwd=root, text=True, capture_output=True)
        return name, result

    with ThreadPoolExecutor(max_workers=3) as executor:
        results = list(executor.map(run, clients))
    for name, result in results:
        print(name + ":\n" + result.stdout + result.stderr)
    if any(result.returncode for _, result in results):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
