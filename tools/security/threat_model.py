#!/usr/bin/env python3
"""Threat model registry: render the register and verify it.

Commands:
  render <threat-model.yaml> <out.md>
      Generate the Markdown register: summary, boundary x STRIDE matrix, threats,
      requirements, requirements by implementation component.
  verify <repo-root> <threat-model.yaml>
      Check the registry: references, scales, coverage and implementation assignments;
      every high threat has a requirement assigned to an existing component and verified
      by something stronger than review.

Exit code 1 and a list of problems when verification fails.
"""

import os
import importlib.util
from pathlib import Path
import re
import sys

import yaml

STRIDE = {
    "S": "Spoofing",
    "T": "Tampering",
    "R": "Repudiation",
    "I": "Information disclosure",
    "D": "Denial of service",
    "E": "Elevation of privilege",
}
VERIFY = {"test", "config", "scan", "pentest", "review"}
AREAS = ["AUTHN", "AUTHZ", "CRYPTO", "SECRETS", "AUDIT", "INPUT", "DOS", "NET", "INTEGR", "DATA", "SUPPLY", "OPS"]
RISK_NAMES = {"high": "высокий", "medium": "средний", "low": "низкий"}
ENGLISH = {
    "# Реестр угроз и требований безопасности": "# Threat and security requirement register",
    "Сгенерировано из [threat-model.yaml](threat-model.yaml) задачей `task docs:threats:gen`. Не редактировать вручную.":
        "Generated from [threat-model.yaml](threat-model.yaml) by `task docs:threats:gen`. Do not edit manually.",
    "Методика, активы и границы доверия - [threat-model.md](threat-model.md).":
        "Method, assets and trust boundaries: [threat-model.md](threat-model.md).",
    "## 1. Сводка": "## 1. Summary", "| Риск | Угроз |": "| Risk | Threats |", "Всего": "Total",
    "Требований безопасности:": "Security requirements:",
    "## 2. Угрозы по границам и категориям STRIDE": "## 2. Threats by boundary and STRIDE category",
    "| Граница |": "| Boundary |", "## 3. Угрозы": "## 3. Threats",
    "Порядок: по убыванию риска. Риск = вероятность × ущерб.": "Ordered by descending risk. Risk = likelihood × impact.",
    "| ID | Граница | STRIDE | Угроза | Риск | Меры | Остаточный риск |":
        "| ID | Boundary | STRIDE | Threat | Risk | Controls | Residual risk |",
    "## 4. Требования безопасности": "## 4. Security requirements",
    "| ID | Область | Требование | Компоненты | Проверка | Закрывает |":
        "| ID | Area | Requirement | Components | Verification | Threats covered |",
    "## 5. Требования по компонентам": "## 5. Requirements by component",
    "Изменения компонента требуют проверки связанных требований безопасности.":
        "Component changes require verification of their associated security requirements.",
    "| Компонент | Требования |": "| Component | Requirements |",
    "высокий": "high", "средний": "medium", "низкий": "low",
    "Высокий": "High", "Средний": "Medium", "Низкий": "Low",
}


def risk(t):
    score = t["likelihood"] * t["impact"]
    return score, "high" if score >= 6 else "medium" if score >= 3 else "low"


