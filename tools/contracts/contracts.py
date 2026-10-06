#!/usr/bin/env python3
"""Contract tooling: AsyncAPI generation and cross-artifact consistency checks.

Commands:
  asyncapi <topics.yaml> <out.yaml>
      Generate the AsyncAPI 3 description of Kafka topics from the topic registry.
  verify <repo-root> <buf-image.json>
      Check consistency between proto descriptors (buf image in JSON), the topic registry,
      the taxonomy, the error catalog, the OpenAPI overlay and the documentation.

Exit code 1 and a list of problems when a check fails.
"""

import glob
import os
import re
import sys

import yaml

GRPC_CODES = {
    "CANCELLED", "UNKNOWN", "INVALID_ARGUMENT", "DEADLINE_EXCEEDED", "NOT_FOUND", "ALREADY_EXISTS",
    "PERMISSION_DENIED", "RESOURCE_EXHAUSTED", "FAILED_PRECONDITION", "ABORTED", "OUT_OF_RANGE",
    "UNIMPLEMENTED", "INTERNAL", "UNAVAILABLE", "DATA_LOSS", "UNAUTHENTICATED",
}
HTTP_CODES = {400, 401, 403, 404, 409, 412, 422, 428, 429, 500, 503, 504}
USED_IN = {"rest", "connector", "dlq", "command_result", "notification", "realtime"}
SERVICES = {
    "connector-gateway", "resource-catalog", "normalizer", "event-history", "correlation-engine",
    "incident-service", "response-engine", "command-service", "notification-service",
    "projection-service", "audit-service", "api-gateway", "all-services",
}
TOPIC_NAME = re.compile(r"^psim(\.[a-z]+)+\.v[0-9]+$")
ISO_DURATION = re.compile(r"^P(\d+D)?(T(\d+H)?(\d+M)?)?$")


def load_yaml(path):
    with open(path, encoding="utf-8") as f:
        return yaml.safe_load(f)


# ---------------------------------------------------------------- AsyncAPI

def duration_ms(value):
    if value == "infinite":
        return -1
    m = re.match(r"^P(?:(\d+)D)?(?:T(?:(\d+)H)?(?:(\d+)M)?)?$", value)
    days, hours, minutes = (int(x) if x else 0 for x in m.groups())
    return ((days * 24 + hours) * 60 + minutes) * 60_000


def camel(name):
    parts = re.split(r"[.\-_:]", name)
    return parts[0] + "".join(p[:1].upper() + p[1:] for p in parts[1:])


def topic_partitions(topic, by_name, profile):
    p = topic["partitions"]
    if "copartitioned_with" in p:
        return topic_partitions(by_name[p["copartitioned_with"]], by_name, profile)
    return p[profile]


def generate_asyncapi(reg):
    by_name = {t["name"]: t for t in reg["topics"]}
    channels, operations, messages = {}, {}, {}
    entries = list(reg["topics"])
    for stage in reg["dlq"]["stages"]:
        entries.append({
            "name": f"psim.dlq.{stage}.v1",
            "title": f"Dead letters of stage {stage}",
            "record": None,
            "key": "original key",
            "partitions": reg["dlq"]["partitions"],
            "retention": reg["dlq"]["retention"],
            "cleanup": "delete",
            "semantics": "ALO",
            "producers": [],
            "consumers": [],
        })
    for t in entries:
        cid = camel(t["name"].removeprefix("psim."))
        if t["record"]:
            mid = t["record"].rsplit(".", 1)[1]
            messages[mid] = {
                "name": mid,
                "title": t["record"],
                "contentType": "application/x-protobuf",
                "summary": f"Protobuf message {t['record']} in Confluent wire format (ADR-004).",
                "payload": {
                    "type": "object",
                    "description": f"See contracts/proto: {t['record']}.",
                    "x-psim-proto": t["record"],
                },
                "bindings": {"kafka": {
                    "key": {"type": "string", "description": t["key"]},
                    "schemaIdLocation": "payload",
                    "schemaIdPayloadEncoding": "confluent",
                    "bindingVersion": "0.5.0",
                }},
            }
        else:
            mid = "DeadLetter"
            messages[mid] = {
                "name": mid,
                "contentType": "application/octet-stream",
                "summary": "Original message bytes; error in headers " + ", ".join(reg["dlq"]["headers"]) + ".",
                "payload": {"type": "string", "format": "binary"},
            }
        retention = duration_ms(t["retention"])
        channels[cid] = {
            "address": t["name"],
            "title": t["title"],
            "description": f"Key: {t['key']}. Semantics: {t['semantics']}."
                           + (" Internal topic." if t.get("internal") else ""),
            "messages": {mid: {"$ref": f"#/components/messages/{mid}"}},
            "bindings": {"kafka": {
                "topic": t["name"],
                "partitions": topic_partitions(t, by_name, "cluster"),
                "replicas": reg["defaults"]["replication_factor"]["cluster"],
                "topicConfiguration": {
                    "cleanup.policy": [t["cleanup"]],
                    "retention.ms": retention,
                },
                "bindingVersion": "0.5.0",
            }},
            "x-psim-partitions": {p: topic_partitions(t, by_name, p) for p in ("cluster", "single")},
            "x-psim-semantics": t["semantics"],
        }
        for action, services in (("send", t["producers"]), ("receive", t["consumers"])):
            for svc in services:
                operations[camel(f"{svc}-{action}-{cid}")] = {
                    "action": action,
                    "channel": {"$ref": f"#/channels/{cid}"},
                    "messages": [{"$ref": f"#/channels/{cid}/messages/{mid}"}],
                    "x-psim-service": svc,
                }
    return {
        "asyncapi": "3.1.0",
        "info": {
            "title": "PSIM Platform Kafka topics",
            "version": "1.0.0",
            "description": "Generated from contracts/topics/topics.yaml by tools/contracts/contracts.py. "
                           "Every message value is a topic record with an Envelope and a oneof payload "
                           "(ADR-004, ADR-005).",
            "license": {"name": "MIT", "url": "https://opensource.org/licenses/MIT"},
        },
        "defaultContentType": "application/x-protobuf",
        "servers": {"kafka": {
            "host": "kafka:9093",
            "protocol": "kafka-secure",
            "description": "Kafka (KRaft), TLS. Schema Registry: Apicurio, Confluent-compatible API (ADR-003).",
            "bindings": {"kafka": {
                "schemaRegistryUrl": "https://schema-registry:8081/apis/ccompat/v7",
                "schemaRegistryVendor": "apicurio",
                "bindingVersion": "0.5.0",
            }},
        }},
        "channels": channels,
        "operations": operations,
        "components": {"messages": messages},
    }


