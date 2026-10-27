#!/usr/bin/env python3
"""Render the Russian Protobuf reference from reviewed, shared contract descriptions."""

import html
import os
from pathlib import Path
import re
import sys

import yaml

SOURCE = "contracts/docs/proto-reference.md"
TARGET = "docs/ru/repository/" + SOURCE


def key(text):
    text = html.unescape(text).replace("\u2013", "-").replace("\u2014", "-")
    return " ".join(text.split()).rstrip(".")


def phrases(root):
    catalog = yaml.safe_load((root / "tools/docs/locales/artifacts.yaml").read_text(encoding="utf-8"))
    entries = catalog["artifacts"]["contracts/openapi/psim-api-v1.yaml"]["strings"].values()
    result = {key(entry["source"]): entry["ru"].rstrip(".") for entry in entries}
    extra = yaml.safe_load((root / "tools/docs/locales/proto.yaml").read_text(encoding="utf-8"))
    result.update({key(source): target.rstrip(".") for source, target in extra.items()})
    return result


def render(source, translations):
    def translate(text, markup=False):
        normalized = key(text)
        if normalized not in translations:
            raise ValueError("Untranslated Protobuf description: " + text)
        value = translations[normalized]
        if text.rstrip().endswith("."):
            value += "."
        return value if markup else html.escape(value, quote=True)

    output = []
    scalar = False
    header = False
    for line in source.splitlines():
        stripped = line.strip()
        if stripped == "<!-- language-links -->":
            header = True
        if header:
            if stripped.startswith("[English]"):
                relative = os.path.relpath(SOURCE, Path(TARGET).parent)
                line = f"[English]({relative}) | [Русский](proto-reference.md)"
            output.append(line)
            if stripped == "<!-- /language-links -->":
                header = False
            continue
        heading = re.match(r"^(#+) (Protocol Documentation|Table of Contents|Scalar Value Types|Imported Message Types)$", line)
        if heading:
            if heading[2] == "Scalar Value Types":
                scalar = True
                output.append('<a name="scalar-value-types"></a>')
            elif heading[2] == "Imported Message Types":
                scalar = False
                output.append('<a name="imported-message-types"></a>')
            output.append(heading[1] + " " + translate(heading[2]))
        elif stripped.startswith("|<"):
            raise ValueError("Unexpected Protobuf table format")
        elif stripped.startswith("|"):
            cells = line.split("|")
            if cells[-2].strip() == "Description" or cells[1].strip() == ".proto Type":
                languages = {"C++", "Java", "Python", "Go", "C#", "PHP", "Ruby"}
                cells = [" " + translate(cell.strip()) + " " if cell.strip() and cell.strip() not in languages else cell for cell in cells]
            elif set(stripped) - set("|- :"):
                index = 2 if scalar else len(cells) - 2
                if cells[index].strip():
                    cells[index] = " " + translate(cells[index].strip()) + " "
                if scalar:
                    cells = [cell.replace("Bignum or Fixnum (as required)", translations["Bignum or Fixnum (as required)"]) for cell in cells]
            output.append("|".join(cells))
        elif not stripped or stripped.startswith(("#", "<a", "<!--", "- [")) or set(stripped) <= set("- "):
            output.append(line.replace("[Scalar Value Types]", "[" + translations["Scalar Value Types"] + "]"))
        else:
            output.append(translate(stripped, markup=stripped.startswith('<p align="right">')))
    return "\n".join(output) + "\n"


def main(root, update=False):
    root = Path(root).resolve()
    try:
        expected = render((root / SOURCE).read_text(encoding="utf-8"), phrases(root))
    except (ValueError, KeyError) as error:
        print("FAIL", error)
        return 1
    target = root / TARGET
    if update:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(expected, encoding="utf-8")
    elif not target.is_file() or target.read_text(encoding="utf-8") != expected:
        print("FAIL missing or stale Russian Protobuf reference")
        return 1
    print("localized Protobuf reference: 0 problems")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], update="--update" in sys.argv[2:]))
