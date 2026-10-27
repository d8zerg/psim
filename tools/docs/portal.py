#!/usr/bin/env python3
"""Sources of the documentation portal (ADR-038).

  stage <repo-root> <out-dir> <c4-plantuml-dir> <repo-url> <commit>

Writes <out-dir>/mkdocs.yml (template tools/docs/portal/mkdocs.base.yml plus the navigation built from
the sections of docs/README.md, the ADR registry and the references) and <out-dir>/src. Copies the documentation of the repository into <out-dir> with the repository layout, so relative
links keep working, and generates the reference pages from the contracts:
  reference/kafka-topics.md   from contracts/asyncapi/psim-topics.yaml (AsyncAPI)
  reference/error-codes.md    from contracts/errors/errors.yaml
  reference/c4.md             from the views exported from docs/architecture/c4/L2-Containers.puml
The Protobuf reference (contracts/docs/proto-reference.md) and the OpenAPI document are generated
and checked by `task contracts:check`; the REST reference HTML is built from the OpenAPI document.

Every page is then rewritten for a static, offline portal:
  - a PlantUML block becomes an image; its source is written to _diagrams/<page>-<n>.puml and rendered
    to SVG by PlantUML (task docs:portal);
  - a relative link to a file that is not part of the portal (code, YAML, proto) points to the
    repository at the commit the portal is built from.
"""

import json
import importlib.util
import os
from html.parser import HTMLParser
from pathlib import Path
import re
import shutil
import sys
from urllib.parse import unquote

import yaml

SPEC = importlib.util.spec_from_file_location("localization", Path(__file__).with_name("localization.py"))
I18N = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(I18N)

# Pages of the portal besides docs/ (repository-relative).
EXTRA_PAGES = ["README.md", "CONTRIBUTING.md", "SECURITY.md", "contracts/README.md", "contracts/docs/proto-reference.md", "deploy/README.md",
               "web/README.md", "libs/platform/pg/README.md", "libs/platform/redis/README.md", "libs/platform/security/README.md", "libs/platform/clickhouse/README.md", "libs/platform/s3/README.md",
               "libs/sim/README.md"]
# MkDocs treats README.md as the index of its directory; the root one would clash with index.md.
RENAMED = {"README.md": "project.md"}
# Development-only inputs are never part of the released documentation.
DEVELOPMENT_FILES = {"psim-mvp-development-plan.md", "progress.md", "psim-mvp-diagrams.drawio"}
# Built after staging (task docs:portal); links to them stay inside the portal.
BUILT_LATER = {"reference/rest-api.html"}
LINK = re.compile(r"(!?\[[^\]]*\]\()([^)\s]+)(\))")
PLANTUML = re.compile(r"^```plantuml\n(.*?)^```\n", re.S | re.M)
C4_TITLES = {
    "L1-Context": "Уровень 1. Контекст системы",
    "L2-Containers": "Уровень 2. Контейнеры",
    "L3-ConnectorGateway": "Уровень 3. Компоненты Connector Gateway",
    "L3-CorrelationEngine": "Уровень 3. Компоненты Correlation Engine",
    "L3-IncidentService": "Уровень 3. Компоненты Incident Service",
    "Deploy-SingleNode": "Развёртывание: одна машина",
    "Deploy-VMCluster": "Развёртывание: кластер на VM",
}
C4_ENGLISH = {
    "L1-Context": "Level 1. System context", "L2-Containers": "Level 2. Containers",
    "L3-ConnectorGateway": "Level 3. Connector Gateway components",
    "L3-CorrelationEngine": "Level 3. Correlation Engine components",
    "L3-IncidentService": "Level 3. Incident Service components",
    "Deploy-SingleNode": "Deployment: single machine", "Deploy-VMCluster": "Deployment: VM cluster",
}


def choose(language, en, ru):
    return en if language == "en" else ru


def generated(language, source):
    return choose(language, "Generated from `{}` during the portal build; do not edit manually.",
                  "Сгенерировано при сборке портала из `{}`; вручную не редактируется.").format(source)


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

