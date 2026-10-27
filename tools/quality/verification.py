#!/usr/bin/env python3
"""Verification registry: render the matrix and verify coverage.

Commands:
  render <repo-root> <verification.yaml> <out.md>
      Generate the Markdown verification matrix: service and engineering quality goals,
      targets and methods, acceptance scenarios, method catalog and implementation coverage.
  verify <repo-root> <verification.yaml>
      Check that every goal and acceptance scenario has a verification method with an
      automated barrier or a manual procedure; SLO goals require an automated method.

Exit code 1 and a list of problems when verification fails.
"""

import os
import importlib.util
from pathlib import Path
import re
import sys

import yaml

KINDS = ["test", "load", "soak", "chaos", "benchmark", "fitness", "security", "manual"]
KIND_NAMES = {
    "test": "Наборы тестов", "load": "Нагрузка", "soak": "Длительная нагрузка", "chaos": "Хаос и отказы",
    "benchmark": "Бенчмарки", "fitness": "Fitness functions", "security": "Безопасность",
    "manual": "Ручные процедуры",
}
GATES = ["pr", "nightly", "release", "on-demand"]
BARRIERS = {"automated", "manual"}
ENVS = {"E-DEV", "E-CI", "E-LOCAL", "E-CLUSTER"}
ENGLISH = {
    "# Матрица верификации": "# Verification matrix",
    "Сгенерировано из [verification.yaml](verification.yaml) задачей `task docs:verification:gen`. Не редактировать вручную.":
        "Generated from [verification.yaml](verification.yaml) by `task docs:verification:gen`. Do not edit manually.",
    "Стратегия - [strategy.md](strategy.md). В скобках - барьер метода: `pr` блокирует слияние, `nightly` - следующий выпуск, `release` - выпуск; `on-demand` запускается вручную и не входит в CI.":
        "See [strategy.md](strategy.md). Parentheses identify the gate: `pr` blocks merging, `nightly` blocks the next release, and `release` blocks release; `on-demand` runs manually outside CI.",
    "## 1. Сводка": "## 1. Summary", "## 2. Цели SLO": "## 2. SLO goals",
    "## 3. Цели инженерного качества": "## 3. Engineering quality goals",
    "## 4. Сценарии приёмки": "## 4. Acceptance scenarios", "## 5. Каталог методов": "## 5. Method catalog",
    "| Вид |": "| Kind |", "Всего": "Total", "| ID | Цель | Порог | Методы |": "| ID | Goal | Target | Methods |",
    "| Сценарий | Методы |": "| Scenario | Methods |",
    "| ID | Метод | Критерий прохождения | Стенд | Барьер | Запуск |": "| ID | Method | Pass criterion | Environment | Gate | Command |",
    "Наборы тестов": "Test suites", "Длительная нагрузка": "Soak tests", "Нагрузка": "Load tests",
    "Хаос и отказы": "Chaos and failures", "Бенчмарки": "Benchmarks", "Безопасность": "Security",
    "Ручные процедуры": "Manual procedures", ", ручной": ", manual", "регламент": "procedure", " Назначение: ": " Purpose: ",
}


def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def requirements(root):
    return load(os.path.join(root, "docs/quality/requirements.yaml"))["requirements"]


def scenarios(root):
    with open(os.path.join(root, "docs/product/acceptance-spec.md"), encoding="utf-8") as source:
        text = source.read()
    return re.findall(r"^## (S\d+)\. ", text, re.M)


# ---------------------------------------------------------------- verify

def verify(root, reg):
    problems = []
    definitions = requirements(root)
    required = {}
    for definition in definitions:
        rid = definition["id"]
        if rid in required:
            problems.append(f"duplicate requirement {rid}")
        required[rid] = definition
        if not re.fullmatch(r"G-\d{2}", rid):
            problems.append(f"invalid requirement id {rid}")
        if definition.get("category") not in {"slo", "engineering"}:
            problems.append(f"{rid}: unknown requirement category")
        for field in ("name", "target"):
            for lang in ("en", "ru"):
                if not definition.get(field, {}).get(lang):
                    problems.append(f"{rid}: missing {field}.{lang}")
    methods = {}
    for m in reg["methods"]:
        mid = m["id"]
        if mid in methods:
            problems.append(f"duplicate method {mid}")
        methods[mid] = m
        if m["kind"] not in KINDS:
            problems.append(f"{mid}: unknown kind {m['kind']}")
        if m["gate"] not in GATES:
            problems.append(f"{mid}: unknown gate {m['gate']}")
        if m["barrier"] not in BARRIERS:
            problems.append(f"{mid}: unknown barrier {m['barrier']}")
        if (m["kind"] == "manual") != (m["barrier"] == "manual"):
            problems.append(f"{mid}: manual kind and manual barrier must go together")
        if m["env"] not in ENVS:
            problems.append(f"{mid}: unknown env {m['env']}")
        if m["barrier"] == "automated" and not re.match(r"^task [a-z0-9-]+(:[a-z0-9-]+)+$", m["task"]):
            problems.append(f"{mid}: automated method needs a Task command, got {m['task']!r}")
        if not m.get("criteria"):
            problems.append(f"{mid}: no pass criteria")
    used = set()
    seen_goals = set()
    for g in reg["goals"]:
        gid = g["id"]
        if gid in seen_goals:
            problems.append(f"duplicate goal {gid}")
        seen_goals.add(gid)
        if gid not in required:
            problems.append(f"{gid}: unknown requirement")
            continue
        if not g["methods"]:
            problems.append(f"{gid}: no methods")
        for mid in g["methods"]:
            if mid not in methods:
                problems.append(f"{gid}: unknown method {mid}")
            used.add(mid)
        if required[gid]["category"] == "slo" and not any(methods.get(mid, {}).get("barrier") == "automated" for mid in g["methods"]):
            problems.append(f"{gid}: SLO goal needs at least one automated method")
    for rid in required.keys() - seen_goals:
        problems.append(f"{rid}: requirement has no verification methods")
    mapped = {s["id"]: s for s in reg["scenarios"]}
    expected_scenarios = set(scenarios(root))
    if len(mapped) != len(reg["scenarios"]):
        problems.append("duplicate acceptance scenario")
    for sid in mapped.keys() - expected_scenarios:
        problems.append(f"{sid}: unknown acceptance scenario")
    for sid in expected_scenarios:
        if sid not in mapped:
            problems.append(f"scenario {sid} has no verification methods")
    for s in reg["scenarios"]:
        if not s["methods"]:
            problems.append(f"{s['id']}: no methods")
        for mid in s["methods"]:
            if mid not in methods:
                problems.append(f"{s['id']}: unknown method {mid}")
            used.add(mid)
    for mid, m in methods.items():
        if mid not in used and not m.get("purpose"):
            problems.append(f"{mid}: not linked to a goal or scenario and has no purpose")
    for p in problems:
        print("FAIL", p)
    print(f"verification: {len(methods)} methods, {len(reg['goals'])} goals, {len(reg['scenarios'])} scenarios, "
          f"{len(problems)} problems")
    return 1 if problems else 0


