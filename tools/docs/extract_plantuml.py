#!/usr/bin/env python3
"""Extract inline and standalone PlantUML diagrams for reproducible rendering.

Usage: extract_plantuml.py <out-dir> <source-dir>
"""

from pathlib import Path
import re
import shutil
import sys

BLOCK = re.compile(r"^```plantuml\n(.*?)^```", re.S | re.M)
DEVELOPMENT_FILES = {"psim-mvp-development-plan.md", "progress.md", "psim-mvp-diagrams.drawio"}


def extract(output, source):
    output, source = Path(output), Path(source)
    if output.resolve() == source.resolve() or output.resolve() in source.resolve().parents:
        raise ValueError("output must not contain the sources")
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    count = 0
    for path in sorted(source.rglob("*")):
        if not path.is_file() or path.name in DEVELOPMENT_FILES or output in path.parents:
            continue
        if path.suffix not in {".md", ".puml"}:
            continue
        text = path.read_text(encoding="utf-8")
        if path.suffix == ".md" and "```mermaid" in text:
            raise ValueError(f"unsupported diagram format: {path}")
        blocks = [text] if path.suffix == ".puml" else BLOCK.findall(text)
        stem = "__".join(path.relative_to(source).with_suffix("").parts)
        for index, block in enumerate(blocks):
            if not block.strip().startswith(("@startuml", "@startmindmap")):
                raise ValueError(f"missing PlantUML start marker: {path}")
            if re.search(r"!include(?:url)?\s+https?://", block, re.I):
                raise ValueError(f"network include is not reproducible: {path}")
            (output / f"{stem}__{index}.puml").write_text(block, encoding="utf-8")
            count += 1
    if not count:
        raise ValueError("no PlantUML diagrams found")
    print(f"plantuml: {count} diagrams extracted to {output}")


if __name__ == "__main__":
    extract(*sys.argv[1:])