def kafka_topics(root, language="en"):
    doc = load_yaml(os.path.join(root, "contracts/asyncapi/psim-topics.yaml"))
    registry = load_yaml(os.path.join(root, "contracts/topics/topics.yaml"))
    registry = I18N.localized_artifact(root, "contracts/topics/topics.yaml", registry, language)
    titles = {topic["name"]: topic["title"] for topic in registry["topics"]}
    for stage_name in registry["dlq"]["stages"]:
        titles[f"psim.dlq.{stage_name}.v1"] = choose(language, "Dead-letter queue: ", "Очередь ошибочных сообщений: ") + stage_name
    messages = doc["components"]["messages"]
    producers, consumers = {}, {}
    for op in doc["operations"].values():
        channel = op["channel"]["$ref"].rsplit("/", 1)[-1]
        target = producers if op["action"] == "send" else consumers
        target.setdefault(channel, []).append(op.get("x-psim-service", "?"))
    out = [choose(language, "# Kafka topics (AsyncAPI)", "# Топики Kafka (AsyncAPI)"), "",
           generated(language, "contracts/asyncapi/psim-topics.yaml"), "",
           choose(language, "Delivery semantics: [data-flows.md](../docs/architecture/data-flows.md); partitioning: ",
                  "Семантика доставки: [data-flows.md](../docs/architecture/data-flows.md); партиционирование: ")
           + "[scaling.md](../docs/architecture/scaling.md).", "",
           choose(language, "| Topic | Purpose | Message | Key | Partitions (cluster / single) | Retention | Semantics | Producers | Consumers |",
                  "| Топик | Назначение | Сообщение | Ключ | Партиции (кластер / одна машина) | Retention | Семантика | Пишут | Читают |"),
           "|---|---|---|---|---|---|---|---|---|"]
    for cid, ch in doc["channels"].items():
        kafka = ch["bindings"]["kafka"]
        msg = messages[next(iter(ch["messages"].values()))["$ref"].rsplit("/", 1)[-1]]
        proto = msg["payload"].get("x-psim-proto")
        message = (f"[{proto.rsplit('.', 1)[-1]}](../contracts/docs/proto-reference.md#{proto.replace('.', '-')})"
                   if proto else msg["name"])
        key = msg["bindings"]["kafka"]["key"]["description"] if "bindings" in msg else choose(language, "original key", "исходный ключ")
        parts = ch.get("x-psim-partitions", {})
        ms = kafka["topicConfiguration"]["retention.ms"]
        retention = (choose(language, "unlimited", "бессрочно") if ms < 0 else
                     f"{ms // 3_600_000} " + choose(language, "h", "ч") if ms < 86_400_000 * 2 else
                     f"{ms // 86_400_000} " + choose(language, "days", "сут"))
        out.append(f"| `{ch['address']}` | {cell(titles[ch['address']])} | {message} | `{key}` | "
                   f"{parts.get('cluster', kafka['partitions'])} / {parts.get('single', '-')} | {retention} | "
                   f"{ch.get('x-psim-semantics', '')} | {', '.join(sorted(producers.get(cid, [])))} | "
                   f"{', '.join(sorted(consumers.get(cid, [])))} |")
    return "\n".join(out) + "\n"


def error_codes(root, language="en"):
    reg = load_yaml(os.path.join(root, "contracts/errors/errors.yaml"))
    out = [choose(language, "# Error code catalog", "# Каталог кодов ошибок"), "", generated(language, "contracts/errors/errors.yaml"), "",
           choose(language, "Codes appear in `Problem.code` (REST, RFC 9457), connector errors, DLQ headers, command results and realtime frames. Codes are never renamed or reused.",
                  "Коды возвращаются в `Problem.code` (REST, RFC 9457), ошибках коннекторов, заголовках DLQ, результатах команд и кадрах realtime. Коды не переименовываются и не переиспользуются."), ""]
    for domain in reg["domains"]:
        codes = [c for c in reg["codes"] if c["code"].startswith(domain + "_")]
        if not codes:
            continue
        out += [f"## {domain}", "", choose(language, "| Code | HTTP | gRPC | Retry | Usage | Description |",
                                           "| Код | HTTP | gRPC | Повтор | Где | Описание |"), "|---|---|---|---|---|---|"]
        for c in codes:
            out.append(f"| `{c['code']}` | {c.get('http') or '-'} | {c.get('grpc') or '-'} | "
                       f"{choose(language, 'yes', 'да') if c.get('retryable') else choose(language, 'no', 'нет')} | "
                       f"{', '.join(c.get('used_in', []))} | {cell(c[language])} |")
        out.append("")
    return "\n".join(out)


