#!/usr/bin/env python3
"""Validate paired documentation, source freshness and shared technical identifiers."""

import argparse
from collections import Counter
import copy
import hashlib
import os
from pathlib import Path
import re
import sys

import yaml

IGNORED = {".git", ".cache", ".task", "build", "node_modules", "third_party", "__pycache__"}
DEVELOPMENT = {"psim-mvp-development-plan.md", "progress.md", "psim-mvp-diagrams.drawio"}
FENCES = re.compile(r"^```([^\n]*)\n(.*?)^```", re.M | re.S)
HEADINGS = re.compile(r"^(#{1,6}) (.+)$", re.M)
IDENTIFIERS = re.compile(r"\b(?:ADR-\d{3}|SR-\d{2}|G-\d{2}|F-[A-Z]+-\d{2}|QS-[A-Z]\d+|(?:TS|FF|C|L|B)-\d{2}|[SX]\d+)\b")
CYRILLIC = re.compile(r"[\u0400-\u04ff]")
UNEXPECTED_SCRIPT = re.compile(r"[\u3400-\u4dbf\u4e00-\u9fff]")

ARTIFACT_FIELDS = {
    "quality": {"name", "criteria", "purpose"},
    "security": {"name", "title", "scenario", "residual", "text", "description"},
    "topics": {"title", "description"},
    "openapi": {"description", "summary"},
    "dashboard": {"title", "description", "displayName", "text", "legendFormat", "noValue", "label"},
    "alerts": {"summary", "description"},
}


def russian_path(relative):
    return "docs/ru/" + (relative[5:] if relative.startswith("docs/") else "repository/" + relative)


def artifact_strings(document, kind, pointer=""):
    result = {}
    if isinstance(document, dict):
        for key, value in document.items():
            path = pointer + "/" + str(key).replace("~", "~0").replace("/", "~1")
            dashboard_label = kind == "dashboard" and (
                key == "value" and document.get("id") == "displayName"
                or pointer.endswith("/renameByName")
                or key == "name" and re.fullmatch(r"/annotations/list/\d+", pointer))
            if isinstance(value, str) and value and (key in ARTIFACT_FIELDS[kind] or dashboard_label or kind == "openapi" and path == "/info/title"):
                result[path] = value
            else:
                result.update(artifact_strings(value, kind, path))
    elif isinstance(document, list):
        for index, value in enumerate(document):
            result.update(artifact_strings(value, kind, pointer + "/" + str(index)))
    return result


def catalog(root):
    with (Path(root) / "tools/docs/locales/artifacts.yaml").open(encoding="utf-8") as source:
        return yaml.safe_load(source)["artifacts"]


def localized_artifact(root, relative, document, language):
    if language == "en":
        return document
    definition = catalog(root)[relative]
    expected = artifact_strings(document, definition["kind"])
    if set(expected) != set(definition["strings"]):
        raise ValueError(f"{relative}: localized fields are incomplete")
    result = copy.deepcopy(document)
    for pointer, source in expected.items():
        entry = definition["strings"][pointer]
        if entry["source"] != source or not entry.get(language):
            raise ValueError(f"{relative}{pointer}: translation is missing or stale")
        if not re.search(r"[A-Za-z\u0400-\u04ff]", source) and entry[language] != source:
            raise ValueError(f"{relative}{pointer}: nonverbal placeholder changed")
        parts = [part.replace("~1", "/").replace("~0", "~") for part in pointer.split("/")[1:]]
        parent = result
        for part in parts[:-1]:
            parent = parent[int(part)] if isinstance(parent, list) else parent[part]
        if isinstance(parent, list):
            parent[int(parts[-1])] = entry[language]
        else:
            parent[parts[-1]] = entry[language]
    return result


