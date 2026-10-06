#!/usr/bin/env python3
"""Apply an OpenAPI Overlay 1.0 document to an OpenAPI description.

Supports the JSONPath subset used in contracts/openapi/overlay.yaml:
  $            root
  .name        child by name
  .*           every child of an object
  ['key']      child by quoted key (for keys with '/', '{', ':' ...)

Semantics follow the Overlay specification: `update` merges objects recursively and
appends to arrays; `remove: true` deletes the target. A target that matches nothing is an
error, so the overlay cannot silently drift from the generated description.

After the overlay the description is finalized: every operation gets a summary taken from
the first sentence of its description, and component schemas nobody references are removed
(the generator emits them for google.api.HttpBody responses replaced by the overlay).

Usage: openapi_overlay.py <openapi.yaml> <overlay.yaml> <output.yaml>
"""

import re
import sys

import yaml

TOKEN = re.compile(r"\.\*|\.([A-Za-z_][A-Za-z0-9_\-]*)|\['((?:[^'\\]|\\.)*)'\]")


def parse_path(expr):
    if not expr.startswith("$"):
        raise ValueError(f"target must start with '$': {expr}")
    tokens, pos = [], 1
    while pos < len(expr):
        m = TOKEN.match(expr, pos)
        if not m:
            raise ValueError(f"unsupported JSONPath at {expr[pos:]!r} in {expr}")
        if m.group(0) == ".*":
            tokens.append(("*", None))
        else:
            tokens.append(("key", m.group(1) if m.group(1) is not None else m.group(2)))
        pos = m.end()
    return tokens


def select(doc, tokens):
    """Return (parent, key) pairs for every node matched by tokens."""
    nodes = [(None, None, doc)]
    for kind, key in tokens:
        nxt = []
        for _, _, node in nodes:
            if not isinstance(node, dict):
                continue
            if kind == "*":
                nxt.extend((node, k, v) for k, v in node.items())
            elif key in node:
                nxt.append((node, key, node[key]))
        nodes = nxt
    return [(parent, key) for parent, key, _ in nodes]


def merge(target, update):
    if isinstance(target, dict) and isinstance(update, dict):
        for k, v in update.items():
            target[k] = merge(target[k], v) if k in target else v
        return target
    if isinstance(target, list) and isinstance(update, list):
        return target + update
    return update


def apply(doc, overlay):
    for i, action in enumerate(overlay.get("actions", [])):
        target = action["target"]
        tokens = parse_path(target)
        if not tokens:
            if action.get("remove"):
                raise ValueError("cannot remove the root")
            merge(doc, action["update"])
            continue
        matches = select(doc, tokens)
        if not matches:
            raise ValueError(f"action {i}: target matched nothing: {target}")
        for parent, key in matches:
            if action.get("remove"):
                del parent[key]
            else:
                parent[key] = merge(parent[key], action["update"])
    return doc


HTTP_METHODS = ("get", "put", "post", "delete", "patch")


def add_summaries(doc):
    for item in doc.get("paths", {}).values():
        for method in HTTP_METHODS:
            op = item.get(method)
            if op and "summary" not in op and op.get("description"):
                op["summary"] = op["description"].split(". ")[0].rstrip(".")


def refs(node, found):
    if isinstance(node, dict):
        for k, v in node.items():
            if k == "$ref" and isinstance(v, str) and v.startswith("#/components/schemas/"):
                found.add(v.rsplit("/", 1)[1])
            else:
                refs(v, found)
    elif isinstance(node, list):
        for v in node:
            refs(v, found)


def prune_schemas(doc):
    schemas = doc.get("components", {}).get("schemas", {})
    reachable = set()
    refs({k: v for k, v in doc.items() if k != "components"}, reachable)
    for name, comp in doc.get("components", {}).items():
        if name != "schemas":
            refs(comp, reachable)
    pending = list(reachable)
    while pending:
        found = set()
        refs(schemas.get(pending.pop(), {}), found)
        for name in found - reachable:
            reachable.add(name)
            pending.append(name)
    for name in [n for n in schemas if n not in reachable]:
        del schemas[name]


def main(argv):
    if len(argv) != 4:
        print(__doc__, file=sys.stderr)
        return 2
    with open(argv[1], encoding="utf-8") as f:
        doc = yaml.safe_load(f)
    with open(argv[2], encoding="utf-8") as f:
        overlay = yaml.safe_load(f)
    apply(doc, overlay)
    add_summaries(doc)
    prune_schemas(doc)
    with open(argv[3], "w", encoding="utf-8") as f:
        f.write("# Generated: contracts/openapi/generated/openapi.yaml + contracts/openapi/overlay.yaml.\n")
        f.write("# Do not edit; change the proto files or the overlay and run: task contracts:gen.\n")
        yaml.safe_dump(doc, f, sort_keys=False, allow_unicode=True, width=120)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