# ---------------------------------------------------------------- render

def render(root, reg, language="en"):
    if language == "ru":
        spec = importlib.util.spec_from_file_location("localization", Path(root) / "tools/docs/localization.py")
        locale = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(locale)
        reg = locale.localized_artifact(root, "docs/quality/verification.yaml", reg, language)
    definitions = {r["id"]: r for r in requirements(root)}
    methods = {m["id"]: m for m in reg["methods"]}
    out = []
    def w(line):
        if language == "en":
            for source, target in sorted(ENGLISH.items(), key=lambda pair: -len(pair[0])):
                line = line.replace(source, target)
        else:
            line = line.replace("](verification.yaml)", "](../../quality/verification.yaml)")
        out.append(line)

    def refs(ids):
        return ", ".join(f"{i} ({methods[i]['gate']})" for i in ids)

    w("# Матрица верификации")
    w("")
    w("Сгенерировано из [verification.yaml](verification.yaml) задачей `task docs:verification:gen`. Не редактировать вручную.")
    w("Стратегия - [strategy.md](strategy.md). В скобках - барьер метода: `pr` блокирует слияние, `nightly` - следующий выпуск, `release` - выпуск; `on-demand` запускается вручную и не входит в CI.")
    w("")
    w("## 1. Сводка")
    w("")
    w("| Вид | " + " | ".join(GATES) + " | Всего |")
    w("|---|" + "---|" * (len(GATES) + 1))
    for k in KINDS:
        row = [sum(1 for m in reg["methods"] if m["kind"] == k and m["gate"] == g) for g in GATES]
        if sum(row):
            w(f"| {KIND_NAMES[k]} | " + " | ".join(str(x) for x in row) + f" | {sum(row)} |")
    w(f"| **Всего** | " + " | ".join(str(sum(1 for m in reg['methods'] if m['gate'] == g)) for g in GATES)
      + f" | **{len(reg['methods'])}** |")
    w("")
    for source, title in (("slo", "2. Цели SLO"), ("engineering", "3. Цели инженерного качества")):
        w(f"## {title}")
        w("")
        w("| ID | Цель | Порог | Методы |")
        w("|---|---|---|---|")
        for g in reg["goals"]:
            requirement = definitions[g["id"]]
            if requirement["category"] == source:
                w(f"| {g['id']} | {requirement['name'][language]} | {requirement['target'][language]} | {refs(g['methods'])} |")
        w("")
    w("## 4. Сценарии приёмки")
    w("")
    w("| Сценарий | Методы |")
    w("|---|---|")
    for s in reg["scenarios"]:
        w(f"| [{s['id']}](../product/acceptance-spec.md) | {refs(s['methods'])} |")
    w("")
    w("## 5. Каталог методов")
    w("")
    for k in KINDS:
        items = [m for m in reg["methods"] if m["kind"] == k]
        if not items:
            continue
        w(f"### {KIND_NAMES[k]}")
        w("")
        w("| ID | Метод | Критерий прохождения | Стенд | Барьер | Запуск |")
        w("|---|---|---|---|---|---|")
        for m in items:
            barrier = m["gate"] + (", ручной" if m["barrier"] == "manual" else "")
            task = f"`{m['task']}`" if m["task"] != "-" else "регламент"
            purpose = f" Назначение: {m['purpose']}." if m.get("purpose") else ""
            w(f"| {m['id']} | {m['name']} | {m['criteria']}.{purpose} | {m['env']} | {barrier} | {task} |")
        w("")
    return "\n".join(out)


def main(argv):
    if len(argv) in (5, 6) and argv[1] == "render":
        with open(argv[4], "w", encoding="utf-8") as f:
            f.write(render(argv[2], load(argv[3]), argv[5] if len(argv) == 6 else "en"))
        return 0
    if len(argv) == 4 and argv[1] == "verify":
        return verify(argv[2], load(argv[3]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