def check_artifacts(root):
    failures = []
    if not (Path(root) / "tools/docs/locales/artifacts.yaml").is_file():
        return ["localized artifact catalog is missing"]
    definitions = catalog(root)
    expected = {"docs/quality/verification.yaml", "docs/security/threat-model.yaml",
                "contracts/topics/topics.yaml", "contracts/openapi/psim-api-v1.yaml", "deploy/observability/rules.json"}
    expected.update(path.relative_to(root).as_posix() for path in (Path(root) / "deploy/observability/dashboards").glob("*.json"))
    for missing in sorted(expected - set(definitions)):
        failures.append(f"artifact has no language pair: {missing}")
    for relative, definition in definitions.items():
        if definition.get("kind") not in ARTIFACT_FIELDS:
            failures.append(f"invalid localized artifact kind: {relative}")
            continue
        target = (Path(root) / relative).resolve()
        if not target.is_relative_to(Path(root).resolve()) or not target.is_file():
            failures.append(f"invalid localized artifact: {relative}")
            continue
        with target.open(encoding="utf-8") as source:
            document = yaml.safe_load(source)
        try:
            localized_artifact(root, relative, document, "ru")
        except (KeyError, ValueError) as error:
            failures.append(str(error))
        for pointer, source in artifact_strings(document, definition["kind"]).items():
            if CYRILLIC.search(source):
                failures.append(f"{relative}{pointer}: Russian text in default artifact")
            translation = definition.get("strings", {}).get(pointer, {}).get("ru", "")
            if UNEXPECTED_SCRIPT.search(source + translation):
                failures.append(f"{relative}{pointer}: unexpected language in EN/RU text")
            tokens = re.compile(r"`[^`\n]+`|\{\{[^{}]+\}\}|\$\{[^{}]+\}|\$[A-Za-z_][A-Za-z_0-9]*")
            if Counter(tokens.findall(source)) != Counter(tokens.findall(translation)):
                failures.append(f"{relative}{pointer}: translated technical identifiers differ")
    return failures


def digest(text):
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def headings(text):
    used = set()
    result = []
    for level, title in HEADINGS.findall(FENCES.sub("", text)):
        slug = re.sub(r"[^\w\- ]", "", title.strip().lower()).replace(" ", "-")
        anchor, number = slug, 0
        while anchor in used:
            number += 1
            anchor = f"{slug}-{number}"
        used.add(anchor)
        result.append((len(level), anchor))
    return result


def section_map(source, target):
    source_headings, target_headings = headings(source), headings(target)
    if [level for level, _ in source_headings] != [level for level, _ in target_headings]:
        raise ValueError("cannot map sections with different heading structures")
    return {left[1]: right[1] for left, right in zip(source_headings, target_headings, strict=True)}


def inventory(root):
    root = Path(root)
    result = set()
    for directory, children, names in os.walk(root):
        children[:] = sorted(name for name in children if name not in IGNORED)
        for name in names:
            path = Path(directory, name)
            relative = path.relative_to(root)
            if name in DEVELOPMENT or relative.parts[:2] == ("docs", "ru"):
                continue
            if (relative.parts[0] == "docs" and path.suffix in {".md", ".puml"}
                    or name == "README.md" or relative.as_posix() in {"CONTRIBUTING.md", "SECURITY.md", "contracts/docs/proto-reference.md"}):
                result.add(relative.as_posix())
    return result


def read(root):
    with (Path(root) / "docs/localization.yaml").open(encoding="utf-8") as source:
        return yaml.safe_load(source)


def prose(text):
    text = FENCES.sub("", text)
    text = re.sub(r"`+[^`]*`+", "", text)
    text = re.sub(r"\[Русский\]\([^)]*\)", "", text)
    text = re.sub(r"\]\([^)]*\)", "]", text)
    return text