def c4(plantuml_dir, language="en"):
    out = [choose(language, "# C4 model", "# Модель C4"), "", generated(language, "docs/architecture/c4/L2-Containers.puml") + " " +
           choose(language, "PlantUML renders version-controlled sources. Architecture: ",
                  "PlantUML отображает версионируемые исходники. Архитектура: ") + "[arc42](../docs/architecture/README.md).", ""]
    for key, title in (C4_ENGLISH if language == "en" else C4_TITLES).items():
        path = os.path.join(plantuml_dir, f"{key}.puml")
        if not os.path.exists(path):
            raise SystemExit(f"portal: C4 view {key} is missing in {plantuml_dir}; update C4_TITLES")
        with open(path, encoding="utf-8") as f:
            out += [f"## {title}", "", "```plantuml", f.read().rstrip(), "```", ""]
    extra = sorted(set(n[:-5] for n in os.listdir(plantuml_dir) if n.endswith(".puml")) - set(C4_TITLES))
    if extra:
        raise SystemExit(f"portal: C4 views without a title: {extra}; add them to C4_TITLES")
    return "\n".join(out)


def index(language="en"):
    def t(en, ru):
        return choose(language, en, ru)
    return "\n".join([
        "# Open-Source PSIM Platform - " + t("documentation", "документация"), "",
        t("The portal is built from maintained repository documents and contract-generated references.",
          "Портал собирается из поддерживаемых документов репозитория и справочников, генерируемых из контрактов."), "",
        "- [" + t("Documentation: product, domain, architecture, ADRs, quality and security", "Документация: продукт, предметная область, архитектура, ADR, качество, безопасность") + "](docs/README.md)",
        "- [" + t("Implemented capabilities and release conditions", "Реализованные функции и условия выпуска") + "](docs/product/release-readiness.md)",
        "- [" + t("Permanent quality requirements", "Постоянные требования качества") + "](docs/quality/requirements.yaml)",
        "- [" + t("Development commands and environment", "Команды разработки и окружение") + "](project.md)", "",
        t("## References", "## Справочники"), "",
        "- [REST API v1 (OpenAPI)](reference/rest-api.html)",
        "- [" + t("Kafka topics (AsyncAPI)", "Топики Kafka (AsyncAPI)") + "](reference/kafka-topics.md)",
        "- [" + t("Messages and services (Protobuf)", "Сообщения и сервисы (Protobuf)") + "](contracts/docs/proto-reference.md)",
        "- [" + t("Error codes", "Коды ошибок") + "](reference/error-codes.md)",
        "- [" + t("C4 model", "Модель C4") + "](reference/c4.md)",
        "- [" + t("Contracts and conventions", "Контракты и соглашения") + "](contracts/README.md)", "",
    ])


def rest_api(root, out, language="en"):
    """Render from the local contract without server/client React hydration differences."""
    spec = load_yaml(os.path.join(root, "contracts/openapi/psim-api-v1.yaml"))
    spec = I18N.localized_artifact(root, "contracts/openapi/psim-api-v1.yaml", spec, language)
    write(os.path.join(out, "reference/openapi.json"), json.dumps(spec, ensure_ascii=False) + "\n")
    write(os.path.join(out, "reference/rest-api.html"), """<!doctype html>
<html lang="LANGUAGE"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>REST API - Open-Source PSIM Platform</title>
<style>body{margin:0;font-family:system-ui,sans-serif}nav{padding:1rem;background:#263238}
nav a{color:white}#reference{min-height:100vh}</style></head><body>
<nav><a href="../index.html">Open-Source PSIM Platform</a> | <a data-language-switch href="ALTERNATE">LANGUAGE_LABEL</a></nav>
<noscript><a href="openapi.json">OpenAPI JSON</a></noscript>
<div id="reference"></div><script src="redoc.standalone.js"></script>
<script>Redoc.init('openapi.json', {hideDownloadButton: false, nativeScrollbars: true,
theme: {typography: {fontFamily: 'system-ui, sans-serif', code: {fontFamily: 'monospace'}}}},
document.getElementById('reference'));</script><script src="rest-language.js"></script></body></html>
""".replace("LANGUAGE_LABEL", choose(language, "Русский", "English"))
        .replace("LANGUAGE", language).replace("ALTERNATE", choose(language, "../ru/reference/rest-api.html", "../../reference/rest-api.html")))
    shutil.copyfile(os.path.join(root, "tools/docs/portal/rest-language.js"), os.path.join(out, "reference/rest-language.js"))


