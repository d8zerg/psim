#!/usr/bin/env python3
"""Create a service from the template (step 2.1, ADR-039).

  new_service.py <repo-root> <name> <contexts> [summary]

  name      kebab-case service name, for example incident-service
  contexts  comma-separated domain contexts the service may use (ADR-001, tools/arch/architecture.yaml)

Copies tools/service-template to services/<name> (executable psim-<name>, packaged), renames its
identifiers and metrics, and registers the service: services/CMakeLists.txt, the module rules of
tools/arch/architecture.yaml, the container image deploy/images/psim-<name> and IMAGES in the Taskfile.
"""

import os
import re
import shutil
import sys

import yaml

TEMPLATE = "tools/service-template"
COPIED = ["CMakeLists.txt", "src", "tests", "config"]
NAME = re.compile(r"^[a-z][a-z0-9]*(-[a-z0-9]+)*$")

DOCKERFILE = """# psim-{name} container image (ADR-034, SR-24): distroless base with glibc only, non-root user.
# Built by `task release:images`; the base digest is pinned in the root Taskfile.
ARG BASE=gcr.io/distroless/base-nossl-debian12:nonroot
FROM ${{BASE}}
COPY psim-{name} /usr/bin/psim-{name}
USER 65532:65532
EXPOSE 9100
ENTRYPOINT ["/usr/bin/psim-{name}"]
CMD ["--config", "/etc/psim/{name}.yaml"]
"""


def render(text, name, summary):
    snake = name.replace("-", "_")
    text = text.replace("psim_template_", f"psim_{snake}_")
    text = text.replace("service_template", snake).replace("service-template", name)
    text = text.replace('SUMMARY "PSIM service template"\n  NO_PACKAGE)', f'SUMMARY "{summary}")')
    text = re.sub(r"^# Template of a PSIM service.*?\n(?=add_library)",
                  f"# psim-{name}: {summary} (created from tools/service-template, ADR-039).\n", text, flags=re.S | re.M)
    return text


def create(root, name, contexts, summary):
    if not NAME.match(name):
        raise SystemExit(f"service name must be kebab-case: {name!r}")
    target = os.path.join(root, "services", name)
    if os.path.exists(target):
        raise SystemExit(f"{target} already exists")
    rules_path = os.path.join(root, "tools/arch/architecture.yaml")
    with open(rules_path, encoding="utf-8") as f:
        rules_text = f.read()
    known = set()
    for module in yaml.safe_load(rules_text)["services"].values():
        known.update(module)
    known.update({"catalog", "ingest", "processing", "incident", "response", "access", "audit"})
    unknown = [c for c in contexts if c not in known]
    if unknown:
        raise SystemExit(f"unknown domain contexts {unknown}; known: {sorted(known)}")

    source = os.path.join(root, TEMPLATE)
    for entry in COPIED:
        src = os.path.join(source, entry)
        dst = os.path.join(target, entry)
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            os.makedirs(target, exist_ok=True)
            shutil.copy2(src, dst)
    for dirpath, _, filenames in os.walk(target):
        for filename in filenames:
            path = os.path.join(dirpath, filename)
            with open(path, encoding="utf-8") as f:
                text = f.read()
            with open(path, "w", encoding="utf-8") as f:
                f.write(render(text, name, summary))

    with open(os.path.join(root, "services/CMakeLists.txt"), "a", encoding="utf-8") as f:
        f.write(f"add_subdirectory({name})\n")
    if f"\n  {name}:" not in rules_text:
        with open(rules_path, "a", encoding="utf-8") as f:
            f.write(f"  {name}: [{', '.join(contexts)}]\n")
    # release:images builds deploy/images/<item>/Dockerfile around the executable named <item>.
    image = os.path.join(root, "deploy/images", f"psim-{name}")
    os.makedirs(image, exist_ok=True)
    with open(os.path.join(image, "Dockerfile"), "w", encoding="utf-8") as f:
        f.write(DOCKERFILE.format(name=name))
    taskfile = os.path.join(root, "Taskfile.yml")
    with open(taskfile, encoding="utf-8") as f:
        text = f.read()
    text, count = re.subn(r"^(  IMAGES: )(.*)$", lambda m: f"{m.group(1)}{m.group(2)} psim-{name}", text, count=1, flags=re.M)
    if count:
        with open(taskfile, "w", encoding="utf-8") as f:
            f.write(text)
    print(f"created services/{name} (psim-{name}), contexts {contexts}; next: task cpp:test, task arch:deps")
    return 0


def main(argv):
    if len(argv) not in (4, 5):
        print(__doc__, file=sys.stderr)
        return 2
    contexts = [c.strip() for c in argv[3].split(",") if c.strip()]
    summary = argv[4] if len(argv) == 5 else f"PSIM {argv[2]}"
    return create(argv[1], argv[2], contexts, summary)


if __name__ == "__main__":
    sys.exit(main(sys.argv))
