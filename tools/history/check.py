"""Verify event history, causal links, retention and searches against real infrastructure."""
import concurrent.futures
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import tempfile
import time
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/catalog"))
from check import Check as HttpCheck, TASK, uid  # noqa: E402


def stamp(ms):
    return dt.datetime.fromtimestamp(ms / 1000, dt.timezone.utc).isoformat().replace("+00:00", "Z")


class Check(HttpCheck):
    def __init__(self, project, preset, count, benchmark=False):
        self.project, self.preset, self.count = project, preset, count
        self.output = ROOT / "build/event-history" / (preset + ("-bench" if benchmark else ""))
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.env = os.environ | {"PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid()), "PSIM_PRESET": preset}
        self.owned = subprocess.run(["docker", "network", "inspect", project + "_default"], capture_output=True).returncode != 0
        self.toolchain = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
        self.tokens, self.ports = {}, {}
        self.ca = None
        self.tenant, self.retention_tenant = uid(), uid()
        self.site_id, self.zone, self.device, self.rule, self.incident_id = uid(), uid(), uid(), uid(), uid()
        self.foreign_site, self.fixtures = uid(), uid()
        self.config = self.output / "service.json"
        self.incident_config = self.output / "incident.json"
        self.override = self.output / "compose.json"
        image = "debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5"
        base = {"image": image, "profiles": ["history-test"], "user": f"{os.getuid()}:{os.getgid()}", "read_only": True,
                "cap_drop": ["ALL"], "security_opt": ["no-new-privileges:true"] + (["seccomp=unconfined"] if preset == "tsan" else []),
                "volumes": [str(ROOT) + ":/repo:ro", str(ROOT / f"build/debian13/{preset}/profiles") + ":/profiles"],
                "tmpfs": ["/tmp"], "environment": {"LLVM_PROFILE_FILE": "/profiles/%p-%m.profraw"},
                "command": [f"/repo/build/debian13/{preset}/services/event-history/psim-event-history", "--config", "/repo/" + str(self.config.relative_to(ROOT))],
                "ports": ["127.0.0.1::8443", "127.0.0.1::9100"], "stop_grace_period": "35s"}
        services = {"incident-service": base | {"command": [f"/repo/build/debian13/{preset}/services/incident-service/psim-incident-service", "--config", "/repo/" + str(self.incident_config.relative_to(ROOT))]}, "event-history": base, "history-2": base | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": "history-2"}},
                    "keycloak": {"ports": ["127.0.0.1::8080"]}, "registry": {"ports": ["127.0.0.1::8080"]}}
        self.history_apps = ["event-history", "history-2"]
        if benchmark:
            for name in ("history-3", "history-4"):
                self.history_apps.append(name)
                services[name] = base | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": name}}
        self.override.write_text(json.dumps({"services": services}))
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml"), "-f", str(self.override)]
        self.report = {"passed": False, "checks": [], "count": count, "historyInstances": len(self.history_apps)}
        self.dataset = {"seed": uid(), "timeMs": int(time.time() * 1000) - 60000, "count": 30, "repeat": 2}

    def wait(self, callback, timeout=60):
        end, last = time.monotonic() + timeout, None
        while time.monotonic() < end:
            try:
                result = callback()
                if result:
                    return result
            except (OSError, ValueError, AssertionError, RuntimeError) as error:
                last = error
            time.sleep(0.1)
        raise AssertionError(f"condition timed out: {last}")

    def port(self, service, port):
        if (service, port) not in self.ports:
            self.ports[service, port] = self.compose("port", service, str(port)).strip().rsplit(":", 1)[1]
        return self.ports[service, port]

    def env_command(self, command, network=True):
        return super().env_command(f"LLVM_PROFILE_FILE=/repo/build/debian13/{self.preset}/profiles/%p-%m.profraw " + command, network)

    def request(self, method, path, body=None, role="operator", version=None, expected=200, key=None, content_type=None, service="event-history"):
        return super().request(method, path, body, role, version, expected, key, content_type, service)

    def principals(self):
        admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        clients = "/admin/realms/psim/clients"
        resource = self.keycloak("GET", clients + "?clientId=psim", admin)[0]["id"]
        cases = {"operator": ("operator", [self.site_id], self.tenant), "chief": ("supervisor", [self.site_id], self.tenant),
                 "admin": ("admin", ["*"], self.tenant), "auditor": ("auditor", ["*"], self.tenant),
                 "other": ("operator", [self.foreign_site], self.tenant), "empty": ("operator", [], self.tenant),
                 "tenant": ("operator", ["*"], uid()), "retention": ("operator", ["*"], self.retention_tenant),
                 "zone": ("operator", ["zone:" + self.zone], self.tenant)}
        for name, (role, scopes, tenant) in cases.items():
            client_id = "psim-history-" + self.fixtures + "-" + name
            self.keycloak("POST", clients, admin, {"clientId": client_id, "enabled": True, "publicClient": False,
                "serviceAccountsEnabled": True, "standardFlowEnabled": False, "secret": "psim-dev-catalog", "protocol": "openid-connect", "protocolMappers": [
                {"name": "audience", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper", "config": {"included.client.audience": "psim", "access.token.claim": "true"}},
                {"name": "tenant", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_tenant", "claim.value": tenant, "jsonType.label": "String", "access.token.claim": "true"}},
                {"name": "scope", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_scopes", "claim.value": json.dumps(scopes), "jsonType.label": "JSON", "access.token.claim": "true"}}]})
            client = self.keycloak("GET", clients + "?clientId=" + client_id, admin)[0]["id"]
            account = self.keycloak("GET", clients + "/" + client + "/service-account-user", admin)["id"]
            role_body = self.keycloak("GET", clients + "/" + resource + "/roles/" + role, admin)
            self.keycloak("POST", "/admin/realms/psim/users/" + account + "/role-mappings/clients/" + resource, admin, [role_body])
            self.tokens[name] = {"client": client_id, "value": "", "expires": 0}

    def ch(self, sql):
        return self.compose("exec", "-T", "clickhouse", "clickhouse-client", "--user", "psim", "--password", "psim-dev-clickhouse", "-q", sql).strip()

    def pg(self, sql):
        return self.compose("exec", "-T", "postgres", "psql", "-U", "psim", "-d", "psim", "-Atc", sql).strip()

    def emit(self, mode, **values):
        data = {"mode": mode, "tenant": self.tenant, "site": self.site_id, "zone": self.zone, "device": self.device,
                "rule": self.rule, "incident": self.incident_id, "seed": uid(), "timeMs": int(time.time() * 1000)} | values
        path = self.output / ("input-" + uid() + ".json")
        path.write_text(json.dumps(data))
        raw = self.env_command(f"build/debian13/{self.preset}/tools/history/psim-history-probe /repo/{path.relative_to(ROOT)}")
        return json.loads(raw.strip().splitlines()[-1])

    def prepare(self):
        self.env_command("sh tools/env/event_flow_tls.sh", False)
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem")
        secrets = ROOT / ".cache/processing"
        secrets.mkdir(parents=True, exist_ok=True)
        password = secrets / "clickhouse.password"
        with tempfile.NamedTemporaryFile(mode="w", dir=secrets, delete=False) as temporary:
            temporary.write("psim-dev-clickhouse")
        os.chmod(temporary.name, 0o400)
        os.replace(temporary.name, password)
        (ROOT / f"build/debian13/{self.preset}/profiles").mkdir(parents=True, exist_ok=True)
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keycloak", "keeper", "clickhouse", "otel-collector")
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.env_command("python3 -I tools/env/keycloak_security.py http://keycloak:8080")
        self.principals()
        self.emit("config", tenant=self.retention_tenant, allSeconds=15, significantSeconds=60)
        self.config.write_text(json.dumps({"service": {"instance": "history-1"}, "runtime": {"shards": 1, "blocking_threads": 8},
            "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001},
            "settings": {"allow_plaintext": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "introspection_secret_file": "/repo/.cache/event-flow/introspection.secret", "clickhouse_host": "clickhouse",
                "clickhouse_port": 9000, "clickhouse_password_file": "/repo/.cache/processing/clickhouse.password", "listen_port": 8443,
                "history_api": True, "history_batch_messages": 50000 if self.preset == "release" else 5000,
                "history_batch_wait_ms": 500 if self.preset == "release" else 100, "issuer": "http://keycloak:8080/realms/psim"}}))
        self.incident_config.write_text(json.dumps({"service": {"instance": "history-evidence"}, "runtime": {"shards": 1}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001}, "settings": {"allow_plaintext": True, "legacy_types": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "introspection_secret_file": "/repo/.cache/event-flow/introspection.secret", "issuer": "http://keycloak:8080/realms/psim", "listen_port": 8443}}))
        self.compose("up", "-d", "--force-recreate", *self.history_apps, "incident-service")
        self.interval = {"from": stamp(self.dataset["timeMs"] - 86400000), "to": stamp(self.dataset["timeMs"] + 86400000)}
        self.wait(lambda: self.search()["events"] == [], 90)
        for service in self.history_apps[1:]:
            self.wait(lambda service=service: self.request("GET", "/v1/events?" + urllib.parse.urlencode(self.interval), service=service)["events"] == [], 90)
        self.mark(f"{len(self.history_apps)} HTTPS history instances with verified JWTs and native ClickHouse")

    def search(self, role="operator", **filters):
        return self.request("GET", "/v1/events?" + urllib.parse.urlencode(self.interval | filters, doseq=True), role=role)

    def basics(self):
        self.signal_ids = self.emit("signals", **self.dataset)["ids"]
        self.emit("incidents", **self.dataset)
        self.wait(lambda: self.ch(f"SELECT count() FROM signal_facts WHERE tenant_id='{self.tenant}'") == "30")
        path = "/v1/incidents/" + self.incident_id + "/events"
        pending = self.wait(lambda: (r if len((r := self.request("GET", path))["unavailableEventIds"]) == 30 else None))
        assert pending["events"] == []
        self.event_ids = self.emit("events", **self.dataset)["ids"]
        self.wait(lambda: len(self.search()["events"]) == 30)
        self.wait(lambda: self.ch(f"SELECT count() FROM retained_events FINAL WHERE tenant_id='{self.tenant}'") == "30", 90)
        self.wait(lambda: self.pg(f"SELECT count(*) FROM history.retentions WHERE tenant='{self.tenant}'") == "0")
        assert {e["eventId"] for e in self.request("GET", path)["events"]} == set(self.event_ids)
        pages, token = [], None
        while True:
            response = self.search(pageSize=7, **({"pageToken": token} if token else {}))
            pages += response["events"]
            token = response.get("nextPageToken")
            if not token:
                break
        assert len(pages) == 30 and {v["eventId"] for v in pages} == set(self.event_ids)
        times = [dt.datetime.fromisoformat(v["occurredAt"].replace("Z", "+00:00")) for v in pages]
        assert times == sorted(times, reverse=True)
        for role in ("other", "empty", "tenant"):
            assert self.search(role=role)["events"] == []
        assert len(self.search(role="zone")["events"]) == 30
        event_path = "/v1/events/" + self.event_ids[0]
        assert self.request("GET", event_path)["event"]["eventId"] == self.event_ids[0]
        self.request("GET", event_path, role="other", expected=404)
        self.request("GET", event_path, role=None, expected=401)
        self.request("GET", event_path, role="admin", expected=403)
        assert len(self.search(deviceIds=[self.device], types=["generic.*"])["events"]) == 30
        assert self.search(types=["' OR 1=1 --"])["events"] == []
        self.request("GET", "/v1/events", expected=400)
        for query in ("pageSize=1001", "pageSize=1x", "pageToken=broken", "unknown=x", "pageSize=1&pageSize=2"):
            self.request("GET", "/v1/events?" + urllib.parse.urlencode(self.interval) + "&" + query, expected=400)
        token = self.search(pageSize=1)["nextPageToken"]
        self.request("GET", "/v1/events?" + urllib.parse.urlencode(self.interval | {"pageSize": 1, "pageToken": token, "significantOnly": "true"}), expected=400)
        statistics = self.request("GET", "/v1/events/rule-statistics?" + urllib.parse.urlencode(self.interval), role="chief")["statistics"]
        assert sorted(int(s["signalCount"]) for s in statistics) == [15, 15]
        assert sum(int(s["eventReferenceCount"]) for s in statistics) == 30
        self.wait(lambda: int(self.pg("SELECT count(*) FROM history.psim_outbox WHERE topic='psim.audit.v1'")) >= 1)
        self.mark("out-of-order evidence, complete paginated links, deduplicated statistics, scoped filters and durable denial audit")

    def retention(self):
        data = {"seed": uid(), "tenant": self.retention_tenant, "timeMs": int(time.time() * 1000), "count": 1}
        event = self.emit("events", **data)["ids"][0]
        self.emit("signals", **data)
        path = "/v1/events/" + event
        self.wait(lambda: self.request("GET", path, role="retention")["event"]["event"]["significant"], 30)
        self.compose("restart", "event-history", "history-2")
        self.wait(lambda: self.request("GET", path, role="retention")["event"]["event"]["significant"], 30)
        remaining = (data["timeMs"] + 15000) / 1000 - time.time()
        if remaining > 0:
            time.sleep(remaining + 0.2)
        self.ch("ALTER TABLE events MATERIALIZE TTL SETTINGS mutations_sync=2")
        self.wait(lambda: self.ch(f"SELECT count() FROM events FINAL WHERE event_id='{event}'") == "0", 20)
        assert self.request("GET", path, role="retention")["event"]["eventId"] == event
        self.wait(lambda: self.request("GET", path, role="retention", expected=404), 70)
        self.ch("ALTER TABLE retained_events MATERIALIZE TTL SETTINGS mutations_sync=2")
        self.wait(lambda: self.ch(f"SELECT count() FROM retained_events FINAL WHERE event_id='{event}'") == "0")
        self.mark("signal references extend event retention across restart; ordinary and extended TTL remove expired bodies")

    def failures(self):
        data = {"seed": uid(), "timeMs": int(time.time() * 1000) - 1000, "count": self.count, "repeat": 2}
        expected = self.emit("events", **data)["ids"]
        self.compose("stop", "clickhouse")
        self.emit("signals", **data)
        self.emit("incidents", **data)
        self.wait(lambda: int(self.pg("SELECT count(*) FROM psim_clickhouse.pending_batches")) > 0, 60)
        self.compose("kill", "-s", "SIGKILL", "event-history", "history-2")
        self.wait(lambda: len(self.compose("ps", "--all", "--format", "{{.State}}", "event-history", "history-2").strip().splitlines()) == 2 and "running" not in self.compose("ps", "--all", "--format", "{{.State}}", "event-history", "history-2"))
        self.compose("start", "clickhouse", "event-history", "history-2")
        self.wait(lambda: self.ch(f"SELECT count() FROM signal_facts WHERE tenant_id='{self.tenant}'") == str(30 + self.count), 120)
        self.emit("signals", **data)
        self.emit("incidents", **data)
        self.wait(lambda: self.ch(f"SELECT count() FROM retained_events FINAL WHERE tenant_id='{self.tenant}'") == str(self.count + 30), 180)
        self.wait(lambda: self.pg(f"SELECT count(*) FROM history.retentions WHERE tenant='{self.tenant}'") == "0", 120)
        actual = self.ch(f"SELECT event_id FROM event_signals WHERE tenant_id='{self.tenant}' ORDER BY event_id").splitlines()
        assert len(actual) == len(set(actual)) == self.count + 30
        assert set(actual) == set(expected + self.event_ids)
        self.wait(lambda: self.ch(f"SELECT count() FROM incident_signals WHERE tenant_id='{self.tenant}' AND incident_id='{self.incident_id}'") == str(self.count + 30), 120)
        self.wait(lambda: self.ch(f"SELECT uniqExact(signal_id) FROM incident_signals WHERE tenant_id='{self.tenant}' AND incident_id!='{self.incident_id}'") == str(self.count + 30), 180)
        actual_incident = self.ch(f"SELECT incident_id FROM incident_signals WHERE tenant_id='{self.tenant}' AND signal_id='{self.signal_ids[0]}' AND incident_id!='{self.incident_id}' LIMIT 1")
        assert {item["eventId"] for item in self.request("GET", "/v1/incidents/" + actual_incident + "/events")["events"]} == set(self.event_ids)
        self.mark("ClickHouse outage, durable prepared blocks, SIGKILL, rebalance and full link identity reconciliation")

    def promotion_crash(self):
        tenant = uid()
        data = {"tenant": tenant, "seed": uid(), "timeMs": int(time.time() * 1000), "count": 20}
        self.emit("events", **data)
        self.pg("CREATE OR REPLACE FUNCTION history.pause_delete() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN PERFORM pg_sleep(5); RETURN OLD; END $$; CREATE TRIGGER history_pause_delete BEFORE DELETE ON history.retentions FOR EACH ROW EXECUTE FUNCTION history.pause_delete()")
        try:
            self.emit("signals", **data)
            self.wait(lambda: int(self.pg("SELECT count(*) FROM pg_stat_activity WHERE query LIKE 'DELETE FROM history.retentions%' AND wait_event='PgSleep'")) > 0)
            self.compose("kill", "-s", "SIGKILL", "event-history", "history-2")
            self.wait(lambda: "running" not in self.compose("ps", "--all", "--format", "{{.State}}", "event-history", "history-2"))
            self.pg("SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE query LIKE 'DELETE FROM history.retentions%'")
        finally:
            self.pg("DROP TRIGGER IF EXISTS history_pause_delete ON history.retentions; DROP FUNCTION IF EXISTS history.pause_delete()")
        self.compose("start", "event-history", "history-2")
        self.wait(lambda: self.pg(f"SELECT count(*) FROM history.retentions WHERE tenant='{tenant}'") == "0", 120)
        assert self.ch(f"SELECT count() FROM retained_events FINAL WHERE tenant_id='{tenant}'") == "20"
        self.mark("retention retries after ClickHouse commit and before PostgreSQL commit without duplicate logical events")

    def large_retention(self):
        data = {"seed": uid(), "timeMs": int(time.time() * 1000), "count": 128, "padding": 200000, "type": "intrusion.zone.alarm"}
        ids = self.emit("events", **data)["ids"]
        self.wait(lambda: self.ch(f"SELECT count() FROM events FINAL WHERE raw_code='{data['seed']}'") == "128", 120)
        self.compose("stop", "clickhouse")
        self.emit("signals", **data)
        self.wait(lambda: self.pg(f"SELECT count(*) FROM history.retentions WHERE tenant='{self.tenant}'") == "128", 120)
        self.compose("start", "clickhouse")
        self.wait(lambda: self.pg(f"SELECT count(*) FROM history.retentions WHERE tenant='{self.tenant}'") == "0", 120)
        assert self.ch(f"SELECT count() FROM retained_events FINAL WHERE raw_code='{data['seed']}'") == "128"
        self.request("GET", "/v1/events/" + ids[0])
        self.request("GET", "/v1/events?" + urllib.parse.urlencode(self.interval | {"types": "intrusion.zone.alarm", "pageSize": 100}), expected=429)
        self.mark("large evidence adapts retention batches and enforces response byte limits")

    def malformed(self):
        def offsets():
            raw = self.compose("exec", "-T", "kafka-1", "/opt/kafka/bin/kafka-get-offsets.sh", "--bootstrap-server", "kafka-1:19092", "--topic", "psim.dlq.*")
            offsets = {}
            for row in raw.splitlines():
                topic, _, offset = row.rsplit(":", 2)
                offsets[topic] = offsets.get(topic, 0) + int(offset)
            return offsets
        before = offsets()
        data = {"seed": uid(), "count": 1, "site": "invalid"}
        self.emit("events", **data)
        self.emit("signals", **data)
        def delivered():
            after = offsets()
            return after.get("psim.dlq.event-history.v1", 0) >= before.get("psim.dlq.event-history.v1", 0) + 2 and after.get("psim.dlq.history-retention.v1", 0) >= before.get("psim.dlq.history-retention.v1", 0) + 1
        self.wait(delivered, 120)
        self.mark("invalid events and signals reach declared loader and retention dead-letter topics")

    def scoped_links(self):
        data = {"seed": uid(), "timeMs": int(time.time() * 1000), "count": 1, "zone": uid(), "incident": uid()}
        event = self.emit("events", **data)["ids"][0]
        signal = self.emit("signals", **data)["ids"][0]
        self.emit("incidents", **(data | {"zone": self.zone}))
        self.wait(lambda: self.ch(f"SELECT count() FROM incident_signals WHERE tenant_id='{self.tenant}' AND incident_id='{data['incident']}' AND signal_id='{signal}'") == "1")
        path = "/v1/incidents/" + data["incident"] + "/events"
        self.wait(lambda: {v["eventId"] for v in self.request("GET", path)["events"]} == {event})
        response = self.request("GET", path, role="zone")
        assert event not in response.get("unavailableEventIds", [])
        assert event not in {v["eventId"] for v in response["events"]}
        self.mark("incident membership does not reveal evidence identifiers outside the signal scope")

    def failed_start(self):
        self.compose("stop", "-t", "35", "event-history", "history-2")
        result = subprocess.run(self.compose_cmd + ["run", "--rm", "--no-deps", "-e",
            "PSIM__SETTINGS__REGISTRY=http://127.0.0.1:1", "event-history"],
            cwd=ROOT, env=self.env, text=True, capture_output=True, timeout=60)
        output = result.stdout + result.stderr
        (self.output / "failed-start.log").write_text(output)
        assert result.returncode == 1, output
        assert "component failed to start" in output, output
        assert not re.search(r"AddressSanitizer|ThreadSanitizer|runtime error:", output), output
        self.compose("start", "event-history", "history-2")
        self.wait(lambda: self.request("GET", "/v1/events/" + self.event_ids[0]))
        self.mark("registry failure during partial initialization exits cleanly and permits restart")

    def benchmark(self):
        base = {"seed": uid(), "timeMs": int(time.time() * 1000), "count": 200000, "zones": 100, "spreadDay": True}
        baseline = self.emit("events", **base)
        self.interval = {"from": stamp(base["timeMs"] - 86400000), "to": stamp(base["timeMs"] + 1)}
        self.wait(lambda: self.ch(f"SELECT count() FROM events FINAL WHERE raw_code='{base['seed']}'") == str(base["count"]))
        background = base | {"seed": uid(), "count": 1000000, "rate": 60000}
        latencies = []
        before = time.monotonic()
        with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
            pending = executor.submit(self.emit, "events", **background)
            while not pending.done():
                started = time.monotonic()
                self.search(siteIds=[self.site_id], zoneIds=[baseline["firstZone"]], pageSize=100)
                latencies.append((time.monotonic() - started) * 1000)
            emission = pending.result()
        self.wait(lambda: self.ch(f"SELECT count() FROM events FINAL WHERE raw_code='{background['seed']}'") == str(background["count"]), 120)
        elapsed = time.monotonic() - before
        latencies.sort()
        p99 = latencies[max(0, math.ceil(len(latencies) * 0.99) - 1)]
        self.report["benchmark"] = {"written": background["count"], "searches": len(latencies), "searchP99Ms": p99,
            "persistedPerSecond": background["count"] / elapsed, "emission": emission, "rangeHours": 24, "zoneCount": 100}
        assert p99 <= 500 and len(latencies) >= 30, self.report["benchmark"]
        assert background["count"] / elapsed >= 50000, self.report["benchmark"]
        self.mark("one-day history search under at least 50000 persisted events per second")

    def cleanup(self):
        try:
            self.compose("stop", "-t", "35", *self.history_apps, "incident-service")
            logs = self.compose("logs", "--no-color", *self.history_apps, "incident-service")
            (self.output / "servers.log").write_text(logs)
            if re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs):
                self.report["passed"] = False
                raise AssertionError("service sanitizer diagnostics")
        finally:
            try:
                if self.owned and os.environ.get("PSIM_HISTORY_KEEP_ENV") != "1":
                    self.compose("--profile", "history-test", "down", "-v", "--remove-orphans", "--timeout", "10")
            finally:
                (self.output / "report.json").write_text(json.dumps(self.report, indent=2))
                self.log.close()


def main():
    benchmark = len(sys.argv) > 4 and sys.argv[4] == "bench"
    if benchmark and sys.argv[2] != "release":
        raise ValueError("history benchmark requires the release preset")
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]), benchmark)
    try:
        check.prepare()
        if benchmark:
            check.benchmark()
        else:
            check.basics()
            check.retention()
            check.failures()
            check.promotion_crash()
            check.large_retention()
            check.malformed()
            check.scoped_links()
            check.failed_start()
        check.report["passed"] = True
    finally:
        check.cleanup()


if __name__ == "__main__":
    main()
