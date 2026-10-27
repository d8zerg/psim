#!/usr/bin/env python3
"""Reject Russian source comments while allowing localized strings and test data."""

from pathlib import Path
import os
import re
import sys
import tokenize

import yaml

CYRILLIC = re.compile(r"[\u0400-\u04ff]")
SKIP = {".git", ".cache", "build", "node_modules", "third_party", "__pycache__"}
C_LIKE = {".c", ".cc", ".cpp", ".h", ".hpp", ".ts", ".tsx", ".js", ".mjs", ".proto"}
HASH_LIKE = {".sh", ".yaml", ".yml", ".cmake", ".toml"}
OTHER = {".sql", ".html", ".css"}
TOKENS = re.compile(
    r'R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|`(?:\\.|[^`\\])*`'
    r'|(?P<comment>//[^\n]*|/\*.*?\*/)',
    re.S,
)


def comments(path):
    text = path.read_text(encoding="utf-8")
    if path.suffix == ".py":
        with path.open("rb") as source:
            for token in tokenize.tokenize(source.readline):
                if token.type == tokenize.COMMENT:
                    yield token.start[0], token.string
    elif path.suffix in C_LIKE:
        for match in TOKENS.finditer(text):
            if match.group("comment"):
                yield text.count("\n", 0, match.start()) + 1, match.group("comment")
    elif path.suffix in OTHER:
        if path.suffix == ".html":
            pattern = r"(?P<comment><!--.*?-->)"
        else:
            pattern = r'''"(?:""|\\.|[^"\\])*"|'(?:''|\\.|[^'\\])*'|(?P<comment>/\*.*?\*/''' + (r"|--[^\n]*" if path.suffix == ".sql" else "") + ")"
        for match in re.finditer(pattern, text, re.S):
            if match.group("comment"):
                yield text.count("\n", 0, match.start()) + 1, match.group("comment")
    elif path.suffix in HASH_LIKE or path.name in {"Dockerfile", "CMakeLists.txt"}:
        scalar_lines = set()
        if path.suffix in {".yaml", ".yml"}:
            visited = set()
            def scalars(node):
                if id(node) in visited:
                    return
                visited.add(id(node))
                if isinstance(node, yaml.ScalarNode) and node.style in {"|", ">"}:
                    scalar_lines.update(range(node.start_mark.line + 2, node.end_mark.line + 1))
                elif isinstance(node, yaml.MappingNode):
                    for key, value in node.value:
                        scalars(key)
                        scalars(value)
                elif isinstance(node, yaml.SequenceNode):
                    for value in node.value:
                        scalars(value)
            try:
                for document in yaml.compose_all(text):
                    scalars(document)
            except yaml.YAMLError:
                # Template inputs are not necessarily YAML until they are rendered.
                scalar_lines.clear()
        quote = None
        escaped = False
        for number, line in enumerate(text.splitlines(), 1):
            if number in scalar_lines:
                continue
            for index, char in enumerate(line):
                if escaped:
                    escaped = False
                elif char == "\\" and quote != "'":
                    escaped = True
                elif char == quote:
                    quote = None
                elif quote is None and char in "\"'":
                    quote = char
                elif quote is None and char == "#" and (index == 0 or line[index - 1].isspace()):
                    yield number, line[index:]
                    break
            escaped = False


def check(root):
    root = Path(root)
    failures = 0
    for directory, children, filenames in os.walk(root):
        children[:] = sorted(name for name in children if name not in SKIP)
        for filename in sorted(filenames):
            path = Path(directory) / filename
            if path.suffix not in C_LIKE | HASH_LIKE | OTHER | {".py"} and path.name not in {"Dockerfile", "CMakeLists.txt"}:
                continue
            for number, comment in comments(path):
                if CYRILLIC.search(comment):
                    print(f"{path}:{number}: source comments must be English")
                    failures += 1
    print(f"source comments: {failures} problems")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(check(sys.argv[1]))
