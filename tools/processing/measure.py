"""Measure offered load and visible normalization latency on a prepared processing environment."""
import json
import os
from pathlib import Path
import subprocess
import sys
import time
import uuid

sys.path.insert(0, str(Path(__file__).resolve().parent))
from check import Check
import envctl


def measure(check, count, mode="emit", normalizers=3):
    seed = str(uuid.uuid4())
    collector = subprocess.Popen(check.command("observe", count, seed), cwd=check.output.parents[2], env=check.env,
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        if collector.stdout.readline().strip() != "ready":
            raise RuntimeError("normalization observer failed to position at the current end")
        emitted = json.loads(check.run(check.command(mode, count, seed)).splitlines()[-1])
        finished = time.monotonic()
        output, errors = collector.communicate(timeout=120)
        if collector.returncode:
            raise RuntimeError(errors)
        normalized = json.loads(output.splitlines()[-1])
        check.wait(lambda: check.history(seed) == [count, count])
        history_lag = time.monotonic() - finished
        check.reconcile_history(seed, count)
        commits = {}
        for service in ("normalizer", "normalizer-2", "normalizer-3")[:normalizers]:
            status, body = check.probe.http("GET", "http://" + service + ":9100/metrics")
            if status == 200:
                commits[service] = [line for line in body.splitlines() if line.startswith("psim_normalizer_commit_seconds")]
        result = {"commit_histograms": commits, "emission": emitted, "normalization": normalized, "history_lag_upper_bound_seconds": history_lag}
        print(json.dumps(result), flush=True)
        return result
    finally:
        if collector.poll() is None:
            collector.terminate()
            collector.wait(timeout=10)


def main():
    check = Check(sys.argv[1], "release", int(sys.argv[2]))
    report = {"profile": {"offered_events_per_second": 50000, "sources": 100, "zones": 100, "normalizer_instances": 3, "normalizer_shards_per_instance": check.normalizer_shards, "history_instances": 2, "kafka_brokers": 1, "shared_host_clock": True, "warmup_events": min(check.count, 50000), "normalizer_batch_size": 16, "normalizer_commit_interval_ms": 5, "normalizer_transaction_gap_ms": int(os.environ.get("PSIM_NORMALIZER_TRANSACTION_GAP_MS", "5"))}}
    try:
        if check.owned:
            check.prepare()
        else:
            check.compose("up", "-d", "--force-recreate", *check.apps)
            envctl.PROBE_IMAGE = check.toolchain
            check.probe.start_probe()
            check.ready(check.apps)
            check.balanced()
        measure(check, min(check.count, 50000))
        report["three_instances"] = measure(check, check.count)
        current = report["three_instances"]
        report["performance_passed"] = current["normalization"]["p99_stage_ms"] <= 30 and current["history_lag_upper_bound_seconds"] <= 10 and current["emission"]["seconds"] <= check.count / 47500 and current["normalization"]["visible_seconds"] <= check.count / 47500
        if len(sys.argv) > 3 and sys.argv[3] == "scale":
            report["saturated_three"] = measure(check, check.count, "burst")
            check.compose("stop", "normalizer-2", "normalizer-3")
            check.balanced(1)
            measure(check, 1000, normalizers=1)
            report["saturated_one"] = measure(check, check.count, "burst", normalizers=1)
            report["scale_ratio"] = report["saturated_one"]["normalization"]["visible_seconds"] / report["saturated_three"]["normalization"]["visible_seconds"]
            report["scaling_passed"] = report["scale_ratio"] >= 2.4
            report["performance_passed"] = report["performance_passed"] and report["scaling_passed"]
        return 0 if report["performance_passed"] else 1
    finally:
        check.close()
        (check.output / "measurement.json").write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    raise SystemExit(main())