def check(root, manifest=None, require_inventory=True):
    root = Path(root).resolve()
    manifest = read(root) if manifest is None else manifest
    failures = []
    if manifest.get("default_language") != "en" or manifest.get("languages") != ["en", "ru"]:
        failures.append("documentation languages must be en and ru, with en as the default")
    ids, paths = set(), set()
    english, russian = set(), set()
    for page in manifest.get("pages", []):
        identity = page.get("id", "")
        if not identity or identity in ids:
            failures.append(f"missing or duplicate page identifier: {identity}")
        ids.add(identity)
        contents = {}
        for lang in ("en", "ru"):
            relative = page.get(lang, "")
            target = (root / relative).resolve()
            if not relative or relative in paths or not target.is_relative_to(root):
                failures.append(f"{identity}: invalid or duplicate {lang} path: {relative}")
                continue
            paths.add(relative)
            (english if lang == "en" else russian).add(relative)
            if not target.is_file():
                failures.append(f"{identity}: missing {lang} counterpart: {relative}")
                continue
            contents[lang] = target.read_text(encoding="utf-8")
            if page.get(f"{lang}_sha256") != digest(contents[lang]):
                failures.append(f"{identity}: changed {lang} source requires a paired translation review")
        if contents.keys() != {"en", "ru"}:
            continue
        en, ru = contents["en"], contents["ru"]
        if UNEXPECTED_SCRIPT.search(prose(en) + prose(ru)):
            failures.append(f"{identity}: unexpected language in EN/RU prose")
        if not en.strip() or not ru.strip():
            failures.append(f"{identity}: empty translation")
        if page["en"].endswith(".md"):
            if [level for level, _ in headings(en)] != [level for level, _ in headings(ru)]:
                failures.append(f"{identity}: section structures differ")
            en_blocks, ru_blocks = FENCES.findall(en), FENCES.findall(ru)
            if len(en_blocks) != len(ru_blocks):
                failures.append(f"{identity}: example counts differ")
            else:
                localized = set(page.get("localized_examples", []))
                for index, ((en_kind, en_body), (ru_kind, ru_body)) in enumerate(zip(en_blocks, ru_blocks)):
                    if en_kind != ru_kind:
                        failures.append(f"{identity}: example {index} languages differ")
                    elif en_kind == "plantuml" and (CYRILLIC.search(en_body) or UNEXPECTED_SCRIPT.search(en_body)):
                        failures.append(f"{identity}: untranslated English diagram {index}")
                    elif en_kind not in {"plantuml", "gherkin", "text"} and index not in localized and en_body != ru_body:
                        failures.append(f"{identity}: executable example {index} differs")
                if localized - set(range(len(en_blocks))):
                    failures.append(f"{identity}: unknown localized example")
            def code_identifiers(text):
                return {value for value in re.findall(r"`([^`\n]+)`", FENCES.sub("", text))
                        if not CYRILLIC.search(value) and not re.fullmatch(r"[0-9.,]+", value)}
            en_code, ru_code = code_identifiers(en), code_identifiers(ru)
            if en_code != ru_code:
                failures.append(f"{identity}: inline technical identifiers differ")
            if CYRILLIC.search(prose(en)):
                failures.append(f"{identity}: untranslated Russian prose in the English page")
            if not CYRILLIC.search(prose(ru)):
                failures.append(f"{identity}: Russian prose is missing")
        if Counter(IDENTIFIERS.findall(en)) != Counter(IDENTIFIERS.findall(ru)):
            failures.append(f"{identity}: stable requirement or scenario identifiers differ")
        if page["en"].endswith(".puml") and CYRILLIC.search(en):
            failures.append(f"{identity}: untranslated English diagram")
    if require_inventory:
        expected = inventory(root)
        for missing in sorted(expected - english):
            failures.append(f"page has no language pair: {missing}")
        for extra in sorted(english - expected):
            failures.append(f"unexpected English page: {extra}")
        mirrors = {p.relative_to(root).as_posix() for p in (root / "docs/ru").rglob("*") if p.suffix in {".md", ".puml"}}
        for orphan in sorted(mirrors - russian):
            failures.append(f"Russian page has no language pair: {orphan}")
        failures.extend(check_artifacts(root))
        failures.extend(check_assets(root, manifest))
    for failure in failures:
        print("FAIL", failure)
    print(f"documentation translations: {len(ids)} pairs, {len(failures)} problems")
    return bool(failures)