# ---------------------------------------------------------------- descriptors

def index_enum_values(image):
    values = set()

    def walk(mtypes):
        for m in mtypes or []:
            for e in m.get("enumType", []):
                values.update(v["name"] for v in e.get("value", []))
            walk(m.get("nestedType"))

    for f in image["file"]:
        for e in f.get("enumType", []):
            values.update(v["name"] for v in e.get("value", []))
        walk(f.get("messageType"))
    return values


def index_messages(image):
    msgs = {}

    def walk(prefix, mtypes):
        for m in mtypes or []:
            full = f"{prefix}.{m['name']}"
            msgs[full] = m
            walk(full, m.get("nestedType"))

    for f in image["file"]:
        if f.get("package", "").startswith("psim."):
            walk(f["package"], f.get("messageType"))
    return msgs


# ---------------------------------------------------------------- checks

def check_topics(reg, msgs, root, problems):
    names = set()
    for t in reg["topics"]:
        n = t["name"]
        if not TOPIC_NAME.match(n):
            problems.append(f"topics: bad name {n}")
        names.add(n)
        rec = msgs.get(t["record"])
        if rec is None:
            problems.append(f"topics: {n}: record {t['record']} not found in proto")
        else:
            fields = {f["number"]: f for f in rec.get("field", [])}
            env = fields.get(1)
            if not env or env["name"] != "envelope" or env.get("typeName") != ".psim.common.v1.Envelope":
                problems.append(f"topics: {t['record']}: field 1 must be psim.common.v1.Envelope envelope")
            if [o["name"] for o in rec.get("oneofDecl", [])] != ["payload"]:
                problems.append(f"topics: {t['record']}: must have exactly one oneof named payload")
        if not (t["retention"] == "infinite" or ISO_DURATION.match(t["retention"])):
            problems.append(f"topics: {n}: bad retention {t['retention']}")
        for svc in t["producers"] + t["consumers"]:
            if svc not in SERVICES:
                problems.append(f"topics: {n}: unknown service {svc}")
        cw = t["partitions"].get("copartitioned_with")
        if cw and cw not in {x["name"] for x in reg["topics"]}:
            problems.append(f"topics: {n}: copartitioned_with unknown topic {cw}")
    records = {t["record"] for t in reg["topics"]}
    for full in msgs:
        pkg = full.rsplit(".", 1)[0]
        if full.endswith("Record") and full.count(".") == 3 and not pkg.startswith(("psim.api.", "psim.realtime.")):
            if full not in records:
                problems.append(f"topics: record {full} is not bound to any topic")

    # data-flows.md summary table must list the same topics and keys.
    md = open(os.path.join(root, "docs/architecture/data-flows.md"), encoding="utf-8").read()
    table = md[md.index("## 4. Сводная таблица"):]
    doc = dict(re.findall(r"^\| `(psim\.[a-z.]+\.v1)` \| `([^`]+)` \|", table, re.M))
    for t in reg["topics"]:
        if t["name"] not in doc:
            problems.append(f"data-flows.md: topic {t['name']} missing in summary table")
        elif doc[t["name"]] != t["key"]:
            problems.append(f"data-flows.md: topic {t['name']} key {doc[t['name']]} != registry {t['key']}")
    for n in doc:
        if n not in names:
            problems.append(f"data-flows.md: topic {n} not in registry")


