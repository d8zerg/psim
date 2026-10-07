#!/usr/bin/env python3
"""Scope of the push pipeline: which stages and targets `task ci` checks (ADR-034).

  compute <root> <build-dir> <out.json> [full]
      changes = working tree (committed and uncommitted, untracked included) against the merge base
      with origin/master; C++ scope = targets of changed modules plus every target depending on them
      (CMake File API code model of <build-dir>); `full` or a change matching scope.yaml "full"
      selects everything
  get <scope.json> <key>
      print one value for Taskfile variables: lists space-separated, booleans true/false

Keys: full, cpp, contracts, docs, deps, release, bench, env, web (booleans); labels (ctest label regex, empty
for all), tidy (translation units, empty for all), modules (coverage modules, empty for all),
benches (benchmark executables, empty for all); base, changed, targets for the report.
"""

import fnmatch
import json
import os
import subprocess
import sys

import yaml

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "arch"))
import arch_check  # noqa: E402

CONFIG = os.path.join(os.path.dirname(os.path.abspath(__file__)), "scope.yaml")


def git(root, *args):
    result = subprocess.run(["git", "-C", root, *args], capture_output=True, text=True)
    return result.stdout.strip() if result.returncode == 0 else None


def changed_files(root, base):
    tracked = git(root, "diff", "--name-only", base) or ""
    untracked = git(root, "ls-files", "--others", "--exclude-standard") or ""
    return sorted({f for f in (tracked + "\n" + untracked).splitlines() if f})


def matches(path, patterns):
    return any(fnmatch.fnmatchcase(path, p) for p in patterns)


def affected_targets(targets, changed, rules):
    """Targets of changed modules and, transitively, every target that depends on one of them."""
    modules = {m for m in (arch_check.classify(f, rules)[1] for f in changed) if m}
    seeds = {name for name, t in targets.items()
             if any(t["dir"] == m or t["dir"].startswith(m + "/") for m in modules)}
    dependents = {}
    for name, t in targets.items():
        for dep in t["deps"]:
            dependents.setdefault(dep, set()).add(name)
    result, stack = set(), list(seeds)
    while stack:
        name = stack.pop()
        if name not in result:
            result.add(name)
            stack.extend(dependents.get(name, ()))
    return result


def compute(root, build_dir, force_full):
    config = yaml.safe_load(open(CONFIG, encoding="utf-8"))
    rules = arch_check.load_rules()
    _, targets = arch_check.load_codemodel(build_dir)
    base = git(root, "merge-base", "HEAD", "origin/master")
    changed = changed_files(root, base) if base else []
    reason = "requested" if force_full else "no origin/master" if not base else None
    if not reason:
        hit = next((f for f in changed if matches(f, config["full"])), None)
        reason = f"{hit} changed" if hit else None
    full = reason is not None

    stages = {name: full or any(matches(f, patterns) for f in changed) for name, patterns in config["stages"].items()}
    affected = set(targets) if full else affected_targets(targets, changed, rules)
    modules = sorted({arch_check.classify(targets[t]["dir"], rules)[1] for t in affected} - {None})
    tidy = sorted(s for t in affected for s in targets[t]["sources"] if s.endswith((".cpp", ".cc")))
    benches = sorted(t for t in affected if t.endswith("_bench"))
    # Services run in the environment (FF-06): a change of the runtime or of a service needs it.
    runs_in_env = any(t == "psim_platform_runtime" or t.startswith("psim-") or targets[t]["dir"].startswith("services/")
                      for t in affected)
    stages["env"] = stages["env"] or runs_in_env
    return {
        "base": base or "", "full": full, "reason": reason or "changes", "changed": changed,
        "targets": sorted(affected), "cpp": bool(affected), **stages,
        "release": stages["release"] or bool(affected),
        "bench": stages["bench"] or bool(benches),
        # Empty values mean "everything" for the tasks that consume them.
        "labels": "" if full else "^(" + "|".join(sorted(affected)) + ")$" if affected else "",
        "tidy": [] if full else tidy,
        "modules": [] if full else modules,
        "benches": [] if full else benches,
    }


def main(argv):
    if len(argv) in (5, 6) and argv[1] == "compute":
        scope = compute(os.path.abspath(argv[2]), argv[3], len(argv) == 6 and argv[5] == "true")
        os.makedirs(os.path.dirname(os.path.abspath(argv[4])), exist_ok=True)
        with open(argv[4], "w", encoding="utf-8") as f:
            json.dump(scope, f, indent=2)
        what = "full scope" if scope["full"] else f"{len(scope['targets'])} targets"
        stages = ", ".join(k for k in ("contracts", "docs", "deps", "release", "bench", "env", "web") if scope[k]) or "none"
        print(f"ci scope: {what} ({scope['reason']}); {len(scope['changed'])} changed files; optional stages: {stages}")
        return 0
    if len(argv) == 4 and argv[1] == "get":
        value = json.load(open(argv[2], encoding="utf-8"))[argv[3]]
        print(" ".join(value) if isinstance(value, list) else str(value).lower() if isinstance(value, bool) else value)
        return 0
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
