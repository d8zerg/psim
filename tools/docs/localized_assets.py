#!/usr/bin/env python3
"""Generate and verify Russian dashboard assets without changing metric queries."""

import importlib.util
import json
from pathlib import Path
import sys

SPEC = importlib.util.spec_from_file_location("localization", Path(__file__).with_name("localization.py"))
I18N = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(I18N)


def dashboard(root, relative):
    source = json.loads((root / relative).read_text(encoding="utf-8"))
    result = I18N.localized_artifact(root, relative, source, "ru")
    result["uid"] = source["uid"] + "-ru"

    def links(node):
        if isinstance(node, dict):
            for key, value in node.items():
                if key == "url" and isinstance(value, str):
                    if value.startswith("/monitoring/d/psim-"):
                        path, separator, query = value.partition("?")
                        node[key] = path + "-ru" + separator + query
                    elif "operations/runbooks/" in value:
                        node[key] = value + "Ru"
                else:
                    links(value)
        elif isinstance(node, list):
            for child in node:
                links(child)

    links(result)
    result["links"][-1] = {"title": "EN", "url": "/monitoring/d/" + source["uid"],
                            "type": "link", "keepTime": True, "includeVars": True}
    return result


def main(root, update=False):
    root = Path(root).resolve()
    failures = []
    for source in sorted((root / "deploy/observability/dashboards").glob("*.json")):
        relative = source.relative_to(root).as_posix()
        target = root / "deploy/observability/ru/dashboards" / source.name
        expected = json.dumps(dashboard(root, relative), ensure_ascii=False, indent=2) + "\n"
        if update:
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(expected, encoding="utf-8")
        elif not target.is_file() or target.read_text(encoding="utf-8") != expected:
            failures.append(f"missing or stale Russian dashboard: {target.relative_to(root)}")
    for failure in failures:
        print("FAIL", failure)
    print(f"localized dashboards: {len(failures)} problems")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], update="--update" in sys.argv[2:]))
