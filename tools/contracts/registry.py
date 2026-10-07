#!/usr/bin/env python3
"""Schemas of the Kafka topics in the Schema Registry (ADR-003, ADR-004, step 2.3).

  register <url> [<root>]          register the value schema of every topic of the registry
                                   (contracts/topics/topics.yaml under <root>, default: the repository)
                                   with its imports as references and the compatibility rules
  compat <url> <baseline-root>     FF-04: register the baseline schemas, then the current ones, under
                                   a temporary subject prefix; the registry rejects incompatible
                                   changes; the temporary subjects are deleted afterwards

<url> is the Confluent-compatible API, for example http://registry:8080/apis/ccompat/v7.
Subjects: "<topic>-value" (TopicNameStrategy) with BACKWARD_TRANSITIVE; every imported file is its
own subject named by its import path with FULL_TRANSITIVE. google/protobuf/* are built into the
registry. Only the installer and CI register schemas; services read them (ADR-003).
"""

import json
import os
import random
import re
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import contracts  # noqa: E402

PROTO_DIRS = ("contracts/proto", "contracts/third_party/proto")
BUILTIN = "google/protobuf/"
TOPIC_RULE = "BACKWARD_TRANSITIVE"
SHARED_RULE = "FULL_TRANSITIVE"
IMPORT = re.compile(r'^import\s+(?:public\s+)?"([^"]+)";', re.M)
PACKAGE = re.compile(r"^package\s+([\w.]+);", re.M)
MESSAGE = re.compile(r"^message\s+(\w+)\s*\{", re.M)


class Incompatible(Exception):
    pass


class Registry:
    def __init__(self, url):
        self.url = url.rstrip("/")

    def call(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.url + path, data=data, method=method,
                                         headers={"Content-Type": "application/vnd.schemaregistry.v1+json"})
        try:
            with urllib.request.urlopen(request, timeout=30) as response:
                text = response.read().decode()
                return response.status, json.loads(text) if text else None
        except urllib.error.HTTPError as e:
            text = e.read().decode()
            try:
                return e.code, json.loads(text)
            except json.JSONDecodeError:
                return e.code, {"message": text}

    @staticmethod
    def subject_path(subject):
        return "/subjects/" + urllib.parse.quote(subject, safe="")

    def configure(self, subject, rule):
        status, body = self.call("PUT", "/config/" + urllib.parse.quote(subject, safe=""), {"compatibility": rule})
        if status != 200:
            raise RuntimeError(f"{subject}: set compatibility: {status} {body}")

    def register(self, subject, schema, references):
        payload = {"schemaType": "PROTOBUF", "schema": schema, "references": references}
        status, body = self.call("POST", self.subject_path(subject) + "/versions", payload)
        if status == 409:
            raise Incompatible(f"{subject}: {body.get('message', body)}")
        if status != 200:
            raise RuntimeError(f"{subject}: register: {status} {body}")
        status, found = self.call("POST", self.subject_path(subject), payload)
        if status != 200:
            raise RuntimeError(f"{subject}: look up the registered version: {status} {found}")
        return body["id"], found["version"]

    def delete(self, subject):
        self.call("DELETE", self.subject_path(subject))
        self.call("DELETE", self.subject_path(subject) + "?permanent=true")


class Schemas:
    """Proto files of a contracts tree: where each message is defined and what each file imports."""

    def __init__(self, root):
        self.root = root
        self.files = {}  # import path -> text
        for proto_dir in PROTO_DIRS:
            base = os.path.join(root, proto_dir)
            for dirpath, _, filenames in os.walk(base):
                for name in filenames:
                    if name.endswith(".proto"):
                        path = os.path.join(dirpath, name)
                        self.files[os.path.relpath(path, base)] = open(path, encoding="utf-8").read()
        self.messages = {}
        for path, text in self.files.items():
            package = PACKAGE.search(text)
            for message in MESSAGE.findall(text):
                self.messages[f"{package.group(1) if package else ''}.{message}".lstrip(".")] = path

    def imports(self, path):
        return [i for i in IMPORT.findall(self.files[path]) if not i.startswith(BUILTIN)]


def register_tree(registry, root, prefix=""):
    """Register the value schemas of all topics of `root`; return the subjects touched."""
    schemas = Schemas(root)
    topics = contracts.load_yaml(os.path.join(root, "contracts", "topics", "topics.yaml"))["topics"]
    versions = {}  # import path -> registered version (this run)
    subjects = []

    def register_file(path, subject, rule):
        references = []
        for imported in schemas.imports(path):
            if imported not in schemas.files:
                raise RuntimeError(f"{path}: import {imported} is not in the contracts")
            if imported not in versions:
                versions[imported] = register_file(imported, prefix + imported, SHARED_RULE)
            references.append({"name": imported, "subject": prefix + imported, "version": versions[imported]})
        registry.configure(subject, rule)
        subjects.append(subject)
        _, version = registry.register(subject, schemas.files[path], references)
        return version

    for topic in topics:
        path = schemas.messages.get(topic["record"])
        if path is None:
            raise RuntimeError(f"{topic['name']}: record {topic['record']} is not defined in the contracts")
        register_file(path, f"{prefix}{topic['name']}-value", TOPIC_RULE)
    return len(topics), sorted(set(subjects))


def register(url, root):
    topics, subjects = register_tree(Registry(url), root)
    print(f"registry: {topics} topic schemas, {len(subjects)} subjects registered at {url}")
    return 0


def compat(url, baseline):
    registry = Registry(url)
    prefix = f"ff04-{random.getrandbits(32):08x}."
    touched = set()
    try:
        _, subjects = register_tree(registry, baseline, prefix)
        touched.update(subjects)
        topics, subjects = register_tree(registry, ROOT, prefix)
        touched.update(subjects)
        print(f"FF-04: {topics} topic schemas are compatible with the baseline "
              f"({TOPIC_RULE} for topics, {SHARED_RULE} for shared files)")
        return 0
    except Incompatible as e:
        print(f"FF-04: incompatible schema change: {e}", file=sys.stderr)
        return 1
    finally:
        cleanup(registry, touched)


def cleanup(registry, subjects):
    """Delete temporary subjects; a referenced one goes only after its referrers, hence the passes."""
    left = set(subjects)
    for _ in range(10):
        for subject in sorted(left):
            registry.delete(subject)
            if registry.call("GET", registry.subject_path(subject) + "/versions")[0] == 404:
                left.discard(subject)
        if not left:
            return
    print(f"registry: temporary subjects left: {', '.join(sorted(left))}", file=sys.stderr)


def main(argv):
    if len(argv) in (3, 4) and argv[1] == "register":
        return register(argv[2], argv[3] if len(argv) == 4 else ROOT)
    if len(argv) == 4 and argv[1] == "compat":
        return compat(argv[2], argv[3])
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
