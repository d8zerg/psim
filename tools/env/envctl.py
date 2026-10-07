#!/usr/bin/env python3
"""Local environment control: readiness, Kafka topics, smoke tests (step 1.4, ADR-035).

  wait <project>                    wait until every component answers (images without a shell
                                    have no Docker healthcheck, so readiness is probed over HTTP)
  topics <project> <single|cluster> create the topics of contracts/topics/topics.yaml
  test <project> <brokers>          smoke tests of every component and the paths between them
  observability <project> <build-dir> <runtime-image>
                                    FF-06: every service executable of the build (psim-*) runs in the
                                    environment and exports health, metrics, JSON logs and traces, and
                                    drains on SIGTERM

Everything runs inside the compose network <project>_default: CLIs through `docker compose exec`,
HTTP through a probe container with curl. No published ports are needed, so the same tests run
against the interactive environment and the CI one.
"""

import json
import os
import random
import subprocess
import sys
import time
import urllib.parse

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
COMPOSE_DIR = os.path.join(ROOT, "deploy", "local")
sys.path.insert(0, os.path.join(ROOT, "tools", "contracts"))
import contracts  # noqa: E402

PROBE_IMAGE = "curlimages/curl:8.17.0@sha256:935d9100e9ba842cdb060de42472c7ca90cfe9a7c96e4dacb55e79e560b3ff40"
KAFKA = "/opt/kafka/bin"
READY = {
    "loki": "http://loki:3100/ready",
    "tempo": "http://tempo:3200/ready",
    "otel-collector": "http://otel-collector:13133/",
    "toxiproxy": "http://toxiproxy:8474/version",
    "registry": "http://registry:9000/health/ready",
    "keycloak": "http://keycloak:9000/health/ready",
    "grafana": "http://grafana:3000/api/health",
    "prometheus": "http://prometheus:9090/-/ready",
}


class Env:
    def __init__(self, project):
        self.project = project
        self.probe = f"{project}-probe"

    def compose(self, *args, check=True, stdin=None):
        return subprocess.run(["docker", "compose", "-p", self.project, *args], cwd=COMPOSE_DIR,
                              input=stdin, capture_output=True, text=True, check=check)

    def exec(self, service, *cmd, stdin=None, check=True):
        return self.compose("exec", "-T", service, *cmd, stdin=stdin, check=check).stdout

    def start_probe(self):
        subprocess.run(["docker", "rm", "-f", self.probe], capture_output=True)
        subprocess.run(["docker", "run", "-d", "--name", self.probe, "--network", f"{self.project}_default",
                        "--entrypoint", "sleep", PROBE_IMAGE, "3600"], check=True, capture_output=True)

    def stop_probe(self):
        subprocess.run(["docker", "rm", "-f", self.probe], capture_output=True)

    def http(self, method, url, body=None, headers=(), user=None, form=None):
        """Return (status, body) of an HTTP request made from the compose network."""
        cmd = ["docker", "exec", "-i", self.probe, "curl", "-sS", "-m", "10", "-X", method, "-w", "\n%{http_code}"]
        for h in headers:
            cmd += ["-H", h]
        if user:
            cmd += ["-u", user]
        data = None
        if form is not None:
            for k, v in form.items():
                cmd += ["--data-urlencode", f"{k}={v}"]
        elif body is not None:
            data = body if isinstance(body, str) else json.dumps(body)
            cmd += ["-H", "Content-Type: application/json", "--data-binary", "@-"]
        result = subprocess.run(cmd + [url], input=data, capture_output=True, text=True)
        text, _, code = result.stdout.rpartition("\n")
        return (int(code) if code.isdigit() else 0), text

    def http_json(self, method, url, **kwargs):
        status, text = self.http(method, url, **kwargs)
        try:
            return status, json.loads(text) if text else None
        except json.JSONDecodeError:
            return status, None


def until(predicate, timeout, interval=1.0):
    deadline = time.time() + timeout
    while time.time() < deadline:
        result = predicate()
        if result:
            return result
        time.sleep(interval)
    return None