def check_assets(root, manifest):
    """Require paired, reviewed HTML instructions served by the monitoring agent."""
    expected = {path.relative_to(root).as_posix() for path in
                (root / "deploy/observability/runbooks").glob("*.html") if not path.stem.endswith("Ru")}
    actual = set()
    failures = []
    for pair in manifest.get("assets", []):
        actual.add(pair.get("en"))
        contents = {}
        for language in ("en", "ru"):
            relative = pair.get(language, "")
            path = (root / relative).resolve()
            if not path.is_relative_to(root) or not path.is_file():
                failures.append(f"missing or invalid {language} HTML counterpart: {relative}")
                continue
            text = path.read_text(encoding="utf-8")
            contents[language] = text
            if digest(text) != pair.get(language + "_sha256"):
                failures.append(f"HTML translation requires paired review: {relative}")
            if f'<html lang="{language}">' not in text:
                failures.append(f"wrong HTML language: {relative}")
            if "data-language-switch" not in text:
                failures.append(f"HTML language switch missing: {relative}")
            visible = re.sub(r"<(?:style|script|code|pre)\b[^>]*>.*?</(?:style|script|code|pre)>", "", text, flags=re.S)
            visible = re.sub(r"<[^>]*>", "", visible).replace("Русский", "")
            if language == "en" and CYRILLIC.search(visible):
                failures.append(f"Russian prose in default HTML: {relative}")
            if language == "ru" and not CYRILLIC.search(visible):
                failures.append(f"Russian prose missing in HTML: {relative}")
            if UNEXPECTED_SCRIPT.search(visible):
                failures.append(f"unexpected language in HTML: {relative}")
        if contents.keys() == {"en", "ru"}:
            en, ru = contents["en"], contents["ru"]
            if re.findall(r'<h[1-6] id="([^"]+)"', en) != re.findall(r'<h[1-6] id="([^"]+)"', ru):
                failures.append(f"HTML section identifiers differ: {pair['en']}")
            if re.findall(r"<(?:code|pre)\b[^>]*>.*?</(?:code|pre)>", en, re.S) != re.findall(r"<(?:code|pre)\b[^>]*>.*?</(?:code|pre)>", ru, re.S):
                failures.append(f"HTML executable examples differ: {pair['en']}")
    for missing in sorted(expected - actual):
        failures.append(f"HTML page has no language pair: {missing}")
    for extra in sorted(actual - expected):
        failures.append(f"unexpected HTML page: {extra}")
    return failures


def review(root, references, require_inventory=True):
    """Record an explicitly requested paired review only after structural validation."""
    root = Path(root).resolve()
    manifest = read(root)
    pending = set(references)
    selected = []
    for pair in manifest.get("pages", []) + manifest.get("assets", []):
        matches = pending & {pair.get("id"), pair.get("en"), pair.get("ru")}
        if not matches:
            continue
        pending -= matches
        for language in ("en", "ru"):
            target = (root / pair[language]).resolve()
            if not target.is_relative_to(root) or not target.is_file():
                print("FAIL missing or invalid translation:", pair[language])
                return 1
            pair[language + "_sha256"] = digest(target.read_text(encoding="utf-8"))
        selected.append(pair["en"])
    if pending or not selected:
        print("FAIL unknown or empty review selection:", ", ".join(sorted(pending)))
        return 1
    if check(root, manifest, require_inventory=require_inventory):
        return 1
    (root / "docs/localization.yaml").write_text(
        yaml.safe_dump(manifest, allow_unicode=True, sort_keys=False, width=120), encoding="utf-8")
    print("Recorded paired review:", ", ".join(selected))
    return 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root")
    parser.add_argument("--review", nargs="+", metavar="PAGE")
    args = parser.parse_args()
    sys.exit(review(args.root, args.review) if args.review else check(args.root))
