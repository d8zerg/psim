#!/usr/bin/env python3
"""Check relative links and heading anchors in Markdown files.

Anchors follow GitHub slug rules: lower case, punctuation removed, spaces -> '-'.
External links (http, https, mailto) are not checked.

Usage: check_links.py <file-or-dir>...
"""

import os
from pathlib import Path
import re
import sys
from urllib.parse import unquote

LINK = re.compile(r"\]\(([^)\s]+)\)")
HEADING = re.compile(r"^#{1,6} (.+)$", re.M)
CODE_BLOCK = re.compile(r"```.*?```", re.S)


def slug(heading):
    return re.sub(r"[^\w\- ]", "", heading.strip().lower()).replace(" ", "-")


def page_anchors(text):
    text = CODE_BLOCK.sub("", text)
    result = set()
    for heading in HEADING.findall(text):
        base = slug(heading)
        anchor, number = base, 0
        while anchor in result:
            number += 1
            anchor = f"{base}-{number}"
        result.add(anchor)
    result.update(re.findall(r'<(?:a|span)\b[^>]*\b(?:id|name)=[\"\']([^\"\']+)[\"\']', text))
    return result


def markdown_files(paths):
    for p in paths:
        if os.path.isdir(p):
            for root, _, files in os.walk(p):
                yield from (os.path.join(root, f) for f in files if f.endswith(".md"))
        else:
            yield p


def main(argv):
    files = [os.path.normpath(f) for f in markdown_files(argv[1:])]
    anchors, texts = {}, {}
    for f in files:
        text = CODE_BLOCK.sub("", Path(f).read_text(encoding="utf-8"))
        texts[f] = text
        anchors[f] = page_anchors(text)
    bad = 0
    for f, text in texts.items():
        for link in LINK.findall(text):
            if link.startswith(("http://", "https://", "mailto:")):
                continue
            path, _, anchor = link.partition("#")
            path, anchor = unquote(path), unquote(anchor)
            target = os.path.normpath(os.path.join(os.path.dirname(f), path)) if path else f
            if not os.path.exists(target):
                print(f"{f}: missing target {link}")
                bad += 1
            elif anchor and target.endswith(".md"):
                if target not in anchors:
                    text_t = CODE_BLOCK.sub("", Path(target).read_text(encoding="utf-8"))
                    anchors[target] = page_anchors(text_t)
                if anchor not in anchors[target]:
                    print(f"{f}: missing anchor {link}")
                    bad += 1
    print(f"links: {len(files)} files, {bad} problems")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
