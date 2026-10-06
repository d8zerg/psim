#!/usr/bin/env python3
"""Verification registry: render the matrix and verify coverage (step 0.7).

Commands:
  render <repo-root> <verification.yaml> <out.md>
      Generate the Markdown verification matrix: goals of plan tables 1.3 and 1.4 with
      targets and methods, scenarios S1-S13, method catalog, methods by plan step.
  verify <repo-root> <verification.yaml>
      Check the registry. DoD of step 0.7: every row of plan tables 1.3 and 1.4 and every
      acceptance scenario has a verification method with an automated barrier or a manual
      procedure; SLO goals (1.3) have at least one automated method.

Exit code 1 and a list of problems when verification fails.
"""

import os
import re
import sys

import yaml

KINDS = ["test", "load", "soak", "chaos", "benchmark", "fitness", "security", "manual"]
KIND_NAMES = {
    "test": "Наборы тестов", "load": "Нагрузка", "soak": "Длительная нагрузка", "chaos": "Хаос и отказы",
    "benchmark": "Бенчмарки", "fitness": "Fitness functions", "security": "Безопасность",
    "manual": "Ручные процедуры",
}
GATES = ["pr", "nightly", "release"]
BARRIERS = {"automated", "manual"}
ENVS = {"E-DEV", "E-CI", "E-LOCAL", "E-CLUSTER"}


def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def step_key(step):
    return tuple(int(x) for x in step.split("."))


def plan(root):
    text = open(os.path.join(root, "docs/psim-mvp-development-plan.md"), encoding="utf-8").read()
    steps = set(re.findall(r"^### Шаг (\d+\.\d+) ", text, re.M))
    tables = {}
    for section, nxt in (("### 1.3", "### 1.4"), ("### 1.4", "## 2.")):
        part = text[text.index(section):text.index(nxt, text.index(section))]
        rows = re.findall(r"^\| ([^|]+?) \| ([^|]+?) \|$", part, re.M)
        tables[section[4:]] = {k: v for k, v in rows if k not in ("Атрибут", "Показатель") and not k.startswith("---")}
    return steps, tables


def scenarios(root):
    text = open(os.path.join(root, "docs/product/acceptance-spec.md"), encoding="utf-8").read()
    return re.findall(r"^## (S\d+)\. ", text, re.M)


# ---------------------------------------------------------------- verify

def verify(root, reg):
    problems = []
    steps, tables = plan(root)
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
        for s in m["steps"]:
            if s not in steps:
                problems.append(f"{mid}: plan step {s} does not exist")
    used = set()
    seen_attrs = {}
    for g in reg["goals"]:
        gid = g["id"]
        table = tables.get(g["source"])
        if table is None:
            problems.append(f"{gid}: unknown source {g['source']}")
            continue
        if g["attribute"] not in table:
            problems.append(f"{gid}: attribute {g['attribute']!r} is not a row of plan table {g['source']}")
        seen_attrs.setdefault(g["source"], set()).add(g["attribute"])
        if not g["methods"]:
            problems.append(f"{gid}: no methods")
        for mid in g["methods"]:
            if mid not in methods:
                problems.append(f"{gid}: unknown method {mid}")
            used.add(mid)
        if g["source"] == "1.3" and not any(methods.get(mid, {}).get("barrier") == "automated" for mid in g["methods"]):
            problems.append(f"{gid}: SLO goal needs at least one automated method")
    for source, table in tables.items():
        for attr in table:
            if attr not in seen_attrs.get(source, set()):
                problems.append(f"plan {source}: row {attr!r} has no goal in the registry")
    mapped = {s["id"]: s for s in reg["scenarios"]}
    for sid in scenarios(root):
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

def render(root, reg):
    _, tables = plan(root)
    methods = {m["id"]: m for m in reg["methods"]}
    out = []
    w = out.append

    def refs(ids):
        return ", ".join(f"{i} ({methods[i]['gate']})" for i in ids)

    w("# Матрица верификации")
    w("")
    w("Сгенерировано из [verification.yaml](verification.yaml) задачей `task docs:verification:gen`. Не редактировать вручную.")
    w("Стратегия - [strategy.md](strategy.md). В скобках - барьер метода: `pr` блокирует слияние, `nightly` - следующий выпуск, `release` - выпуск.")
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
    for source, title in (("1.3", "2. Цели SLO (план, раздел 1.3)"), ("1.4", "3. Цели инженерного качества (план, раздел 1.4)")):
        w(f"## {title}")
        w("")
        w("| ID | Цель | Порог | Методы |")
        w("|---|---|---|---|")
        for g in reg["goals"]:
            if g["source"] == source:
                w(f"| {g['id']} | {g['attribute']} | {tables[source][g['attribute']]} | {refs(g['methods'])} |")
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
        w("| ID | Метод | Критерий прохождения | Стенд | Барьер | Запуск | Шаги плана |")
        w("|---|---|---|---|---|---|---|")
        for m in items:
            barrier = m["gate"] + (", ручной" if m["barrier"] == "manual" else "")
            task = f"`{m['task']}`" if m["task"] != "-" else "регламент"
            purpose = f" Назначение: {m['purpose']}." if m.get("purpose") else ""
            w(f"| {m['id']} | {m['name']} | {m['criteria']}.{purpose} | {m['env']} | {barrier} | {task} "
              f"| {', '.join(sorted(m['steps'], key=step_key))} |")
        w("")
    w("## 6. Методы по шагам плана")
    w("")
    w("Шаг создаёт перечисленные методы проверки; задача Task метода появляется вместе с шагом.")
    w("")
    by_step = {}
    for m in reg["methods"]:
        for s in m["steps"]:
            by_step.setdefault(s, []).append(m["id"])
    w("| Шаг | Методы |")
    w("|---|---|")
    for s in sorted(by_step, key=step_key):
        w(f"| {s} | {', '.join(by_step[s])} |")
    w("")
    return "\n".join(out)


def main(argv):
    if len(argv) == 5 and argv[1] == "render":
        with open(argv[4], "w", encoding="utf-8") as f:
            f.write(render(argv[2], load(argv[3])))
        return 0
    if len(argv) == 4 and argv[1] == "verify":
        return verify(argv[2], load(argv[3]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
