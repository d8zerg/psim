#!/usr/bin/env python3
"""Apply repository typography to vendored line comments without changing proto declarations."""
from pathlib import Path
import sys

for path in Path(sys.argv[1]).rglob("*.proto"):
    source = path.read_text()
    normalized = "".join(
        line.replace("\u2013", "-").replace("\u2014", "-").replace("\u2192", "->")
        if line.lstrip().startswith("//") else line
        for line in source.splitlines(keepends=True)
    )
    if normalized != source:
        path.write_text(normalized)
