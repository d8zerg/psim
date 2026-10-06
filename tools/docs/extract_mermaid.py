#!/usr/bin/env python3
"""Extract Mermaid code blocks from Markdown files into separate .mmd files for rendering checks.

Each block is written to <out-dir>/<path_with_underscores>_<n>.mmd.

Usage: extract_mermaid.py <out-dir> <file-or-dir>...
"""

import os
import re
import shutil
import sys

BLOCK = re.compile(r"```mermaid\n(.*?)```", re.S)


def main(argv):
    out = argv[1]
    shutil.rmtree(out, ignore_errors=True)
    os.makedirs(out)
    count = 0
    for p in argv[2:]:
        walk = os.walk(p) if os.path.isdir(p) else [(os.path.dirname(p), [], [os.path.basename(p)])]
        for root, _, names in walk:
            for name in sorted(names):
                if not name.endswith(".md"):
                    continue
                path = os.path.join(root, name)
                stem = os.path.relpath(path).replace(os.sep, "_")[:-3]
                for i, block in enumerate(BLOCK.findall(open(path, encoding="utf-8").read())):
                    with open(os.path.join(out, f"{stem}_{i}.mmd"), "w", encoding="utf-8") as f:
                        f.write(block)
                    count += 1
    print(f"mermaid: {count} diagrams extracted to {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
