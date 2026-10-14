#!/usr/bin/env python3
"""Standalone simulation coverage (no CTest/CI integration), task sim:coverage."""

import json
import os
import pathlib
import shutil
import subprocess
import sys


def main():
    build = pathlib.Path(sys.argv[1]).resolve()
    root = pathlib.Path(__file__).resolve().parents[2]
    profiles = build / "profiles"
    shutil.rmtree(profiles, ignore_errors=True)
    profiles.mkdir()
    env = dict(os.environ, LLVM_PROFILE_FILE=str(profiles / "%p-%m.profraw"))
    checks, runner = build / "psim_sim_checks", build / "psim-sim"
    subprocess.run([str(checks)], env=env, check=True)
    subprocess.run([sys.executable, "-I", str(root / "tools/sim/test_cli.py"), str(runner)], env=env, check=True)
    subprocess.run([str(runner), "--runs", "200"], env=env, check=True)
    merged = build / "coverage.profdata"
    subprocess.run(["llvm-profdata", "merge", "-sparse", *map(str, profiles.glob("*.profraw")), "-o", str(merged)],
                   check=True)
    common = [str(checks), "-object", str(runner), "-instr-profile=" + str(merged)]
    result = subprocess.run(["llvm-cov", "export", "-summary-only", *common],
                            text=True, capture_output=True, check=True)
    report = json.loads(result.stdout)
    (build / "coverage.json").write_text(result.stdout)
    covered = count = 0
    for data in report["data"]:
        for entry in data["files"]:
            path = pathlib.Path(entry["filename"])
            if path.is_relative_to(root / "libs/sim") and "tests" not in path.parts:
                lines = entry["summary"]["lines"]
                covered += lines["covered"]
                count += lines["count"]
    percent = 100 * covered / count if count else 0
    print(f"libs/sim: {percent:.2f}% lines ({covered}/{count}), threshold 75%")
    subprocess.run(["llvm-cov", "show", *common, "-format=html", "-output-dir=" + str(build / "coverage-html"),
                    "-ignore-filename-regex=(/tests/|/platform/|^/cache/|^/usr/)"], check=True,
                   stdout=subprocess.DEVNULL)
    return 0 if count and percent >= 75 else 1


if __name__ == "__main__":
    sys.exit(main())