# ------------------------------------------------------------------------------------------ wait

def wait(env, timeout=240):
    env.start_probe()
    try:
        pending = dict(READY)
        deadline = time.time() + timeout
        while pending and time.time() < deadline:
            for name, url in list(pending.items()):
                if env.http("GET", url)[0] == 200:
                    del pending[name]
            if pending:
                time.sleep(2)
        if pending:
            print(f"env: not ready after {timeout}s: {', '.join(sorted(pending))}", file=sys.stderr)
            return 1
        print("env: all components ready")
        return 0
    finally:
        env.stop_probe()


# ---------------------------------------------------------------------------------------- topics

def topic_specs(profile):
    reg = contracts.load_yaml(os.path.join(ROOT, "contracts", "topics", "topics.yaml"))
    by_name = {t["name"]: t for t in reg["topics"]}
    rf = reg["defaults"]["replication_factor"][profile]
    isr = reg["defaults"]["min_insync_replicas"][profile]
    specs = []
    for t in reg["topics"]:
        specs.append((t["name"], contracts.topic_partitions(t, by_name, profile), rf, {
            "cleanup.policy": t["cleanup"], "retention.ms": contracts.duration_ms(t["retention"]),
            "min.insync.replicas": isr, "compression.type": reg["defaults"]["compression"]}))
    dlq = reg["dlq"]
    for stage in dlq["stages"]:
        specs.append((f"psim.dlq.{stage}.v1", dlq["partitions"][profile], rf, {
            "cleanup.policy": "delete", "retention.ms": contracts.duration_ms(dlq["retention"]),
            "min.insync.replicas": isr, "compression.type": reg["defaults"]["compression"]}))
    return specs


def topics(env, profile):
    specs = topic_specs(profile)
    existing = set(env.exec("kafka-1", f"{KAFKA}/kafka-topics.sh", "--bootstrap-server", "kafka-1:19092",
                            "--list").split())
    created = 0
    for name, partitions, rf, config in specs:
        if name in existing:
            continue
        args = [f"{KAFKA}/kafka-topics.sh", "--bootstrap-server", "kafka-1:19092", "--create", "--topic", name,
                "--partitions", str(partitions), "--replication-factor", str(rf)]
        for k, v in config.items():
            args += ["--config", f"{k}={v}"]
        env.exec("kafka-1", *args)
        created += 1
    print(f"topics: {len(specs)} in the registry ({profile} profile), {created} created")
    return 0


# ------------------------------------------------------------------------------------------ test