def load(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


# ---------------------------------------------------------------- render

def render(model, language="en", root=None):
    if language == "ru":
        root = Path(root) if root else Path(__file__).resolve().parents[2]
        spec = importlib.util.spec_from_file_location("localization", root / "tools/docs/localization.py")
        locale = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(locale)
        model = locale.localized_artifact(root, "docs/security/threat-model.yaml", model, language)
    threats = model["threats"]
    reqs = {r["id"]: r for r in model["requirements"]}
    covers = {rid: [] for rid in reqs}
    for t in threats:
        for rid in t["requirements"]:
            covers.setdefault(rid, []).append(t["id"])
    out = []
    def w(line):
        if language == "en":
            for source, target in sorted(ENGLISH.items(), key=lambda pair: -len(pair[0])):
                line = line.replace(source, target)
        else:
            line = line.replace("](threat-model.yaml)", "](../../security/threat-model.yaml)")
        out.append(line)
    w("# Реестр угроз и требований безопасности")
    w("")
    w("Сгенерировано из [threat-model.yaml](threat-model.yaml) задачей `task docs:threats:gen`. Не редактировать вручную.")
    w("Методика, активы и границы доверия - [threat-model.md](threat-model.md).")
    w("")
    w("## 1. Сводка")
    w("")
    counts = {"high": 0, "medium": 0, "low": 0}
    for t in threats:
        counts[risk(t)[1]] += 1
    w("| Риск | Угроз |")
    w("|---|---|")
    for k in ("high", "medium", "low"):
        w(f"| {RISK_NAMES[k].capitalize()} | {counts[k]} |")
    w(f"| **Всего** | **{len(threats)}** |")
    w("")
    w(f"Требований безопасности: {len(reqs)}.")
    w("")
    w("## 2. Угрозы по границам и категориям STRIDE")
    w("")
    w("| Граница | " + " | ".join(STRIDE) + " |")
    w("|---|" + "---|" * len(STRIDE))
    for b in model["boundaries"]:
        cells = []
        for s in STRIDE:
            ids = [t["id"] for t in threats if t["boundary"] == b["id"] and t["stride"] == s]
            cells.append(", ".join(ids) if ids else "-")
        w(f"| {b['id']} {b['name']} | " + " | ".join(cells) + " |")
    w("")
    w("## 3. Угрозы")
    w("")
    w("Порядок: по убыванию риска. Риск = вероятность × ущерб.")
    w("")
    w("| ID | Граница | STRIDE | Угроза | Риск | Меры | Остаточный риск |")
    w("|---|---|---|---|---|---|---|")
    for t in sorted(threats, key=lambda x: (-risk(x)[0], x["id"])):
        score, level = risk(t)
        w(f"| {t['id']} | {t['boundary']} | {t['stride']} | **{t['title']}.** {t['scenario']} "
          f"| {RISK_NAMES[level]} ({t['likelihood']}×{t['impact']}) | {', '.join(t['requirements'])} "
          f"| {t['residual']} |")
    w("")
    w("## 4. Требования безопасности")
    w("")
    w("| ID | Область | Требование | Компоненты | Проверка | Закрывает |")
    w("|---|---|---|---|---|---|")
    for r in sorted(model["requirements"], key=lambda x: (AREAS.index(x["area"]), x["id"])):
        w(f"| {r['id']} | {r['area']} | {r['text']} | {', '.join(sorted(r['components']))} "
          f"| {r['verify']} | {', '.join(covers.get(r['id'], []))} |")
    w("")
    w("## 5. Требования по компонентам")
    w("")
    w("Изменения компонента требуют проверки связанных требований безопасности.")
    w("")
    by_component = {}
    for r in model["requirements"]:
        for component in r["components"]:
            by_component.setdefault(component, []).append(r["id"])
    w("| Компонент | Требования |")
    w("|---|---|")
    for component in sorted(by_component):
        w(f"| {component} | {', '.join(sorted(by_component[component]))} |")
    w("")
    return "\n".join(out)


# ---------------------------------------------------------------- verify

def verify(root, model):
    problems = []
    assets = {a["id"] for a in model["assets"]}
    bounds = {b["id"] for b in model["boundaries"]}
    reqs = {}
    for r in model["requirements"]:
        if r["id"] in reqs:
            problems.append(f"duplicate requirement {r['id']}")
        reqs[r["id"]] = r
        if not re.match(r"^SR-\d\d$", r["id"]):
            problems.append(f"{r['id']}: bad id")
        if r["area"] not in AREAS:
            problems.append(f"{r['id']}: unknown area {r['area']}")
        if r["verify"] not in VERIFY:
            problems.append(f"{r['id']}: unknown verify {r['verify']}")
        if not r.get("components"):
            problems.append(f"{r['id']}: no implementation components")
        for component in r.get("components", []):
            target = os.path.realpath(os.path.join(root, component))
            if (os.path.isabs(component) or
                    os.path.commonpath([os.path.realpath(root), target]) != os.path.realpath(root) or
                    not os.path.isdir(target)):
                problems.append(f"{r['id']}: invalid implementation component {component}")
    used = set()
    seen = set()
    for t in model["threats"]:
        tid = t["id"]
        if tid in seen:
            problems.append(f"duplicate threat {tid}")
        seen.add(tid)
        if t["boundary"] not in bounds:
            problems.append(f"{tid}: unknown boundary {t['boundary']}")
        if t["stride"] not in STRIDE:
            problems.append(f"{tid}: unknown STRIDE {t['stride']}")
        for a in t["assets"]:
            if a not in assets:
                problems.append(f"{tid}: unknown asset {a}")
        for k in ("likelihood", "impact"):
            if t[k] not in (1, 2, 3):
                problems.append(f"{tid}: {k} must be 1..3")
        if not t.get("residual"):
            problems.append(f"{tid}: residual risk not stated")
        if not t["requirements"]:
            problems.append(f"{tid}: no requirements")
        for rid in t["requirements"]:
            if rid not in reqs:
                problems.append(f"{tid}: unknown requirement {rid}")
            used.add(rid)
        if risk(t)[1] == "high":
            strong = [rid for rid in t["requirements"] if rid in reqs and reqs[rid]["verify"] != "review"]
            if not strong:
                problems.append(f"{tid}: high risk needs a requirement verified by test, config, scan or pentest")
    for rid in reqs:
        if rid not in used:
            problems.append(f"{rid}: not used by any threat")
    for b in bounds:
        if not any(t["boundary"] == b for t in model["threats"]):
            problems.append(f"{b}: boundary without threats")
    # The narrative document must list the same assets and boundaries.
    with open(os.path.join(root, "docs/security/threat-model.md"), encoding="utf-8") as source:
        md = source.read()
    md_assets = set(re.findall(r"^\| (A\d+) \|", md, re.M))
    md_bounds = set(re.findall(r"^\| (TB\d+) \|", md, re.M))
    if md_assets != assets:
        problems.append(f"threat-model.md assets {sorted(md_assets)} != registry {sorted(assets)}")
    if md_bounds != bounds:
        problems.append(f"threat-model.md boundaries {sorted(md_bounds)} != registry {sorted(bounds)}")
    for p in problems:
        print("FAIL", p)
    high = sum(1 for t in model["threats"] if risk(t)[1] == "high")
    print(f"threats: {len(model['threats'])} ({high} high), {len(reqs)} requirements, {len(problems)} problems")
    return 1 if problems else 0


def main(argv):
    if len(argv) in (4, 5) and argv[1] == "render":
        with open(argv[3], "w", encoding="utf-8") as f:
            f.write(render(load(argv[2]), argv[4] if len(argv) == 5 else "en", Path(argv[2]).resolve().parents[2]))
        return 0
    if len(argv) == 4 and argv[1] == "verify":
        return verify(argv[2], load(argv[3]))
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
