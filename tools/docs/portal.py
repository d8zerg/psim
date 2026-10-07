#!/usr/bin/env python3
"""Sources of the documentation portal (step 1.6, ADR-038).

  stage <repo-root> <out-dir> <c4-mermaid-dir> <repo-url> <commit>

Writes <out-dir>/mkdocs.yml (template tools/docs/portal/mkdocs.base.yml plus the navigation built from
the sections of docs/README.md, the ADR registry and the references) and <out-dir>/src. Copies the documentation of the repository into <out-dir> with the repository layout, so relative
links keep working, and generates the reference pages from the contracts:
  reference/kafka-topics.md   from contracts/asyncapi/psim-topics.yaml (AsyncAPI)
  reference/error-codes.md    from contracts/errors/errors.yaml
  reference/c4.md             from the views exported from docs/architecture/c4/workspace.dsl
The Protobuf reference (contracts/docs/proto-reference.md) and the OpenAPI document are generated
and checked by `task contracts:check`; the REST reference HTML is built from the OpenAPI document.

Every page is then rewritten for a static, offline portal:
  - a Mermaid block becomes an image; its source is written to _diagrams/<page>-<n>.mmd and rendered
    to SVG by mermaid-cli (task docs:portal);
  - a relative link to a file that is not part of the portal (code, YAML, proto) points to the
    repository at the commit the portal is built from.
"""

import os
import re
import shutil
import sys

import yaml

# Pages of the portal besides docs/ (repository-relative).
EXTRA_PAGES = ["README.md", "contracts/README.md", "contracts/docs/proto-reference.md", "deploy/README.md",
               "web/README.md"]
# MkDocs treats README.md as the index of its directory; the root one would clash with index.md.
RENAMED = {"README.md": "project.md"}
# Well-known types the Protobuf reference links to without describing them.
WELL_KNOWN = [
    (re.compile(r"\]\(#google-protobuf-(\w+)\)"),
     lambda m: f"](https://protobuf.dev/reference/protobuf/google.protobuf/#{m.group(1).lower()})"),
    (re.compile(r"\]\(#google-api-(\w+)\)"),
     lambda m: f"](https://github.com/googleapis/googleapis/tree/master/google/api#{m.group(1).lower()})"),
]
# Built after staging (task docs:portal); links to them stay inside the portal.
BUILT_LATER = {"reference/rest-api.html"}
LINK = re.compile(r"(!?\[[^\]]*\]\()([^)\s]+)(\))")
MERMAID = re.compile(r"^```mermaid\n(.*?)^```\n", re.S | re.M)
C4_TITLES = {
    "L1-Context": "Уровень 1. Контекст системы",
    "L2-Containers": "Уровень 2. Контейнеры",
    "L3-ConnectorGateway": "Уровень 3. Компоненты Connector Gateway",
    "L3-CorrelationEngine": "Уровень 3. Компоненты Correlation Engine",
    "L3-IncidentService": "Уровень 3. Компоненты Incident Service",
    "Deploy-SingleNode": "Развёртывание: одна машина",
    "Deploy-VMCluster": "Развёртывание: кластер на VM",
}


def write(path, text):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