class Smoke:
    def __init__(self, env, brokers):
        self.env, self.brokers = env, brokers
        self.token = f"smoke{random.getrandbits(48):012x}"
        self.failures = []

    def check(self, name, fn):
        started = time.time()
        try:
            detail = fn()
            print(f"ok   {name} ({time.time() - started:.1f}s){': ' + detail if detail else ''}")
        except Exception as e:  # every failure is reported, the run continues
            print(f"FAIL {name}: {e}")
            self.failures.append(name)

    # --- Kafka
    def kafka_quorum(self):
        out = self.env.exec("kafka-1", f"{KAFKA}/kafka-metadata-quorum.sh", "--bootstrap-server", "kafka-1:19092",
                            "describe", "--status")
        voters = next(line for line in out.splitlines() if line.startswith("CurrentVoters"))
        count = voters.count('"id"')
        assert count == self.brokers, f"{count} voters, expected {self.brokers}"
        return f"{count} voters"

    def kafka_roundtrip(self, bootstrap):
        topic = f"psim.smoke.{self.token}.v1"
        kt = [f"{KAFKA}/kafka-topics.sh", "--bootstrap-server", "kafka-1:19092"]
        self.env.exec("kafka-1", *kt, "--create", "--topic", topic, "--partitions", "1",
                      "--replication-factor", str(self.brokers))
        try:
            self.env.exec("kafka-1", f"{KAFKA}/kafka-console-producer.sh", "--bootstrap-server", bootstrap,
                          "--topic", topic, stdin=f"{self.token}\n")
            out = self.env.exec("kafka-1", f"{KAFKA}/kafka-console-consumer.sh", "--bootstrap-server", bootstrap,
                                "--topic", topic, "--from-beginning", "--max-messages", "1", "--timeout-ms", "20000")
            assert self.token in out, "message not consumed"
        finally:
            self.env.exec("kafka-1", *kt, "--delete", "--topic", topic, check=False)
        return f"via {bootstrap}"

    def kafka_topics(self):
        profile = "cluster" if self.brokers > 1 else "single"
        out = self.env.exec("kafka-1", f"{KAFKA}/kafka-topics.sh", "--bootstrap-server", "kafka-1:19092",
                            "--describe", "--exclude-internal")
        actual = {}
        for line in out.splitlines():
            if line.startswith("Topic:") and "PartitionCount:" in line:
                fields = dict(f.split(": ", 1) for f in line.split("\t") if ": " in f)
                actual[fields["Topic"]] = (int(fields["PartitionCount"]), int(fields["ReplicationFactor"]))
        specs = topic_specs(profile)
        for name, partitions, rf, _ in specs:
            assert actual.get(name) == (partitions, rf), f"{name}: {actual.get(name)} != {(partitions, rf)}"
        return f"{len(specs)} topics match the registry ({profile})"

    # --- Schema Registry
    def registry(self):
        subject = f"{self.token}-value"
        schema = 'syntax = "proto3"; package psim.smoke.v1; message Smoke { string id = 1; }'
        base = "http://registry:8080/apis/ccompat/v7"
        status, body = self.env.http_json("POST", f"{base}/subjects/{subject}/versions",
                                          body={"schemaType": "PROTOBUF", "schema": schema},
                                          headers=["Content-Type: application/vnd.schemaregistry.v1+json"])
        assert status == 200 and body and "id" in body, f"register: {status} {body}"
        status, body = self.env.http_json("GET", f"{base}/subjects/{subject}/versions/latest")
        assert status == 200 and body["schemaType"] == "PROTOBUF", f"read: {status}"
        self.env.http("DELETE", f"{base}/subjects/{subject}")
        return f"schema id {body['id']} via the Confluent-compatible API"

    # --- PostgreSQL
    def postgres(self, host="postgres", port="5432"):
        for db, user in (("psim", "psim"), ("registry", "registry"), ("keycloak", "keycloak")):
            out = self.env.exec("postgres", "env", f"PGPASSWORD=psim-dev-{user}", "psql", "-h", host, "-p", port,
                                "-U", user, "-d", db, "-tAc", "SELECT current_database()")
            assert out.strip() == db, f"{db}: {out!r}"
        return f"databases psim, registry, keycloak via {host}:{port}"

    # --- ClickHouse
    def clickhouse(self):
        table = f"default.{self.token}"
        q = ["clickhouse-client", "--user", "psim", "--password", "psim-dev-clickhouse", "-q"]
        self.env.exec("clickhouse", *q, f"CREATE TABLE {table} ON CLUSTER psim (id UInt64) "
                                        f"ENGINE = ReplicatedMergeTree('/clickhouse/tables/{{shard}}/{self.token}', "
                                        f"'{{replica}}') ORDER BY id")
        try:
            self.env.exec("clickhouse", *q, f"INSERT INTO {table} SELECT number FROM numbers(1000)")
            count = self.env.exec("clickhouse", *q, f"SELECT count() FROM {table}").strip()
            assert count == "1000", count
        finally:
            self.env.exec("clickhouse", *q, f"DROP TABLE IF EXISTS {table} ON CLUSTER psim SYNC", check=False)
        return "ReplicatedMergeTree through Keeper, cluster psim"

    # --- Valkey
    def valkey(self, host="valkey", port="6379"):
        cli = ["valkey-cli", "-h", host, "-p", port, "-a", "psim-dev-valkey", "--no-auth-warning"]
        self.env.exec("valkey", *cli, "SET", self.token, "1", "EX", "60")
        assert self.env.exec("valkey", *cli, "GET", self.token).strip() == "1"
        return f"via {host}:{port}"

    # --- Keycloak (SR-02)
    def keycloak(self):
        base = "http://keycloak:8080"
        status, conf = self.env.http_json("GET", f"{base}/realms/psim/.well-known/openid-configuration")
        assert status == 200, f"discovery: {status}"
        status, token = self.env.http_json("POST", f"{base}/realms/psim/protocol/openid-connect/token", form={
            "grant_type": "client_credentials", "client_id": "psim-services", "client_secret": "psim-dev-services-secret"})
        assert status == 200 and token.get("access_token"), f"client credentials: {status}"
        status, admin = self.env.http_json("POST", f"{base}/realms/master/protocol/openid-connect/token", form={
            "grant_type": "password", "client_id": "admin-cli", "username": "admin",
            "password": "psim-dev-keycloak-admin"})
        assert status == 200, f"admin token: {status}"
        auth = [f"Authorization: Bearer {admin['access_token']}"]
        _, realm = self.env.http_json("GET", f"{base}/admin/realms/psim", headers=auth)
        assert "length(12)" in realm["passwordPolicy"], realm["passwordPolicy"]
        assert realm["bruteForceProtected"], "brute force protection is off"
        assert realm["ssoSessionIdleTimeout"] == 1800 and realm["ssoSessionMaxLifespan"] == 43200, "session timeouts"
        assert realm["browserFlow"] == "psim-browser", realm["browserFlow"]
        _, executions = self.env.http_json("GET", f"{base}/admin/realms/psim/authentication/flows/psim-browser/executions",
                                           headers=auth)
        providers = {e.get("providerId") for e in executions}
        assert {"conditional-user-role", "auth-otp-form"} <= providers, f"MFA flow: {providers}"
        _, clients = self.env.http_json("GET", f"{base}/admin/realms/psim/clients?clientId=psim-console", headers=auth)
        console = clients[0]
        assert not console["directAccessGrantsEnabled"] and not console["implicitFlowEnabled"], "console grants"
        assert console["attributes"].get("pkce.code.challenge.method") == "S256", "PKCE"
        return "OIDC, client credentials, SR-02 policy (password, lockout, sessions, MFA for admin and supervisor, PKCE)"

    # --- Observability: one OTLP request per signal, then read it back from the store
    def otlp(self, path, payload):
        status, text = self.env.http("POST", f"http://otel-collector:4318/v1/{path}", body=payload)
        assert status == 200, f"collector /v1/{path}: {status} {text[:200]}"

    def resource(self):
        return {"attributes": [{"key": "service.name", "value": {"stringValue": "psim-smoke"}}]}

    def traces(self):
        trace_id = f"{random.getrandbits(128):032x}"
        now = time.time_ns()
        self.otlp("traces", {"resourceSpans": [{"resource": self.resource(), "scopeSpans": [{"spans": [{
            "traceId": trace_id, "spanId": f"{random.getrandbits(64):016x}", "name": "smoke", "kind": 1,
            "startTimeUnixNano": str(now - 1_000_000), "endTimeUnixNano": str(now)}]}]}]})
        found = until(lambda: self.env.http("GET", f"http://tempo:3200/api/traces/{trace_id}")[0] == 200, 60, 2)
        assert found, "trace not found in Tempo"
        return "Collector -> Tempo"

    def logs(self):
        now = time.time_ns()
        self.otlp("logs", {"resourceLogs": [{"resource": self.resource(), "scopeLogs": [{"logRecords": [{
            "timeUnixNano": str(now), "severityText": "INFO", "body": {"stringValue": f"smoke {self.token}"}}]}]}]})
        query = f'{{service_name="psim-smoke"}} |= "{self.token}"'

        def found():
            status, body = self.env.http_json(
                "GET", "http://loki:3100/loki/api/v1/query_range?limit=10&query=" + urllib.parse.quote(query))
            return status == 200 and body["data"]["result"]
        assert until(found, 60, 2), "log not found in Loki"
        return "Collector -> Loki"

    def metrics(self):
        now = time.time_ns()
        self.otlp("metrics", {"resourceMetrics": [{"resource": self.resource(), "scopeMetrics": [{"metrics": [{
            "name": "psim_smoke_value", "gauge": {"dataPoints": [{
                "asDouble": 1.0, "timeUnixNano": str(now),
                "attributes": [{"key": "smoke_id", "value": {"stringValue": self.token}}]}]}}]}]}]})

        def found():
            status, body = self.env.http_json(
                "GET", f"http://prometheus:9090/api/v1/query?query=psim_smoke_value%7Bsmoke_id%3D%22{self.token}%22%7D")
            return status == 200 and body["data"]["result"]
        assert until(found, 60, 2), "metric not found in Prometheus"
        return "Collector -> Prometheus"

    def grafana(self):
        for uid in ("prometheus", "loki", "tempo"):
            status, body = self.env.http_json("GET", f"http://grafana:3000/api/datasources/uid/{uid}/health",
                                              user="admin:psim-dev-grafana")
            assert status == 200 and body.get("status") == "OK", f"{uid}: {status} {body}"
        return "data sources Prometheus, Loki, Tempo healthy"

    # --- Toxiproxy: a latency toxic must slow down the proxied path and only it
    def toxiproxy(self):
        api = "http://toxiproxy:8474/proxies/postgres/toxics"
        sql = ["env", "PGPASSWORD=psim-dev-psim", "psql", "-h", "toxiproxy", "-p", "15432", "-U", "psim", "-d", "psim",
               "-tAc", "SELECT 1"]

        def timed():
            started = time.time()
            self.env.exec("postgres", *sql)
            return time.time() - started
        base = timed()
        status, _ = self.env.http("POST", api, body={"name": "smoke_latency", "type": "latency", "stream": "downstream",
                                                     "attributes": {"latency": 500}})
        assert status == 200, f"add toxic: {status}"
        try:
            slow = timed()
        finally:
            self.env.http("DELETE", f"{api}/smoke_latency")
        assert slow > base + 0.4, f"latency toxic had no effect: {base:.2f}s -> {slow:.2f}s"
        return f"postgres via proxy {base:.2f}s, with 500 ms toxic {slow:.2f}s"

    def run(self):
        self.env.start_probe()
        try:
            self.check("kafka: KRaft quorum", self.kafka_quorum)
            self.check("kafka: produce and consume", lambda: self.kafka_roundtrip("kafka-1:19092"))
            self.check("kafka: produce and consume through Toxiproxy", lambda: self.kafka_roundtrip("toxiproxy:29092"))
            self.check("kafka: topics of the registry", self.kafka_topics)
            self.check("schema registry", self.registry)
            self.check("postgresql", self.postgres)
            self.check("postgresql through Toxiproxy", lambda: self.postgres("toxiproxy", "15432"))
            self.check("clickhouse", self.clickhouse)
            self.check("valkey", self.valkey)
            self.check("valkey through Toxiproxy", lambda: self.valkey("toxiproxy", "16379"))
            self.check("keycloak", self.keycloak)
            self.check("traces", self.traces)
            self.check("logs", self.logs)
            self.check("metrics", self.metrics)
            self.check("grafana", self.grafana)
            self.check("toxiproxy latency", self.toxiproxy)
        finally:
            self.env.stop_probe()
        print(f"env:test: {len(self.failures)} failures")
        return 1 if self.failures else 0