# --- staging ----------------------------------------------------------------------------------

def copy_sources(root, out, language, manifest):
    selected = {(Path(root) / page[language]).resolve(): RENAMED.get(page["en"], page["en"]) for page in manifest["pages"]}
    selected.update({(Path(root) / pair[language]).resolve(): pair["en"] for pair in manifest.get("assets", [])})
    for dirpath, children, filenames in os.walk(os.path.join(root, "docs")):
        children[:] = [name for name in children if name != "ru"]
        for name in filenames:
            if name == "Taskfile.yml" or name in DEVELOPMENT_FILES or Path(name).suffix in {".md", ".puml"}:
                continue
            src = os.path.join(dirpath, name)
            dst = os.path.join(out, os.path.relpath(src, root))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copy2(src, dst)
    for source, logical in selected.items():
        text = source.read_text(encoding="utf-8")
        if source.suffix == ".md":
            text = re.sub(r"<!-- language-links -->.*?<!-- /language-links -->\n*", "", text, flags=re.S)

            def link(match):
                destination = match.group(2)
                if re.match(r"^[a-z]+:|^#", destination):
                    return match.group(0)
                path, _, anchor = destination.partition("#")
                target = (source.parent / unquote(path)).resolve()
                if target in selected:
                    relative = selected[target]
                elif target.is_relative_to(Path(root).resolve()):
                    relative = target.relative_to(Path(root).resolve()).as_posix()
                else:
                    raise ValueError(f"page link escapes the repository: {source}: {destination}")
                relative = os.path.relpath(relative, os.path.dirname(logical) or ".")
                return match.group(1) + relative + ("#" + anchor if anchor else "") + match.group(3)

            text = LINK.sub(link, text)
        elif source.suffix == ".html":
            text = runbook_html(text, logical, language, manifest)
        write(os.path.join(out, logical), text)


def runbook_html(text, logical, language, manifest):
    """Resolve runtime instruction URLs inside the standalone bilingual archive."""
    aliases = {}
    for pair in manifest.get("assets", []):
        for locale in ("en", "ru"):
            target = Path(pair["en"])
            if locale == "ru":
                target = Path("ru") / target
            aliases[Path(pair[locale]).stem] = target
    aliases[""] = aliases["index"]
    current = Path("ru") / logical if language == "ru" else Path(logical)

    def link(match):
        alias = match[1]
        if alias not in aliases:
            raise ValueError(f"unknown runbook URL: {alias}")
        return os.path.relpath(aliases[alias], current.parent)

    text = re.sub(r'/operations/runbooks/([A-Za-z0-9]*)(?:\.html)?', link, text)
    text = re.sub(r'<a href="(/monitoring/[^\"]+)">([^<]+)</a>',
                  r'<span>\2: <code>\1</code></span>', text)
    home = os.path.relpath(Path("ru/index.html") if language == "ru" else Path("index.html"), current.parent)
    label = choose(language, "Documentation", "Документация")
    return text.replace("<header>", f'<header><a href="{home}">{label}</a>', 1)


class RunbookSearch(HTMLParser):
    def __init__(self):
        super().__init__()
        self.in_main = False
        self.in_title = False
        self.title = []
        self.text = []

    def handle_starttag(self, tag, attrs):
        if tag == "main":
            self.in_main = True
        if tag == "h1":
            self.in_title = True

    def handle_endtag(self, tag):
        if tag == "main":
            self.in_main = False
        if tag == "h1":
            self.in_title = False

    def handle_data(self, data):
        if self.in_main:
            self.text.append(data)
        if self.in_title:
            self.title.append(data)


