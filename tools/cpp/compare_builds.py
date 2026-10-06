#!/usr/bin/env python3
"""Compare build artifacts of two build directories byte for byte (reproducible builds, FF-13).

Artifacts: executables and static libraries of the project (not the CMake and ninja metadata).

Usage: compare_builds.py <build-dir-a> <build-dir-b>
"""

import hashlib
import os
import stat
import sys

SKIP_DIRS = {"CMakeFiles", "conan", "Testing", ".cmake"}


def artifacts(root):
    result = {}
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
        for name in filenames:
            path = os.path.join(dirpath, name)
            mode = os.stat(path).st_mode
            is_exe = stat.S_ISREG(mode) and mode & stat.S_IXUSR and not name.endswith((".sh", ".py"))
            if name.endswith(".a") or is_exe:
                with open(path, "rb") as f:
                    result[os.path.relpath(path, root)] = hashlib.sha256(f.read()).hexdigest()
    return result


def main(argv):
    if len(argv) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    a, b = artifacts(argv[1]), artifacts(argv[2])
    problems = 0
    for name in sorted(set(a) | set(b)):
        if a.get(name) != b.get(name):
            print(f"DIFFERS: {name}")
            problems += 1
    print(f"reproducible: {len(a)} artifacts compared, {problems} differ")
    return 1 if problems or not a else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