# ---------------------------------------------------------------------------- observability (FF-06)

def service_binaries(build_dir):
    found = []
    for top in ("services", "tools/service-template"):
        for dirpath, dirnames, filenames in os.walk(os.path.join(ROOT, build_dir, top)):
            dirnames[:] = [d for d in dirnames if d != "CMakeFiles"]
            for name in filenames:
                path = os.path.join(dirpath, name)
                if name.startswith("psim-") and os.access(path, os.X_OK):
                    found.append(os.path.relpath(path, ROOT))
    return sorted(found)


REQUIRED_LOG_KEYS = {"ts", "level", "service", "instance", "msg"}


def observe(env, binary, image):
    executable = os.path.basename(binary)
    service = executable[len("psim-"):]
    container = f"{env.project}-{executable}"
    subprocess.run(["docker", "rm", "-f", container], capture_output=True)
    subprocess.run(["docker", "run", "-d", "--name", container, "--network", f"{env.project}_default",
                    "-v", f"{ROOT}:/repo:ro",
                    "-e", "PSIM__TELEMETRY__OTLP_ENDPOINT=otel-collector:4317",
                    "-e", "PSIM__TELEMETRY__SAMPLING_RATIO=1",
                    "-e", f"PSIM__SERVICE__INSTANCE={service}-ff06",
                    "-e", "PSIM__ADMIN__LISTEN=0.0.0.0:9100",
                    image, f"/repo/{binary}"], check=True, capture_output=True)
    base = f"http://{container}:9100"
    try:
        assert until(lambda: env.http("GET", f"{base}/health/live")[0] == 200, 30, 0.5), "/health/live"
        assert until(lambda: env.http("GET", f"{base}/health/ready")[0] == 200, 30, 0.5), "/health/ready"
        status, metrics = env.http("GET", f"{base}/metrics")
        assert status == 200 and "psim_runtime_ready 1" in metrics and "psim_runtime_info{" in metrics, "/metrics"

        def traced():
            query = urllib.parse.quote(f'{{resource.service.name="{service}"}}')
            status, body = env.http_json("GET", f"http://tempo:3200/api/search?limit=5&q={query}")
            return status == 200 and bool(body and body.get("traces"))
        assert until(traced, 60, 2), f"no traces of {service} in Tempo"
    finally:
        subprocess.run(["docker", "stop", "-t", "30", container], capture_output=True)
    exit_code = subprocess.run(["docker", "inspect", "-f", "{{.State.ExitCode}}", container],
                               capture_output=True, text=True).stdout.strip()
    logs = subprocess.run(["docker", "logs", container], capture_output=True, text=True).stdout.splitlines()
    subprocess.run(["docker", "rm", "-f", container], capture_output=True)
    assert exit_code == "0", f"exit code {exit_code} after SIGTERM"
    records = [json.loads(line) for line in logs if line.strip()]
    missing = [r for r in records if not REQUIRED_LOG_KEYS <= r.keys()]
    assert not missing, f"log records without {REQUIRED_LOG_KEYS}: {missing[:1]}"
    assert any(r["msg"] == "service stopped" and r.get("exit_code") == 0 for r in records), "no clean stop in logs"
    return f"health, metrics, {len(records)} JSON log records, traces in Tempo, clean drain on SIGTERM"


def observability(env, build_dir, image):
    binaries = service_binaries(build_dir)
    if not binaries:
        print(f"observability: no service executables in {build_dir}", file=sys.stderr)
        return 1
    env.start_probe()
    smoke = Smoke(env, 1)
    try:
        for binary in binaries:
            smoke.check(f"FF-06 {os.path.basename(binary)}", lambda b=binary: observe(env, b, image))
    finally:
        env.stop_probe()
    print(f"observability: {len(binaries)} services, {len(smoke.failures)} failures")
    return 1 if smoke.failures else 0


def main(argv):
    if len(argv) == 3 and argv[1] == "wait":
        return wait(Env(argv[2]))
    if len(argv) == 4 and argv[1] == "topics" and argv[3] in ("single", "cluster"):
        return topics(Env(argv[2]), argv[3])
    if len(argv) == 5 and argv[1] == "observability":
        return observability(Env(argv[2]), argv[3], argv[4])
    if len(argv) == 4 and argv[1] == "test":
        return Smoke(Env(argv[2]), int(argv[3])).run()
    print(__doc__, file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