def finalize(site):
    """Add copied HTML instructions to each language's local search index."""
    for language in ("en", "ru"):
        directory = Path(site) / "ru" if language == "ru" else Path(site)
        index_path = directory / "search/search_index.json"
        index = json.loads(index_path.read_text(encoding="utf-8"))
        books = sorted((directory / "deploy/observability/runbooks").glob("*.html"))
        locations = {book.relative_to(directory).as_posix() for book in books}
        index["docs"] = [item for item in index["docs"] if item["location"] not in locations]
        for book in books:
            parser = RunbookSearch()
            parser.feed(book.read_text(encoding="utf-8"))
            index["docs"].append({"location": book.relative_to(directory).as_posix(),
                                  "title": "".join(parser.title), "text": " ".join(parser.text)})
        write(index_path, json.dumps(index, ensure_ascii=False))
        print(f"portal search: {language}: {len(books)} HTML instructions")
    return 0


def rewrite(root, out, repo_url, commit, language="en"):
    """PlantUML blocks -> images, links outside the portal -> repository. Returns diagram count."""
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
                write(os.path.join(dirpath, "_diagrams", f"{stem}-{n}.puml"), m.group(1))
                svg = f"_diagrams/{stem}-{n}.svg"
                return f"[![{choose(language, 'Diagram', 'Диаграмма')} {n}]({svg})]({svg})\n"
            text = PLANTUML.sub(diagram, text)

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
            with open(page, "w", encoding="utf-8") as f:
                f.write(text)
    return diagrams


HEADING = re.compile(r"^#\s+(.+)$", re.M)
MD_LINK = re.compile(r"\[([^\]]+)\]\(([^)#\s]+\.md)\)")


def title_of(path):
    with open(path, encoding="utf-8") as f:
        m = HEADING.search(f.read())
    return m.group(1).strip() if m else os.path.basename(path)


def navigation(src, language="en"):
    """Sections of docs/README.md, then ADRs, references and development pages.

    A page appears once: the fixed sections own ADRs, references and development pages, and the
    entries of docs/README.md that point to them are skipped (MkDocs titles a page by its first
    navigation entry).
    """
    def t(en, ru):
        return choose(language, en, ru)
    adr_dir = os.path.join(src, "docs", "adr")
    adrs = [{t("Registry", "Реестр"): "docs/adr/README.md"}] + [
        {title_of(os.path.join(adr_dir, n)): f"docs/adr/{n}"} for n in sorted(os.listdir(adr_dir)) if re.match(r"\d{4}-", n)
    ] + [{t("Template", "Шаблон"): "docs/adr/template.md"}]
    fixed = [
        {t("Architecture decisions", "Архитектурные решения"): adrs},
        {t("References", "Справочники"): [
            {t("Kafka topics (AsyncAPI)", "Топики Kafka (AsyncAPI)"): "reference/kafka-topics.md"},
            {"Protobuf": "contracts/docs/proto-reference.md"},
            {t("Error codes", "Коды ошибок"): "reference/error-codes.md"},
            {t("C4 model", "Модель C4"): "reference/c4.md"},
            {t("Contracts", "Контракты"): "contracts/README.md"},
            {t("Operational runbooks", "Эксплуатационные инструкции"): "deploy/observability/runbooks/index.html"},
        ]},
        {t("Development", "Разработка"): [{t("Commands", "Команды"): "project.md"},
            {t("Deployment", "Развёртывание"): "deploy/README.md"}, {t("Web application", "Веб-приложение"): "web/README.md"}]},
    ]
    owned = {path for section in fixed for entry in next(iter(section.values())) for path in entry.values()}
    nav = [{t("Home", "Главная"): "index.md"}]
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


