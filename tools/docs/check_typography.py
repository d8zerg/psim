#!/usr/bin/env python3
"""Check project typography rules in text sources.

Rules: plain "-" instead of em dash, en dash and minus sign; "->" instead of the arrow; no emoji.
Generated files listed in GENERATED are skipped.

Usage: check_typography.py <file-or-dir>...
"""

import os
import re
import sys

EXTENSIONS = (".md", ".yaml", ".yml", ".proto", ".py", ".sh", ".dsl", ".json", ".txt")
GENERATED = {
    "contracts/docs/proto-reference.md",
    "contracts/openapi/psim-api-v1.yaml",
    "contracts/asyncapi/psim-topics.yaml",
}
# Code points are written with chr() so that this file passes its own check.
FORBIDDEN = {
    chr(0x2014): 'em dash, use "-"',
    chr(0x2013): 'en dash, use "-"',
    chr(0x2212): 'minus sign, use "-"',
    chr(0x2192): 'arrow, use "->"',
}
EMOJI = re.compile("[" + chr(0x1F300) + "-" + chr(0x1FAFF) + chr(0x2600) + "-" + chr(0x27BF) + chr(0xFE0F) + "]")


def files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, names in os.walk(p):
                yield from (os.path.join(root, n) for n in names if n.endswith(EXTENSIONS))
        else:
            yield p


def main(argv):
    bad = 0
    checked = 0
    for path in files(argv[1:]):
        rel = os.path.relpath(path)
        if rel in GENERATED:
            continue
        checked += 1
        for no, line in enumerate(open(path, encoding="utf-8"), 1):
            for ch, why in FORBIDDEN.items():
                if ch in line:
                    print(f"{rel}:{no}: {why}")
                    bad += 1
            if EMOJI.search(line):
                print(f"{rel}:{no}: emoji")
                bad += 1
    print(f"typography: {checked} files, {bad} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
