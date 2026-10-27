#!/usr/bin/env python3
"""Verify documentation in a source copy without development-only materials.

Usage: check_release.py <repo-root> <built-portal> <portal-image> <plantuml-image>
"""

import contextlib
import importlib.util
import io
import os
from html.parser import HTMLParser
from pathlib import Path
import shutil
import subprocess
import sys
import tarfile
import tempfile
from urllib.parse import unquote, urlsplit


DEVELOPMENT_FILES = {"psim-mvp-development-plan.md", "progress.md", "psim-mvp-diagrams.drawio"}
IGNORED = {".git", ".cache", ".task", "build", "node_modules", "__pycache__"} | DEVELOPMENT_FILES


class Resources(HTMLParser):
    def __init__(self):
        super().__init__()
        self.urls = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag in {"script", "img", "iframe", "source"} and attrs.get("src"):
            self.urls.append(attrs["src"])
        if tag == "link" and set(attrs.get("rel", "").split()) & {"stylesheet", "icon", "preload", "modulepreload"}:
            self.urls.append(attrs.get("href", ""))


def check_resources(portal):
    failures = []
    for page in portal.rglob("*.html"):
        parser = Resources()
        parser.feed(page.read_text(encoding="utf-8"))
        for url in parser.urls:
            parsed = urlsplit(url)
            if parsed.scheme == "data":
                continue
            if parsed.scheme or parsed.netloc:
                failures.append(f"external runtime resource: {page.relative_to(portal)}: {url}")
                continue
            target = (portal / unquote(parsed.path).lstrip("/") if parsed.path.startswith("/")
                      else page.parent / unquote(parsed.path)).resolve()
            if not target.is_relative_to(portal.resolve()) or not target.is_file():
                failures.append(f"missing runtime resource: {page.relative_to(portal)}: {url}")
    return failures


def load_module(root, relative):
    path = root / relative
    spec = importlib.util.spec_from_file_location(path.stem, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def build_snapshot(out, portal_image, plantuml_image, finalize):
    identity = f"{os.getuid()}:{os.getgid()}"
    commands = []
    for project in (out, out / "ru"):
        commands.append(
        ["docker", "run", "--rm", "--network", "none", "-u", identity,
         "-v", f"{project / 'src/reference'}:/output", portal_image,
         "cp", "/opt/portal/redoc.standalone.js", "/opt/portal/redoc-logo.svg", "/output"])
    commands.extend([
        ["docker", "run", "--rm", "--network", "none", "-u", identity,
         "-v", f"{out}:/data", plantuml_image,
         "-charset", "UTF-8", "-failfast2", "-tsvg", "-nometadata", "/data/**/_diagrams/*.puml"],
        ["docker", "run", "--rm", "--network", "none", "-u", identity,
         "-v", f"{out}:/work", "-w", "/work", portal_image, "mkdocs", "build"],
        ["docker", "run", "--rm", "--network", "none", "-u", identity,
         "-v", f"{out}:/work", "-w", "/work/ru", portal_image, "mkdocs", "build"],
    ])
    for command in commands:
        subprocess.run(command, check=True)
    finalize(out / "site")
    return check_resources(out / "site")


def check(root, portal, portal_image, plantuml_image):
    root = Path(root).resolve()
    failures = []
    cache = root / ".cache"
    cache.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="release-docs-", dir=cache) as directory:
        snapshot = Path(directory) / "source"
        shutil.copytree(root, snapshot, ignore=shutil.ignore_patterns(*IGNORED), symlinks=True)
        pages = sorted((snapshot / "docs").rglob("*.md"))
        pages += sorted(snapshot.glob("*.md"))
        pages += sorted((snapshot / "contracts/docs").rglob("*.md"))
        pages += [p for p in snapshot.rglob("README.md") if snapshot / "docs" not in p.parents]
        pages = sorted(set(pages))
        for page in pages:
            if page.name == "AGENTS.md":
                continue
            text = page.read_text(encoding="utf-8")
            for name in DEVELOPMENT_FILES:
                if name in text:
                    failures.append(f"{page.relative_to(snapshot)} references excluded material: {name}")
        links = load_module(snapshot, "tools/docs/check_links.py")
        quality = load_module(snapshot, "tools/quality/verification.py")
        security = load_module(snapshot, "tools/security/threat_model.py")
        portal_builder = load_module(snapshot, "tools/docs/portal.py")
        with contextlib.redirect_stdout(io.StringIO()) as output:
            result = links.main(["links", *map(str, pages)])
            result |= quality.verify(snapshot, quality.load(snapshot / "docs/quality/verification.yaml"))
            result |= security.verify(snapshot, security.load(snapshot / "docs/security/threat-model.yaml"))
            result |= portal_builder.stage(str(snapshot), str(Path(directory) / "portal"),
                                           str(snapshot / "docs/architecture/c4"),
                                           "https://github.com/d8zerg/psim", "source-copy")
        print(output.getvalue(), end="")
        if result:
            failures.append("source copy failed documentation verification")
        else:
            failures.extend(build_snapshot(Path(directory) / "portal", portal_image, plantuml_image, portal_builder.finalize))
            print("portal built from the release source copy with network disabled")
    if portal:
        portal = Path(portal)
        if not (portal / "index.html").is_file():
            failures.append("built portal index is missing")
        for path in portal.rglob("*"):
            if not path.is_file():
                continue
            if path.name in DEVELOPMENT_FILES:
                failures.append(f"excluded material in portal: {path}")
            if path.suffix in {".html", ".json", ".md", ".xml"}:
                text = path.read_text(encoding="utf-8")
                for name in DEVELOPMENT_FILES:
                    if name in text:
                        failures.append(f"portal references excluded material: {path}: {name}")
        failures.extend(check_resources(portal))
        if not failures:
            archive = root / "build/portal-validation.tar"
            with tarfile.open(archive, "w") as bundle:
                bundle.add(portal, arcname="portal")
            with tempfile.TemporaryDirectory(prefix="psim-portal-archive-") as directory:
                with tarfile.open(archive) as bundle:
                    bundle.extractall(directory, filter="data")
                extracted = Path(directory) / "portal"
                failures.extend(check_resources(extracted))
                original = {p.relative_to(portal): p.read_bytes() for p in portal.rglob("*") if p.is_file()}
                restored = {p.relative_to(extracted): p.read_bytes() for p in extracted.rglob("*") if p.is_file()}
                if original != restored:
                    failures.append("documentation archive differs from the built portal")
            print(f"documentation validation archive: {archive}")
    for failure in failures:
        print("FAIL", failure)
    print(f"release documentation: {len(failures)} problems")
    return bool(failures)


if __name__ == "__main__":
    sys.exit(check(*sys.argv[1:]))