def check_taxonomy(tax, root, problems):
    classes, sevs = set(tax["classes"]), set(tax["severities"])
    types = {}
    for d in tax["domains"]:
        for t in d["types"]:
            c = t["code"]
            if c in types:
                problems.append(f"taxonomy: duplicate {c}")
            types[c] = t
            if not re.match(r"^[a-z0-9_]+(\.[a-z0-9_]+){1,2}$", c) or c.split(".")[0] != d["code"]:
                problems.append(f"taxonomy: bad code {c} in domain {d['code']}")
            if t["class"] not in classes or t["severity"] not in sevs:
                problems.append(f"taxonomy: {c}: bad class or severity")
            for lang in ("ru", "en"):
                if not t.get("name", {}).get(lang):
                    problems.append(f"taxonomy: {c}: missing name.{lang}")
    for c, t in types.items():
        r = t.get("restore")
        if r and (r not in types or types[r]["class"] != "restore"):
            problems.append(f"taxonomy: {c}: restore {r} is not a restore type")
    md = open(os.path.join(root, "docs/domain/event-taxonomy.md"), encoding="utf-8").read()
    for code, cls, sev in re.findall(r"^\| `([a-z0-9_.]+)` \| [^|]+ \| (\w+) \| (\w+) \|", md, re.M):
        t = types.get(code)
        if t is None:
            problems.append(f"event-taxonomy.md: {code} not in taxonomy.yaml")
        elif (t["class"], t["severity"]) != (cls, sev):
            problems.append(f"event-taxonomy.md: {code} differs from taxonomy.yaml")
    return types


def check_errors(errs, enum_values, root, problems):
    domains = set(errs["domains"])
    codes = {}
    for e in errs["codes"]:
        c = e["code"]
        if c in codes:
            problems.append(f"errors: duplicate {c}")
        codes[c] = e
        if not re.match(r"^[A-Z]+(_[A-Z0-9]+)+$", c) or c.split("_")[0] not in domains:
            problems.append(f"errors: bad code {c}")
        if e["http"] is not None and e["http"] not in HTTP_CODES:
            problems.append(f"errors: {c}: unexpected http {e['http']}")
        if e["grpc"] not in GRPC_CODES:
            problems.append(f"errors: {c}: bad grpc {e['grpc']}")
        if not set(e["used_in"]) <= USED_IN:
            problems.append(f"errors: {c}: bad used_in {e['used_in']}")
        if "rest" in e["used_in"] and e["http"] is None and c != "CATALOG_IMPORT_ROW_INVALID":
            problems.append(f"errors: {c}: used in rest without http status")
        if not e.get("title") or not e.get("ru"):
            problems.append(f"errors: {c}: missing title or ru")
    # Every error-like code mentioned in docs and proto comments must exist in the catalog.
    files = glob.glob(os.path.join(root, "docs/**/*.md"), recursive=True)
    files += glob.glob(os.path.join(root, "contracts/proto/**/*.proto"), recursive=True)
    for path in files:
        text = open(path, encoding="utf-8").read()
        for token in set(re.findall(r"`?\b([A-Z][A-Z0-9]*(?:_[A-Z0-9]+)+)\b`?", text)):
            if token in codes or token in enum_values:
                continue
            if token.split("_")[0] in domains or any(c.endswith("_" + token) for c in codes):
                rel = os.path.relpath(path, root)
                problems.append(f"{rel}: error code {token} is not in errors.yaml")
    return codes


def check_problem_schema(msgs, root, problems):
    overlay = load_yaml(os.path.join(root, "contracts/openapi/overlay.yaml"))
    props = None
    for a in overlay["actions"]:
        if a["target"] == "$.components":
            props = set(a["update"]["schemas"]["Problem"]["properties"])
    proto = {f["jsonName"] for f in msgs["psim.api.v1.Problem"]["field"]}
    if props != proto:
        problems.append(f"overlay Problem schema {sorted(props or [])} != psim.api.v1.Problem {sorted(proto)}")


def verify(root, image_path):
    import json
    with open(image_path, encoding="utf-8") as f:
        image = json.load(f)
    msgs = index_messages(image)
    problems = []
    check_topics(load_yaml(os.path.join(root, "contracts/topics/topics.yaml")), msgs, root, problems)
    types = check_taxonomy(load_yaml(os.path.join(root, "contracts/taxonomy/v1/taxonomy.yaml")), root, problems)
    codes = check_errors(load_yaml(os.path.join(root, "contracts/errors/errors.yaml")), index_enum_values(image), root, problems)
    check_problem_schema(msgs, root, problems)
    for p in problems:
        print("FAIL", p)
    print(f"verify: {len(msgs)} messages, {len(types)} taxonomy types, {len(codes)} error codes, "
          f"{len(problems)} problems")
    return 1 if problems else 0


def main(argv):
    if len(argv) == 4 and argv[1] == "asyncapi":
        doc = generate_asyncapi(load_yaml(argv[2]))
        with open(argv[3], "w", encoding="utf-8") as f:
            f.write("# Generated from contracts/topics/topics.yaml. Do not edit.\n")
            yaml.safe_dump(doc, f, sort_keys=False, allow_unicode=True, width=120)
        return 0
    if len(argv) == 4 and argv[1] == "verify":
        return verify(argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