def load_yaml(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


def cell(value):
    return str(value).replace("|", "\\|").replace("\n", " ")


GENERATED = "Сгенерировано при сборке портала из `{}`; вручную не редактируется."


# --- generated reference pages ---------------------------------------------------------------

def kafka_topics(root):
    doc = load_yaml(os.path.join(root, "contracts/asyncapi/psim-topics.yaml"))
    messages = doc["components"]["messages"]
    producers, consumers = {}, {}
    for op in doc["operations"].values():
        channel = op["channel"]["$ref"].rsplit("/", 1)[-1]
        target = producers if op["action"] == "send" else consumers
        target.setdefault(channel, []).append(op.get("x-psim-service", "?"))
    out = ["# Топики Kafka (AsyncAPI)", "", GENERATED.format("contracts/asyncapi/psim-topics.yaml"), "",
           f"{doc['info']['title']}, версия {doc['info']['version']}. Семантика доставки - "
           "[data-flows.md](../docs/architecture/data-flows.md), партиционирование - "
           "[scaling.md](../docs/architecture/scaling.md).", "",
           "| Топик | Назначение | Сообщение | Ключ | Партиции (кластер / одна машина) | Retention | Семантика | Пишут | Читают |",
           "|---|---|---|---|---|---|---|---|---|"]
    for cid, ch in doc["channels"].items():
        kafka = ch["bindings"]["kafka"]
        msg = messages[next(iter(ch["messages"].values()))["$ref"].rsplit("/", 1)[-1]]
        proto = msg["payload"].get("x-psim-proto")
        message = (f"[{proto.rsplit('.', 1)[-1]}](../contracts/docs/proto-reference.md#{proto.replace('.', '-')})"
                   if proto else msg["name"])
        key = msg["bindings"]["kafka"]["key"]["description"] if "bindings" in msg else "исходный ключ"
        parts = ch.get("x-psim-partitions", {})
        ms = kafka["topicConfiguration"]["retention.ms"]
        retention = "бессрочно" if ms < 0 else f"{ms // 3_600_000} ч" if ms < 86_400_000 * 2 else f"{ms // 86_400_000} сут"
        out.append(f"| `{ch['address']}` | {cell(ch['title'])} | {message} | `{key}` | "
                   f"{parts.get('cluster', kafka['partitions'])} / {parts.get('single', '-')} | {retention} | "
                   f"{ch.get('x-psim-semantics', '')} | {', '.join(sorted(producers.get(cid, [])))} | "
                   f"{', '.join(sorted(consumers.get(cid, [])))} |")
    return "\n".join(out) + "\n"


def error_codes(root):
    reg = load_yaml(os.path.join(root, "contracts/errors/errors.yaml"))
    out = ["# Каталог кодов ошибок", "", GENERATED.format("contracts/errors/errors.yaml"), "",
           "Код возвращается в `Problem.code` (REST, RFC 9457), причинах ошибок протокола коннекторов, "
           "заголовках DLQ, результатах команд и кадрах realtime. Коды не переименовываются и не переиспользуются.", ""]
    for domain in reg["domains"]:
        codes = [c for c in reg["codes"] if c["code"].startswith(domain + "_")]
        if not codes:
            continue
        out += [f"## {domain}", "", "| Код | HTTP | gRPC | Повтор | Где | Описание |", "|---|---|---|---|---|---|"]
        for c in codes:
            out.append(f"| `{c['code']}` | {c.get('http') or '-'} | {c.get('grpc') or '-'} | "
                       f"{'да' if c.get('retryable') else 'нет'} | {', '.join(c.get('used_in', []))} | {cell(c['ru'])} |")
        out.append("")
    return "\n".join(out)


def c4(mermaid_dir):
    out = ["# Модель C4", "", GENERATED.format("docs/architecture/c4/workspace.dsl") + " Представления выгружены "
           "Structurizr в Mermaid; описание архитектуры - [arc42](../docs/architecture/README.md).", ""]
    for key, title in C4_TITLES.items():
        path = os.path.join(mermaid_dir, f"structurizr-{key}.mmd")
        if not os.path.exists(path):
            raise SystemExit(f"portal: C4 view {key} is missing in {mermaid_dir}; update C4_TITLES")
        with open(path, encoding="utf-8") as f:
            out += [f"## {title}", "", "```mermaid", f.read().rstrip(), "```", ""]
    extra = sorted(set(n[len("structurizr-"):-4] for n in os.listdir(mermaid_dir) if n.endswith(".mmd")) - set(C4_TITLES))
    if extra:
        raise SystemExit(f"portal: C4 views without a title: {extra}; add them to C4_TITLES")
    return "\n".join(out)


def index():
    return "\n".join([
        "# PSIM Platform - документация", "",
        "Портал собирается из репозитория (шаг 1.6): документы - из `docs/`, справочники - из контрактов "
        "при каждой сборке, поэтому не расходятся с ними.", "",
        "- [Документация: продукт, предметная область, архитектура, ADR, качество, безопасность](docs/README.md)",
        "- [План MVP](docs/psim-mvp-development-plan.md) · [прогресс](docs/progress.md)",
        "- [Разработка: команды и окружение](project.md)", "",
        "## Справочники", "",
        "- [REST API v1 (OpenAPI)](reference/rest-api.html)",
        "- [Топики Kafka (AsyncAPI)](reference/kafka-topics.md)",
        "- [Сообщения и сервисы (Protobuf)](contracts/docs/proto-reference.md)",
        "- [Коды ошибок](reference/error-codes.md)",
        "- [Модель C4](reference/c4.md)",
        "- [Контракты: состав и соглашения](contracts/README.md)", "",
    ])


# --- staging ----------------------------------------------------------------------------------

def copy_sources(root, out):
    for dirpath, _, filenames in os.walk(os.path.join(root, "docs")):
        for name in filenames:
            if name in ("Taskfile.yml",):
                continue
            src = os.path.join(dirpath, name)
            dst = os.path.join(out, os.path.relpath(src, root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
    for rel in EXTRA_PAGES:
        dst = os.path.join(out, RENAMED.get(rel, rel))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(os.path.join(root, rel), dst)


def rewrite(root, out, repo_url, commit):
    """Mermaid blocks -> images, links outside the portal -> repository. Returns diagram count."""
    diagrams = 0
    for dirpath, _, filenames in os.walk(out):
        for name in filenames:
            if not name.endswith(".md"):
                continue
            page = os.path.join(dirpath, name)
            rel_page = os.path.relpath(page, out)
            with open(page, encoding="utf-8") as f:
                text = f.read()
            stem = os.path.splitext(name)[0]
            counter = iter(range(1, 1000))

            def diagram(m):
                nonlocal diagrams
                n = next(counter)
                diagrams += 1
                write(os.path.join(dirpath, "_diagrams", f"{stem}-{n}.mmd"), m.group(1))
                svg = f"_diagrams/{stem}-{n}.svg"
                return f"[![Диаграмма {n}]({svg})]({svg})\n"  # the image opens full size
            text = MERMAID.sub(diagram, text)

            def link(m):
                target = m.group(2)
                if re.match(r"^[a-z]+:|^#", target):
                    return m.group(0)
                path, _, anchor = target.partition("#")
                staged = os.path.normpath(os.path.join(os.path.dirname(rel_page), path))
                if staged in RENAMED:
                    new = os.path.relpath(RENAMED[staged], os.path.dirname(rel_page) or ".")
                    return f"{m.group(1)}{new}{'#' + anchor if anchor else ''}{m.group(3)}"
                if os.path.exists(os.path.join(out, staged)) or staged in BUILT_LATER or path.startswith("_diagrams/"):
                    return m.group(0)
                repo_path = os.path.normpath(os.path.join(os.path.dirname(rel_page), path))
                if not os.path.exists(os.path.join(root, repo_path)):
                    return m.group(0)  # broken link: left for the strict MkDocs build to report
                kind = "tree" if os.path.isdir(os.path.join(root, repo_path)) else "blob"
                url = f"{repo_url}/{kind}/{commit}/{repo_path}" + (f"#{anchor}" if anchor else "")
                return f"{m.group(1)}{url}{m.group(3)}"
            text = LINK.sub(link, text)
            for pattern, replacement in WELL_KNOWN:
                text = pattern.sub(replacement, text)
            with open(page, "w", encoding="utf-8") as f:
                f.write(text)
    return diagrams


HEADING = re.compile(r"^#\s+(.+)$", re.M)
MD_LINK = re.compile(r"\[([^\]]+)\]\(([^)#\s]+\.md)\)")


def title_of(path):
    with open(path, encoding="utf-8") as f:
        m = HEADING.search(f.read())
    return m.group(1).strip() if m else os.path.basename(path)


def navigation(src):
    """Sections of docs/README.md, then ADRs, references and development pages.

    A page appears once: the fixed sections own ADRs, references and development pages, and the
    entries of docs/README.md that point to them are skipped (MkDocs titles a page by its first
    navigation entry).
    """
    adr_dir = os.path.join(src, "docs", "adr")
    adrs = [{"Реестр": "docs/adr/README.md"}] + [
        {title_of(os.path.join(adr_dir, n)): f"docs/adr/{n}"} for n in sorted(os.listdir(adr_dir)) if re.match(r"\d{4}-", n)
    ] + [{"Шаблон": "docs/adr/template.md"}]
    fixed = [
        {"Архитектурные решения": adrs},
        {"Справочники": [
            {"Топики Kafka (AsyncAPI)": "reference/kafka-topics.md"},
            {"Protobuf": "contracts/docs/proto-reference.md"},
            {"Коды ошибок": "reference/error-codes.md"},
            {"Модель C4": "reference/c4.md"},
            {"Контракты": "contracts/README.md"},
        ]},
        {"Разработка": [{"Команды": "project.md"}, {"Развёртывание": "deploy/README.md"},
                        {"Веб-приложение": "web/README.md"}]},
    ]
    owned = {path for section in fixed for entry in next(iter(section.values())) for path in entry.values()}
    nav = [{"Главная": "index.md"}]
    with open(os.path.join(src, "docs", "README.md"), encoding="utf-8") as f:
        for line in f:
            if line.startswith("## "):
                nav.append({line[3:].strip(): []})
            elif len(nav) > 1 and line.lstrip().startswith("- "):
                for text, target in MD_LINK.findall(line):
                    path = os.path.normpath(os.path.join("docs", target))
                    if path not in owned and os.path.exists(os.path.join(src, path)):
                        next(iter(nav[-1].values())).append({text: path})
                        owned.add(path)
    nav = [item for item in nav if next(iter(item.values()))]
    return nav + fixed


def config(root, out, repo_url, commit):
    base = load_yaml(os.path.join(root, "tools/docs/portal/mkdocs.base.yml"))
    base.update({
        "docs_dir": "src",
        "site_dir": "site",
        "repo_url": repo_url,
        "edit_uri": "",
        "copyright": f"Собрано из коммита {commit[:12]}",
        "nav": navigation(os.path.join(out, "src")),
    })
    text = yaml.safe_dump(base, allow_unicode=True, sort_keys=False)
    text = text.replace("slugify: SLUGIFY", "slugify: !!python/object/apply:pymdownx.slugs.slugify {kwds: {case: lower}}")
    write(os.path.join(out, "mkdocs.yml"), text)


def stage(root, out_root, c4_dir, repo_url, commit):
    out = os.path.join(out_root, "src")
    if os.path.exists(out_root):
        shutil.rmtree(out_root)
    copy_sources(root, out)
    write(os.path.join(out, "index.md"), index())
    write(os.path.join(out, "reference", "kafka-topics.md"), kafka_topics(root))
    write(os.path.join(out, "reference", "error-codes.md"), error_codes(root))
    write(os.path.join(out, "reference", "c4.md"), c4(c4_dir))
    diagrams = rewrite(root, out, repo_url, commit)
    config(root, out_root, repo_url, commit)
    pages = sum(1 for _, _, files in os.walk(out) for n in files if n.endswith(".md"))
    print(f"portal: {pages} pages, {diagrams} diagrams staged in {out}")
    return 0


def main(argv):
    if len(argv) == 7 and argv[1] == "stage":
        return stage(*argv[2:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