def config(root, out, repo_url, commit, language, alternates):
    base = load_yaml(os.path.join(root, "tools/docs/portal/mkdocs.base.yml"))
    shutil.copytree(os.path.join(root, "tools/docs/portal/overrides"), os.path.join(out, "overrides"))
    base["theme"]["custom_dir"] = "overrides"
    base["theme"]["language"] = language
    base["theme"]["palette"][0]["toggle"]["name"] = choose(language, "Light theme", "Светлая тема")
    base["theme"]["palette"][1]["toggle"]["name"] = choose(language, "Dark theme", "Тёмная тема")
    base["plugins"] = [{"search": {"lang": ["en"] if language == "en" else ["ru", "en"]}}]
    base.update({
        "docs_dir": "src",
        "site_dir": "site" if language == "en" else "../site/ru",
        "site_description": choose(language, "Open-Source PSIM Platform documentation", "Документация Open-Source PSIM Platform"),
        "repo_url": repo_url,
        "edit_uri": "",
        "copyright": choose(language, "Built from commit ", "Собрано из коммита ") + commit[:12],
        "nav": navigation(os.path.join(out, "src"), language),
        "extra": {"document_language": language, "page_alternates": alternates},
        "extra_javascript": ["assets/docs-languages.js"],
    })
    text = yaml.safe_dump(base, allow_unicode=True, sort_keys=False)
    text = text.replace("slugify: SLUGIFY", "slugify: !!python/object/apply:pymdownx.slugs.slugify {kwds: {case: lower}}")
    write(os.path.join(out, "mkdocs.yml"), text)


def heading_ids(text):
    """Use repository anchors, including collision suffixes, in rendered headings."""
    anchors = iter(anchor for _, anchor in I18N.headings(text))
    blocks = list(I18N.FENCES.finditer(text))

    def replace(match):
        if any(block.start() <= match.start() < block.end() for block in blocks):
            return match.group(0)
        return match.group(0) + " {#" + next(anchors) + "}"

    return I18N.HEADINGS.sub(replace, text)


def stage(root, out_root, c4_dir, repo_url, commit):
    manifest = I18N.read(root)
    if I18N.check(root, manifest):
        raise ValueError("documentation language validation failed")
    if os.path.exists(out_root):
        shutil.rmtree(out_root)
    projects = {"en": Path(out_root), "ru": Path(out_root) / "ru"}
    for language, project in projects.items():
        out = str(project / "src")
        copy_sources(root, out, language, manifest)
        write(os.path.join(out, "index.md"), index(language))
        write(os.path.join(out, "reference/kafka-topics.md"), kafka_topics(root, language))
        write(os.path.join(out, "reference/error-codes.md"), error_codes(root, language))
        source_c4 = c4_dir if language == "en" else os.path.join(root, "docs/ru/architecture/c4")
        write(os.path.join(out, "reference/c4.md"), c4(source_c4, language))
        rest_api(root, out, language)
        diagrams = rewrite(root, out, repo_url, commit, language)
        assets = project / "src/assets"
        assets.mkdir(exist_ok=True)
        shutil.copyfile(Path(root) / "tools/docs/portal/docs-languages.js", assets / "docs-languages.js")
        pages = sum(1 for _, _, files in os.walk(out) for n in files if n.endswith(".md"))
        print(f"portal: {language}: {pages} pages, {diagrams} diagrams staged in {out}")
    for language, project in projects.items():
        other = "ru" if language == "en" else "en"
        alternates = {}
        for source in (project / "src").rglob("*.md"):
            relative = source.relative_to(project / "src")
            counterpart = projects[other] / "src" / relative
            if not counterpart.is_file():
                raise ValueError(f"missing portal counterpart: {relative}")
            html = relative.with_name("index.html") if relative.name == "README.md" else relative.with_suffix(".html")
            current_url = Path("ru") / html if language == "ru" else html
            target_url = Path("ru") / html if other == "ru" else html
            anchors = I18N.section_map(source.read_text(encoding="utf-8"), counterpart.read_text(encoding="utf-8"))
            alternates[relative.as_posix()] = {
                "url": os.path.relpath(target_url, current_url.parent), "anchors": anchors,
                "label": "Русский" if other == "ru" else "English", "language": other,
            }
        config(root, str(project), repo_url, commit, language, alternates)
    for project in projects.values():
        for source in (project / "src").rglob("*.md"):
            source.write_text(heading_ids(source.read_text(encoding="utf-8")), encoding="utf-8")
    return 0


def main(argv):
    if len(argv) == 3 and argv[1] == "finalize":
        return finalize(argv[2])
    if len(argv) == 7 and argv[1] == "stage":
        return stage(*argv[2:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
