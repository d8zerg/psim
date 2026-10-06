#!/usr/bin/env python3
"""Merge llvm-cov profiles of the coverage preset, write reports and enforce thresholds (FF-12).

Runs in the toolchain container after `ctest --preset coverage`. Thresholds and module groups are
read from coverage.yaml. Reports: <build-dir>/coverage-report.txt and <build-dir>/coverage-html/.

Usage: coverage.py <coverage-build-dir> <coverage.yaml> [modules]
  modules  space-separated module directories to gate (task ci passes the affected ones); empty: all
"""

import fnmatch
import glob
import json
import os
import subprocess
import sys

import yaml

# Tests, benchmarks, generated and vendored code, dependency and system headers are not measured.
IGNORE_REGEX = r"(^|/)(tests|benchmarks|build|third_party)/|^/cache/|^/usr/"


def run(cmd, **kwargs):
    return subprocess.run(cmd, check=True, text=True, **kwargs)


def test_binaries(build_dir):
    out = run(["ctest", "--test-dir", build_dir, "--show-only=json-v1"], capture_output=True).stdout
    binaries = []
    for test in json.loads(out).get("tests", []):
        command = test.get("command") or []
        if command and command[0] not in binaries:
            binaries.append(command[0])
    return binaries


def module_of(rel, groups):
    """Return (group, module directory) of a source path, or (None, None) when ungated."""
    parts = rel.split("/")
    for group in groups:
        for pattern in group["modules"]:
            depth = len(pattern.split("/"))
            if len(parts) > depth and fnmatch.fnmatchcase("/".join(parts[:depth]), pattern):
                return group, "/".join(parts[:depth])
    return None, None


def gated_modules(groups, root):
    """Module directories with C++ sources outside tests/: each must appear in the report."""
    found = {}
    for group in groups:
        for pattern in group["modules"]:
            for module in glob.glob(os.path.join(root, pattern)):
                for dirpath, dirnames, filenames in os.walk(module):
                    dirnames[:] = [d for d in dirnames if d not in ("tests", "benchmarks")]
                    if any(n.endswith((".cpp", ".cc")) for n in filenames):
                        found.setdefault(os.path.relpath(module, root), group)
                        break
    return found


def evaluate(files, groups, root, only=None):
    """Aggregate per-file summaries by module; return (rows, failures). only: modules to gate."""
    modules = {}
    for name, group in gated_modules(groups, root).items():
        if only and name not in only:
            continue
        modules[name] = {"group": group, "lines": [0, 0], "branches": [0, 0]}
    for f in files:
        rel = os.path.relpath(f["filename"], root)
        group, module = module_of(rel, groups)
        if only and module not in only:
            group, module = None, None
        key = module or "(ungated)"
        m = modules.setdefault(key, {"group": group, "lines": [0, 0], "branches": [0, 0]})
        for kind in ("lines", "branches"):
            m[kind][0] += f["summary"][kind]["covered"]
            m[kind][1] += f["summary"][kind]["count"]

    rows, failures = [], []
    for name in sorted(modules):
        m = modules[name]
        group = m["group"]
        row = [name, group["name"] if group else "-"]
        if group and not m["lines"][1]:
            failures.append(f"{name}: not measured - no test executable covers its sources")
        for kind in ("lines", "branches"):
            covered, count = m[kind]
            pct = 100.0 * covered / count if count else 100.0
            limit = group.get(kind) if group else None
            row.append(f"{pct:6.2f}% ({covered}/{count})" + (f" >= {limit}%" if limit is not None else ""))
            if limit is not None and count and pct < limit:
                failures.append(f"{name}: {kind} {pct:.2f}% < {limit}%")
        rows.append(row)
    return rows, failures


def main(argv):
    if len(argv) not in (3, 4):
        print(__doc__, file=sys.stderr)
        return 2
    build_dir, config_path = argv[1], argv[2]
    only = set(argv[3].split()) if len(argv) == 4 else set()
    root = os.getcwd()
    groups = yaml.safe_load(open(config_path, encoding="utf-8"))["groups"]

    profiles = glob.glob(os.path.join(build_dir, "profiles", "*.profraw"))
    if not profiles:
        print(f"coverage: no profiles in {build_dir}/profiles; run ctest --preset coverage first", file=sys.stderr)
        return 1
    profdata = os.path.join(build_dir, "coverage.profdata")
    run(["llvm-profdata", "merge", "-sparse", "-o", profdata, *profiles])

    binaries = test_binaries(build_dir)
    objects = [binaries[0]] + [arg for b in binaries[1:] for arg in ("-object", b)]
    common = [f"-instr-profile={profdata}", f"-ignore-filename-regex={IGNORE_REGEX}", *objects]

    export = run(["llvm-cov", "export", "-summary-only", *common], capture_output=True).stdout
    files = json.loads(export)["data"][0]["files"]
    with open(os.path.join(build_dir, "coverage-report.txt"), "w", encoding="utf-8") as report:
        run(["llvm-cov", "report", "-show-branch-summary", *common], stdout=report)
    run(["llvm-cov", "show", "-format=html", "-show-branches=count",
         f"-output-dir={os.path.join(build_dir, 'coverage-html')}", *common], stdout=subprocess.DEVNULL)

    rows, failures = evaluate(files, groups, root, only)
    print(f"{'module':40} {'group':15} {'lines':32} branches")
    for row in rows:
        print(f"{row[0]:40} {row[1]:15} {row[2]:32} {row[3]}")
    print(f"coverage: {len(files)} files, report {build_dir}/coverage-report.txt, html {build_dir}/coverage-html/")
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
