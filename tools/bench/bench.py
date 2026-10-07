#!/usr/bin/env python3
"""Benchmark runs and the performance regression gate (B-05, ADR-034).

  run <build-dir> <out.json> [commit] [names]
                                        run every *_bench executable (or only the space-separated
                                        names) of a build with repetitions;
                                        an existing result of the same commit is merged (minimum)
  compare <base.json> <current.json>    fail when a benchmark is slower than the baseline by more
                                        than the threshold (default 5%)

Each benchmark is reduced to the minimum CPU time (real time for UseRealTime ones) over its repetitions: on a shared developer stand
the minimum is far less noisy than the mean, and a real regression raises the minimum too. Noise on
the stand is close to the threshold, so `task bench:compare` reruns the benchmarks of a failed
comparison and merges the runs: retries can only lower the minimum, so a real slowdown stays.
"""

import json
import os
import subprocess
import sys

REPETITIONS = 10
MIN_TIME = "0.2s"
THRESHOLD = 0.05


def executables(build_dir):
    found = []
    for dirpath, dirnames, filenames in os.walk(build_dir):
        dirnames[:] = sorted(d for d in dirnames if d != "CMakeFiles")
        for name in sorted(filenames):
            path = os.path.join(dirpath, name)
            if name.endswith("_bench") and os.access(path, os.X_OK):
                found.append(path)
    return found


def run(build_dir, out_path, commit, only=None):
    results = {}
    if os.path.exists(out_path):
        with open(out_path, encoding="utf-8") as f:
            previous = json.load(f)
        if previous.get("commit") == commit:
            results = previous["cpu_ns_min"]
    for exe in executables(build_dir):
        if only and os.path.basename(exe) not in only:
            continue
        report = subprocess.run(
            [exe, "--benchmark_format=json", f"--benchmark_repetitions={REPETITIONS}",
             f"--benchmark_min_time={MIN_TIME}"],
            check=True, capture_output=True, text=True).stdout
        for b in json.loads(report)["benchmarks"]:
            if b.get("run_type") != "iteration":
                continue
            key = f"{os.path.basename(exe)}/{b['run_name']}"
            # Benchmarks measured in real time (UseRealTime: work on other threads) are compared by
            # real time; the CPU time of the waiting main thread says nothing about them.
            measure = "real_time" if b["run_name"].endswith("/real_time") else "cpu_time"
            ns = b[measure] * {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}[b["time_unit"]]
            results[key] = min(results.get(key, ns), ns)
        print(f"ran {os.path.relpath(exe, build_dir)}")
    os.makedirs(os.path.dirname(os.path.abspath(out_path)), exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump({"commit": commit, "repetitions": REPETITIONS, "cpu_ns_min": results}, f, indent=2, sort_keys=True)
    print(f"bench: {len(results)} benchmarks -> {out_path}")
    return 0


def compare(base_path, current_path, threshold=THRESHOLD):
    with open(base_path, encoding="utf-8") as f:
        base = json.load(f)
    with open(current_path, encoding="utf-8") as f:
        current = json.load(f)
    old, new = base["cpu_ns_min"], current["cpu_ns_min"]
    regressions = []
    print(f"baseline {base.get('commit', '?')[:12]} -> current {current.get('commit', '?')[:12]}, "
          f"threshold +{threshold:.0%}")
    for name in sorted(set(old) | set(new)):
        if name not in new:
            print(f"  not run   {name}")
        elif name not in old:
            print(f"  new       {name}: {new[name]:.1f} ns")
        else:
            change = new[name] / old[name] - 1
            verdict = "REGRESSED" if change > threshold else "ok"
            print(f"  {verdict:9} {name}: {old[name]:.1f} -> {new[name]:.1f} ns ({change:+.1%})")
            if change > threshold:
                regressions.append(name)
    print(f"bench:compare: {len(new)} benchmarks, {len(regressions)} regressions")
    return 1 if regressions else 0


def main(argv):
    if len(argv) >= 4 and argv[1] == "run":
        only = set(argv[5].split()) if len(argv) > 5 else None
        return run(argv[2], argv[3], argv[4] if len(argv) > 4 else "unknown", only)
    if len(argv) == 4 and argv[1] == "compare":
        return compare(argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
