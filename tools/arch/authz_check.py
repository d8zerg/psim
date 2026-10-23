"""Require shared scope decisions and online verification at service boundaries (FF-22)."""
from pathlib import Path
import re
import sys


def check(root):
    problems = []
    for path in sorted((root / "services").rglob("*")):
        if path.suffix not in (".cpp", ".hpp") or "tests" in path.parts:
            continue
        source = path.read_text()
        for number, line in enumerate(source.splitlines(), 1):
            if re.search(r'\bscopes\s*\.\s*(contains|count|find|empty|size|lower_bound|upper_bound)\s*\(', line) or "'site:'" in line or "'zone:'" in line or re.search(r'\bscope\s*\.\s*(starts_with|compare)\s*\(', line):
                problems.append(f"{path.relative_to(root)}:{number}: evaluate scopes through platform::authz")
        if "JwksCache::create" in source and "stream::authentication_options(" not in source:
            problems.append(f"{path.relative_to(root)}: use shared authenticated introspection configuration")
    return problems


if __name__ == "__main__":
    failures = check(Path(sys.argv[1] if len(sys.argv) > 1 else "."))
    print("\n".join(failures) if failures else "arch:authz: shared scope decisions and online authentication verified")
    sys.exit(bool(failures))
