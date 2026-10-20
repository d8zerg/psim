"""Run the rule fuzzer and retain its corpus, log and duration evidence."""

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("--seconds", type=int, default=600)
    args = parser.parse_args()
    if args.seconds < 600:
        parser.error("the rule fuzzing gate requires at least 600 seconds")
    binary = args.binary.resolve(strict=True)
    output = Path("build/rules/fuzz")
    corpus = output / "corpus"
    artifacts = output / "artifacts"
    corpus.mkdir(parents=True, exist_ok=True)
    artifacts.mkdir(parents=True, exist_ok=True)
    report = output / "report.json"
    evidence = {
        "passed": False,
        "gate_passed": False,
        "gate_minimum_seconds": 600,
        "required_seconds": args.seconds,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "corpus": str(corpus),
        "artifacts": str(artifacts),
        "log": str(output / "fuzzer.log"),
    }
    report.write_text(json.dumps(evidence, indent=2) + "\n")
    # libFuzzer's integer clock may stop just before the requested wall duration.
    command = [str(binary), f"-max_total_time={args.seconds + 1}", "-timeout=5",
               "-rss_limit_mb=4096", "-max_len=65536", f"-artifact_prefix={artifacts}/",
               str(corpus), "tools/rules/corpus"]
    started = time.monotonic()
    print(f"Rule fuzzing started: {args.seconds}s; log: {evidence['log']}", flush=True)
    with (output / "fuzzer.log").open("w") as log:
        try:
            result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=args.seconds + 120, check=False)
            code = result.returncode
        except subprocess.TimeoutExpired:
            code = 124
    elapsed = time.monotonic() - started
    evidence.update(exit_code=code, elapsed_seconds=round(elapsed, 3),
                    passed=code == 0 and elapsed >= args.seconds,
                    gate_passed=code == 0 and elapsed >= max(600, args.seconds))
    report.write_text(json.dumps(evidence, indent=2) + "\n")
    print(json.dumps(evidence, indent=2), flush=True)
    raise SystemExit(0 if evidence["passed"] else 1)


if __name__ == "__main__":
    main()
