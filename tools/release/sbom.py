#!/usr/bin/env python3
"""CycloneDX 1.6 SBOMs of PSIM release artifacts and the license policy (step 1.3, ADR-034, FF-14).

  app <conan-graph.json> <libcxx-version> <version> <commit> <out.json>
      components linked into PSIM binaries: Conan host dependencies (test-only ones excluded) and
      the statically linked libc++; the dependency graph comes from Conan
  artifact <app.json> <file> <deb|image> <name> <version> <out.json> [<trivy-image-sbom.json>]
      SBOM of one release artifact: the file with its SHA-256 as metadata.component, the application
      components and, for an image, the operating system packages found by Trivy
  licenses <licenses.yaml> <sbom.json>...
      every application component has a license allowed by the policy

Timestamps come from SOURCE_DATE_EPOCH and serial numbers from the content, so SBOMs are reproducible.
"""

import datetime
import hashlib
import json
import os
import sys
import uuid

import yaml

SPEC = "1.6"
PSIM_REF = "psim"


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save(doc, path):
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2, sort_keys=True, ensure_ascii=False)
        f.write("\n")


def license_entry(value):
    if isinstance(value, (list, tuple)):
        value = " AND ".join(value) if len(value) > 1 else value[0]
    if not value:
        return []
    if any(op in value for op in (" WITH ", " AND ", " OR ")):
        return [{"expression": value}]
    return [{"license": {"id": value}}]


def timestamp():
    epoch = int(os.environ.get("SOURCE_DATE_EPOCH", "0"))
    return datetime.datetime.fromtimestamp(epoch, datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")


def bom(component, components, dependencies):
    body = json.dumps([component, components, dependencies], sort_keys=True)
    return {
        "bomFormat": "CycloneDX",
        "specVersion": SPEC,
        "serialNumber": f"urn:uuid:{uuid.uuid5(uuid.NAMESPACE_URL, body)}",
        "version": 1,
        "metadata": {
            "timestamp": timestamp(),
            "tools": {"components": [{"type": "application", "name": "psim-sbom", "version": "1"}]},
            "component": component,
        },
        "components": components,
        "dependencies": dependencies,
    }


def app_sbom(graph_path, libcxx_version, version, commit):
    nodes = load(graph_path)["graph"]["nodes"]
    refs, components = {}, {}
    for node_id, node in nodes.items():
        if node_id == "0" or node.get("context") != "host" or node.get("test"):
            continue
        purl = f"pkg:conan/{node['name']}@{node['version']}"
        refs[node_id] = purl
        component = {"type": "library", "bom-ref": purl, "name": node["name"], "version": str(node["version"]),
                     "purl": purl, "licenses": license_entry(node.get("license"))}
        if node.get("homepage"):
            component["externalReferences"] = [{"type": "website", "url": node["homepage"]}]
        if node.get("description"):
            component["description"] = " ".join(node["description"].split())
        components[purl] = component

    libcxx = f"pkg:generic/llvm-libcxx@{libcxx_version}"
    components[libcxx] = {
        "type": "library", "bom-ref": libcxx, "name": "llvm-libcxx", "version": libcxx_version, "purl": libcxx,
        "description": "LLVM libc++ and libc++abi, linked statically into PSIM executables (ADR-032)",
        "licenses": license_entry("Apache-2.0 WITH LLVM-exception"),
        "externalReferences": [{"type": "website", "url": "https://libcxx.llvm.org"}],
    }

    def host_deps(node):
        return sorted({refs[d] for d, edge in node.get("dependencies", {}).items()
                       if d in refs and edge.get("direct") and not edge.get("build") and not edge.get("test")})

    dependencies = [{"ref": PSIM_REF, "dependsOn": sorted(set(host_deps(nodes["0"])) | {libcxx})},
                    {"ref": libcxx, "dependsOn": []}]
    dependencies += [{"ref": refs[i], "dependsOn": host_deps(nodes[i])} for i in sorted(refs, key=refs.get)]
    psim = {"type": "application", "bom-ref": PSIM_REF, "name": "psim", "version": version,
            "properties": [{"name": "psim:git-commit", "value": commit}]}
    return bom(psim, sorted(components.values(), key=lambda c: c["bom-ref"]), dependencies)


def artifact_sbom(app, path, kind, name, version, os_sbom=None):
    with open(path, "rb") as f:
        digest = hashlib.sha256(f.read()).hexdigest()
    if kind == "deb":
        purl = f"pkg:deb/psim/{name}@{version}?arch=amd64"
        component = {"type": "application", "bom-ref": purl, "purl": purl}
    else:
        purl = f"pkg:oci/{name}?tag={version}"
        component = {"type": "container", "bom-ref": purl, "purl": purl}
    component.update({"name": name, "version": version, "hashes": [{"alg": "SHA-256", "content": digest}],
                      "properties": [{"name": "psim:file", "value": os.path.basename(path)}]})

    components = [app["metadata"]["component"]] + app["components"]
    dependencies = [{"ref": component["bom-ref"], "dependsOn": [PSIM_REF]}] + app["dependencies"]
    if os_sbom:
        trivy = load(os_sbom)
        known = {c["bom-ref"] for c in components}
        os_components = [c for c in trivy.get("components", []) if c.get("bom-ref") not in known]
        components += os_components
        dependencies[0]["dependsOn"] += sorted(c["bom-ref"] for c in os_components if c.get("type") == "operating-system")
        dependencies += [d for d in trivy.get("dependencies", []) if d["ref"] in {c["bom-ref"] for c in os_components}]
    return bom(component, components, dependencies)


def check_licenses(policy_path, sbom_paths):
    with open(policy_path, encoding="utf-8") as f:
        allowed = set(yaml.safe_load(f)["allowed"])
    problems = []
    for path in sbom_paths:
        doc = load(path)
        for c in doc.get("components", []):
            ref = c.get("bom-ref", "")
            if not (ref.startswith("pkg:conan/") or ref.startswith("pkg:generic/llvm-")):
                continue  # PSIM itself and operating system packages of images
            names = [e.get("expression") or e.get("license", {}).get("id") or e.get("license", {}).get("name")
                     for e in c.get("licenses", [])]
            if not names:
                problems.append(f"{os.path.basename(path)}: {ref}: no license")
            for n in names:
                if n not in allowed:
                    problems.append(f"{os.path.basename(path)}: {ref}: license {n!r} is not allowed")
    for p in sorted(set(problems)):
        print(p)
    print(f"licenses: {len(sbom_paths)} SBOMs, {len(set(problems))} problems")
    return 1 if problems else 0


def main(argv):
    cmd = argv[1] if len(argv) > 1 else ""
    if cmd == "app" and len(argv) == 7:
        save(app_sbom(*argv[2:6]), argv[6])
        return 0
    if cmd == "artifact" and len(argv) in (8, 9):
        app = load(argv[2])
        save(artifact_sbom(app, argv[3], argv[4], argv[5], argv[6], argv[8] if len(argv) == 9 else None), argv[7])
        return 0
    if cmd == "licenses" and len(argv) >= 4:
        return check_licenses(argv[2], argv[3:])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
