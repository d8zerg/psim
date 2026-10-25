"""Exercise rule management and isolated historical replay against native infrastructure."""

import concurrent.futures
import datetime as dt
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
import time
import uuid
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/catalog"))
from check import Check as HttpCheck, TASK, OTHER_TENANT, uid  # noqa: E402
import check as http_helpers  # noqa: E402


def stamp(milliseconds):
    return dt.datetime.fromtimestamp(milliseconds / 1000, dt.timezone.utc).isoformat(timespec="milliseconds").replace("+00:00", "Z")


class Check(HttpCheck):
    def __init__(self, project, preset):
        super().__init__(project, preset, 1)
        self.tenant = uid()
        http_helpers.TENANT = self.tenant
        self.log.close()
        self.output = ROOT / "build/rules" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.override = self.output / "compose.json"
        services = {name: {"ports": [f"127.0.0.1::{port}"]} for name, port in
                    [("rule-management", 8443), ("rule-manager-2", 8443), ("keycloak", 8080), ("registry", 8080)]}
        services["kafka-tls"] = {"environment": {
            "KAFKA_LISTENERS": "CONTROLLER://:9093,INTERNAL://:19092,MANAGEMENT://:19096",
            "KAFKA_ADVERTISED_LISTENERS": "INTERNAL://kafka-tls:19092,MANAGEMENT://kafka-tls:19096",
            "KAFKA_LISTENER_SECURITY_PROTOCOL_MAP": "CONTROLLER:SSL,INTERNAL:SSL,MANAGEMENT:PLAINTEXT",
            "KAFKA_AUTHORIZER_CLASS_NAME": "org.apache.kafka.metadata.authorizer.StandardAuthorizer",
            "KAFKA_SUPER_USERS": "User:CN=psim-test-server",
            "KAFKA_ALLOW_EVERYONE_IF_NO_ACL_FOUND": "false", "KAFKA_AUTO_CREATE_TOPICS_ENABLE": "false",
        }}
        self.override.write_text(json.dumps({"services": services}, indent=2))
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml")]
        if preset == "tsan":
            self.compose_cmd += ["-f", str(ROOT / "deploy/local/compose.sanitizer.yaml")]
        self.compose_cmd += ["-f", str(self.override)]
        self.rule_id, self.zone_id, self.device_id, self.source_id = uid(), uid(), uid(), uid()
        self.origin = int(time.time() * 1000) - 10000
        self.report = {"passed": False, "checks": []}

    def request(self, method, path, body=None, role="admin", version=None, expected=200, key=None, content_type=None, service="rule-management"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode() if body is not None else None
        headers = {"Authorization": "Bearer " + self.token(role)} if role else {}
        if data is not None:
            headers["Content-Type"] = content_type or "application/json"
        if method == "POST":
            headers["Idempotency-Key"] = key or uid()
        if version is not None:
            headers["If-Match"] = '"' + str(version) + '"'
        url = "https://localhost:" + self.port(service, 8443) + path
        try:
            with urllib.request.urlopen(urllib.request.Request(url, method=method, data=data, headers=headers), context=self.ca, timeout=90) as response:
                status, raw = response.status, response.read()
        except urllib.error.HTTPError as error:
            status, raw = error.code, error.read()
        allowed = expected if isinstance(expected, tuple) else (expected,)
        assert status in allowed, f"{method} {path}: expected {allowed}, got {status}: {raw[:500]!r}"
        value = json.loads(raw)
        return (status, value) if isinstance(expected, tuple) else value

    def sql(self, statement):
        return self.compose("exec", "-T", "postgres", "psql", "-U", "postgres", "-d", "psim", "-At", "-v", "ON_ERROR_STOP=1", "-c", statement).strip()

    def clickhouse(self, statement):
        return self.compose("exec", "-T", "clickhouse", "clickhouse-client", "--user", "psim", "--password", "psim-dev-clickhouse", "--multiquery", "-q", statement).strip()

    def probe(self, mode, identity="super", name=None):
        path = self.output / (name or mode + "-" + identity + ".json")
        relative = str(path.relative_to(ROOT))
        self.env_command(f"build/debian13/{self.preset}/tools/rules/psim-rules-probe {mode} {relative} {identity}")
        return json.loads((Path(str(path) + ".result.json") if mode == "seed" else path).read_text())

    def prepare(self):
        self.env_command("sh tools/env/event_flow_tls.sh && sh tools/env/security_test_tls.sh && sh tools/rules/identities.sh", network=False)
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem")
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keycloak", "keeper", "clickhouse", "otel-collector")
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.env_command("python3 -I tools/env/keycloak_security.py http://keycloak:8080")
        self.principals()
        self.sql("DO $$ BEGIN IF NOT EXISTS(SELECT 1 FROM pg_roles WHERE rolname='rule_management') THEN CREATE ROLE rule_management LOGIN PASSWORD 'psim-dev-rule-pg'; END IF; END $$; GRANT CONNECT,CREATE ON DATABASE psim TO rule_management; CREATE SCHEMA IF NOT EXISTS catalog; CREATE TABLE IF NOT EXISTS catalog.rules_test_sentinel(value integer); INSERT INTO catalog.rules_test_sentinel SELECT 0 WHERE NOT EXISTS(SELECT 1 FROM catalog.rules_test_sentinel)")
        self.clickhouse("CREATE USER IF NOT EXISTS rule_replay IDENTIFIED WITH sha256_password BY 'psim-dev-rule-clickhouse'; ALTER USER rule_replay SETTINGS readonly=2")
        self.compose("up", "-d", "--force-recreate", "--renew-anon-volumes", "--wait", "--wait-timeout", "150", "kafka-tls")
        self.run(self.compose_cmd + ["exec", "-T", "kafka-tls", "sh", "-s"], input=(ROOT / "tools/rules/broker-acls.sh").read_text())
        self.seed_fixtures()
        self.probe("seed", name="fixtures.json")
        self.clickhouse("GRANT SELECT ON default.events TO rule_replay")
        self.compose("up", "-d", "--force-recreate", "rule-management", "rule-manager-2")
        self.wait(lambda: self.request("GET", "/v1/rules"), 60)
        self.wait(lambda: self.request("GET", "/v1/rules", service="rule-manager-2"), 60)
        self.mark("HTTPS, JWT, PostgreSQL migrations and two management instances")

    def envelope(self, at=None, tenant=None):
        return {"messageId": uid(), "tenantId": self.tenant if tenant is None else tenant, "occurredAt": stamp(self.origin if at is None else at),
                "producedAt": stamp(self.origin + 3000), "correlationId": uid(), "sequence": "1",
                "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "rules-fixture"}, "schemaVersion": "1"}

    def seed_fixtures(self):
        values = []
        def add(topic, key, payload, tenant=None):
            values.append({"topic": topic, "key": key, "value": {"envelope": self.envelope(tenant=tenant)} | payload})
        add("psim.catalog.v1", "location:" + self.zone_id, {"location": {
            "locationId": self.zone_id, "kind": "LOCATION_KIND_ZONE", "siteId": self.site_id,
            "name": "Rule test zone", "code": "zone", "state": "RESOURCE_STATE_ACTIVE", "version": "1"}})
        add("psim.catalog.v1", "device:" + self.device_id, {"device": {
            "deviceId": self.device_id, "deviceTypeId": uid(), "zoneId": self.zone_id, "siteId": self.site_id,
            "connectorId": uid(), "externalId": "test-device", "name": "Rule test device",
            "sources": [{"sourceId": self.source_id, "kind": "alarm", "externalRef": "test-source"}],
            "state": "RESOURCE_STATE_ACTIVE", "version": "1"}})
        maintenance = {"maintenanceId": uid(), "siteId": self.site_id, "zoneId": self.zone_id,
                       "startsAt": stamp(self.origin - 1000), "activatedAt": stamp(self.origin - 1000),
                       "endsAt": stamp(self.origin + 5000), "updatedAt": stamp(self.origin - 1000),
                       "state": "MAINTENANCE_STATE_ACTIVE", "version": "1", "reason": "Synthetic maintenance",
                       "responsible": "test", "suppression": {"ruleIds": [self.rule_id]}}
        for version in (1, 2):
            policy = maintenance | ({"version": "2", "updatedAt": stamp(self.origin + 1500), "suppression": {}} if version == 2 else {})
            add("psim.audit.v1", self.tenant, {"auditEntry": {"actor": {"kind": "ACTOR_KIND_SERVICE", "id": "resource-catalog"},
                "action": "catalog.maintenance.save", "objectType": "maintenance", "objectId": policy["maintenanceId"],
                "siteId": self.site_id, "outcome": "AUDIT_OUTCOME_SUCCESS", "details": {"snapshot": json.dumps(policy), "version": str(version)}}})
        add("psim.config.v1", "maintenance/" + self.tenant + "/" + policy["maintenanceId"], {"maintenanceWindow": policy})
        for offset, kind in [(0, "intrusion.zone.alarm"), (200, "intrusion.zone.restore"), (2000, "intrusion.zone.alarm"),
                             (2200, "intrusion.zone.alarm"), (500, "fire.detector.alarm"), (100, "video.analytics.person_detected")]:
            envelope = self.envelope(self.origin + offset)
            envelope["receivedAt"] = stamp(self.origin + offset + 100)
            values.append({"topic": "psim.events.normalized.v1", "key": self.site_id + ":" + self.zone_id,
                           "value": {"envelope": envelope, "event": {"type": kind, "taxonomyVersion": "1",
                             "eventClass": "EVENT_CLASS_ALARM", "severity": "SEVERITY_HIGH", "siteId": self.site_id,
                             "zoneId": self.zone_id, "deviceId": self.device_id, "sourceId": self.source_id,
                             "connectorId": self.device_id, "rawCode": "fixture", "attributes": {"channel": "one"}}}})
        values[-1]["value"]["event"]["deviceId"] = uid()
        values[-1]["value"]["event"]["sourceId"] = uid()
        foreign = json.loads(json.dumps(values[-1]))
        foreign["value"]["envelope"]["tenantId"] = OTHER_TENANT
        foreign["value"]["envelope"]["messageId"] = uid()
        foreign["value"]["event"]["siteId"] = self.foreign_site
        values.append(foreign)
        (self.output / "fixtures.json").write_text(json.dumps(values, indent=2))

    def rule(self):
        return {"ruleId": self.rule_id, "name": "Perimeter alarm", "correlationScope": "CORRELATION_SCOPE_ZONE",
                "filter": {"types": ["intrusion.*"], "siteIds": [self.site_id]}, "window": "1s", "single": {},
                "output": {"incidentTypeId": uid(), "priority": "PRIORITY_P2", "summaryTemplate": "Alarm ${event.zone_id}"}}

    def activate(self, references, current, **kwargs):
        return self.request("POST", "/v1/rule-sets:activate", {"rules": references, "expectedRuleSetVersion": str(current)}, version=current, **kwargs)

    def dry_run(self, version=1, source="DRY_RUN_SOURCE_KAFKA", role="admin", **kwargs):
        body = {"version": version, "from": stamp(self.origin - 1000), "to": stamp(self.origin + 5000), "source": source}
        return self.request("POST", "/v1/rules/" + self.rule_id + ":dry-run", body, role=role, **kwargs)["dryRun"]

    def completed(self, run, role="admin", state="DRY_RUN_STATE_COMPLETED"):
        def read():
            value = self.request("GET", "/v1/dry-runs/" + run["dryRunId"], role=role)
            return value if value["dryRun"]["state"] != "DRY_RUN_STATE_RUNNING" else None
        value = self.wait(read, 90)
        assert value["dryRun"]["state"] == state, value
        return value

    def basic(self):
        body = self.rule()
        key = uid()
        first = self.request("POST", "/v1/rules", body, key=key)["rule"]
        assert first["version"] == 1 and first["state"] == "RULE_STATE_DRAFT", first
        assert self.request("POST", "/v1/rules", body, key=key)["rule"] == first
        self.request("POST", "/v1/rules", body | {"name": "changed"}, key=key, expected=422)
        self.request("POST", "/v1/rules/" + self.rule_id + ":validate", {"version": 1}, expected=428)
        checked = self.request("POST", "/v1/rules/" + self.rule_id + ":validate", {"version": 1}, version=1)
        assert checked["valid"] and checked["rule"]["state"] == "RULE_STATE_TESTING", checked
        self.reference = {"ruleId": self.rule_id, "version": 1}
        active = self.activate([self.reference], 0)["ruleSet"]
        assert active["version"] == "1" and active["rules"][0]["state"] == "RULE_STATE_ACTIVE", active
        self.activate([self.reference], 0, expected=412)
        self.request("GET", "/v1/rules", role=None, expected=401)
        self.request("GET", "/v1/rules", role="operator", expected=403)
        for role in ("site", "other", "empty"):
            self.request("POST", "/v1/rules", body, role=role, expected=403)
        self.request("GET", "/v1/rules/" + self.rule_id, role="other", expected=403)
        self.request("GET", "/v1/rules/" + self.rule_id, role="tenant", expected=404)
        assert self.request("GET", "/v1/rules", role="other")["rules"] == []
        assert self.request("GET", "/v1/rules", role="site")["rules"][0]["ruleId"] == self.rule_id
        self.request("GET", "/v1/rule-sets/active", role="site", expected=403)
        assert self.request("GET", "/v1/rules?states=RULE_STATE_ACTIVE")["rules"][0]["version"] == 1
        assert self.request("GET", "/v1/rules?states=RULE_STATE_DRAFT")["rules"] == []
        self.mark("immutable draft, validation, atomic activation, idempotency, roles, tenant and site scopes")

    def replay_checks(self):
        before = self.probe("observe", name="before.json")
        first = self.completed(self.dry_run())
        second = self.completed(self.dry_run(source="DRY_RUN_SOURCE_CLICKHOUSE"))
        assert first["decisions"] == second["decisions"], (first, second)
        assert len(first["decisions"]) == 4 and len(first["signals"]) == 2, first
        assert int(first["dryRun"]["eventsProcessed"]) == 6, first
        suppressed = [decision for decision in first["decisions"] if decision["suppressed"]]
        assert len(suppressed) == 2 and all(decision["maintenanceVersions"][0]["version"] == "1" for decision in suppressed), first
        path = "/v1/dry-runs/" + first["dryRun"]["dryRunId"]
        self.request("GET", path, role="site", expected=403)
        self.request("GET", path, role="tenant", expected=404)
        page = self.request("GET", path + "?pageSize=1")
        assert len(page["decisions"]) == 1 and page["nextPageToken"]
        later = self.request("GET", path + "?pageSize=1&pageToken=" + page["nextPageToken"])
        assert page["decisions"][0]["signalId"] != later["decisions"][0]["signalId"]
        own = self.completed(self.dry_run(role="site"), role="site")
        assert own["decisions"] == first["decisions"]
        after = self.probe("observe", name="after.json")
        for topic in ("psim.signals.v1", "psim.incidents.events.v1", "psim.commands.v1", "psim.response.events.v1"):
            assert before["ends"][topic] == after["ends"][topic], (topic, before, after)
        assert self.sql("SELECT count(*) FROM rules.dry_runs WHERE input IS NOT NULL AND finished AND tenant='" + self.tenant + "'") == "3"
        assert self.sql("SELECT count(*) FROM rules.dry_runs WHERE jsonb_array_length(input->'subjects')=0 AND finished AND tenant='" + self.tenant + "'") == "3"
        self.mark("Kafka and ClickHouse agree; versioned suppression, private results, pagination and zero production writes")

    def models(self):
        cases = [
            ("conjunction", {"parts": [{"types": ["intrusion.zone.alarm"]}, {"types": ["video.analytics.person_detected"]}]}, 1),
            ("sequence", {"steps": [{"types": ["intrusion.zone.alarm"]}, {"types": ["intrusion.zone.restore"]}]}, 1),
            ("threshold", {"count": 2}, 1),
            ("absence", {"trigger": {"types": ["intrusion.zone.alarm"]}, "expected": {"types": ["intrusion.zone.restore"]}}, 2),
            ("absence", {"expected": {"types": ["intrusion.zone.restore"]}}, 4),
        ]
        original = self.rule_id
        for kind, spec, count in cases:
            body = self.rule()
            body.pop("single")
            body["ruleId"] = uid()
            body[kind] = spec
            if kind == "conjunction":
                body["filter"]["types"].append("video.*")
            if kind == "threshold":
                body["filter"]["types"] = ["intrusion.zone.alarm"]
            saved = self.request("POST", "/v1/rules", body)["rule"]
            self.rule_id = saved["ruleId"]
            result = self.completed(self.dry_run())
            inventory = kind == "absence" and "trigger" not in spec
            present = self.sql("SELECT jsonb_array_length(input->'subjects')>0 FROM rules.dry_runs WHERE id='" + result["dryRun"]["dryRunId"] + "'")
            assert present == ("t" if inventory else "f"), (kind, present)
            assert len(result["decisions"]) == count, (kind, result)
            if kind == "conjunction":
                assert len(result["signals"][0]["eventIds"]) == 2
            if kind == "absence" and "trigger" not in spec:
                assert all(not decision["signal"]["eventIds"] and decision["signal"]["absenceDeadline"] for decision in result["decisions"])
        self.rule_id = original
        for version, change in [(2, {"filter": {"types": ["fire.*"], "siteIds": [self.site_id]}}),
                                 (3, {"output": self.rule()["output"] | {"priority": "PRIORITY_P1"}})]:
            saved = self.request("POST", "/v1/rules", self.rule() | change, version=version - 1)["rule"]
            assert saved["version"] == version
            result = self.completed(self.dry_run(version=version))
            assert all(not decision["suppressed"] for decision in result["decisions"]), result
        assert self.request("GET", "/v1/rule-sets/active")["ruleSet"]["rules"][0]["version"] == 1
        self.mark("all five rule kinds, cross-source video correlation, absence deadlines, P1 and fire protection")

    def malformed(self):
        for expression in ("event.missing == true", "true; system('bad')", "1 / 0", "true ? 1 : false"):
            invalid = self.rule()
            invalid["ruleId"] = uid()
            invalid["filter"]["condition"] = expression
            saved = self.request("POST", "/v1/rules", invalid)["rule"]
            checked = self.request("POST", "/v1/rules/" + saved["ruleId"] + ":validate", {"version": 1}, version=1)
            assert not checked["valid"] and checked["violations"] and checked["rule"]["state"] == "RULE_STATE_DRAFT", checked
            self.activate([{"ruleId": saved["ruleId"], "version": 1}], 1, expected=409)
        self.request("POST", "/v1/rules", b"name: &x [*x]", content_type="application/yaml", expected=400)
        self.request("POST", "/v1/rules", b'{"unexpected":true}', expected=400)
        self.request("GET", "/v1/rules?pageSize=201", expected=400)
        self.request("GET", "/v1/rules?unknown=value", expected=400)
        self.request("GET", "/v1/rule-sets/active?unknown=value", expected=400)
        self.request("GET", "/v1/dry-runs/invalid", expected=400)
        self.request("GET", "/v1/unknown", expected=404)
        self.request("POST", "/v1/rules/" + self.rule_id + ":dry-run", {"version": 1, "from": stamp(self.origin), "to": stamp(self.origin - 1)}, expected=400)
        self.mark("syntax, typing, unsupported calls, YAML aliases, unknown fields and request limits reject safely")

    def concurrent_activation(self):
        results = []
        for service in ("rule-management", "rule-manager-2"):
            self.ports.pop(service, None)
            self.port(service, 8443)
        self.token("admin")
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as pool:
            futures = [pool.submit(self.activate, [self.reference], 1, service=service, expected=(200, 412))
                       for service in ("rule-management", "rule-manager-2")]
            results = [future.result() for future in futures]
        assert sorted(status for status, _ in results) == [200, 412], results
        assert self.request("GET", "/v1/rule-sets/active")["ruleSet"]["version"] == "2"
        self.request("POST", "/v1/rules/" + self.rule_id + ":archive", {}, version=1, expected=412)
        self.request("POST", "/v1/rules/" + self.rule_id + ":archive", {}, version=3)
        active = self.request("GET", "/v1/rule-sets/active")["ruleSet"]
        assert active["version"] == "3" and active["rules"] == []
        self.activate([self.reference], 3, expected=409)
        assert self.request("GET", "/v1/rules/" + self.rule_id + "?version=1")["rule"]["state"] == "RULE_STATE_ARCHIVED"
        self.mark("concurrent activation has one winner; archive publishes an empty set and preserves old versions")

    def recovery(self):
        if self.preset == "coverage":
            # Persist instrumentation counters before deliberate process termination.
            self.compose("stop", "-t", "30", "rule-management")
            self.compose("up", "-d", "rule-management")
            self.wait(lambda: self.request("GET", "/v1/rules"), 60)
        self.compose("stop", "-t", "30", "rule-manager-2")
        self.compose("pause", "kafka-tls")
        try:
            job = self.dry_run()
            self.wait(lambda: self.sql("SELECT count(*) FROM rules.dry_runs WHERE id='" + job["dryRunId"] + "' AND lease IS NOT NULL") == "1", 5)
            self.compose("kill", "-s", "KILL", "rule-management")
        finally:
            self.compose("unpause", "kafka-tls")
        self.compose("up", "-d", "rule-management")
        result = self.completed(job)
        assert len(result["decisions"]) == 4
        self.mark("SIGKILL after job claim recovers through the durable lease and private input snapshot")

    def access_denials(self):
        for identity in ("replay", "anonymous"):
            denied = self.probe("deny", identity)
            assert len(denied) == 4
        assert self.probe("deny-offsets", "replay")["offsetCommitDenied"]
        denied = subprocess.run(self.compose_cmd + ["exec", "-T", "postgres", "psql", "-U", "postgres", "-d", "psim", "-v", "ON_ERROR_STOP=1", "-c", "SET ROLE rule_management; UPDATE catalog.rules_test_sentinel SET value=1"], text=True, capture_output=True)
        assert denied.returncode != 0 and "permission denied" in denied.stderr.lower(), denied
        assert self.sql("SELECT value FROM catalog.rules_test_sentinel") == "0"
        for statement in ("INSERT INTO default.events(tenant_id) VALUES('denied')", "CREATE TABLE forbidden(value UInt8) ENGINE=Memory"):
            denied = subprocess.run(self.compose_cmd + ["exec", "-T", "clickhouse", "clickhouse-client", "--user", "rule_replay", "--password", "psim-dev-rule-clickhouse", "-q", statement], text=True, capture_output=True)
            assert denied.returncode != 0 and any(text in denied.stderr.lower() for text in ("readonly", "not enough privileges")), denied
        self.mark("Kafka write ACL, private PostgreSQL role and read-only ClickHouse user deny production mutations")

    def finish(self):
        self.wait(lambda: self.sql("SELECT count(*) FROM rules.psim_outbox WHERE published_at IS NULL") == "0", 30)
        observed = self.probe("observe", name="finished.json")
        assert [item["ruleSet"]["version"] for item in observed["sets"]] == ["1", "2", "3"], observed
        assert observed["audit"].get("rule.dry_run_completed", 0) >= 10, observed
        assert observed["audit"].get("rule.archived", 0) == 1, observed
        for topic in ("psim.signals.v1", "psim.incidents.events.v1", "psim.commands.v1", "psim.response.events.v1"):
            assert not any(observed["ends"][topic].values()), (topic, observed)
        self.report["passed"] = True
        self.mark("outbox drained, active-set versions published exactly once and production outputs remain empty")

    def cleanup(self):
        subprocess.run(self.compose_cmd + ["unpause", "kafka-tls"], stdout=self.log, stderr=self.log)
        self.compose("stop", "-t", "30", "rule-management", "rule-manager-2")
        logs = self.compose("logs", "--no-color", "rule-management", "rule-manager-2")
        (self.output / "servers.log").write_text(logs)
        if self.report["passed"]:
            assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs), "sanitizer finding in service logs"
        if self.owned and os.environ.get("PSIM_RULES_KEEP_ENV") != "1":
            self.compose("--profile", "rules-test", "--profile", "security", "down", "-v", "--remove-orphans", "--timeout", "10")
        self.log.close()


def main():
    project, preset = sys.argv[1:]
    if not re.fullmatch(r"[a-z0-9][a-z0-9_-]*", project) or preset not in ("debug", "release", "asan", "tsan", "coverage"):
        raise SystemExit("invalid project or build preset")
    check = Check(project, preset)
    try:
        check.prepare()
        check.basic()
        check.replay_checks()
        check.models()
        check.malformed()
        check.concurrent_activation()
        check.recovery()
        check.access_denials()
        check.finish()
    finally:
        try:
            check.cleanup()
        except BaseException:
            check.report["passed"] = False
            raise
        finally:
            (check.output / "result.json").write_text(json.dumps(check.report, indent=2) + "\n")
    print(json.dumps(check.report, indent=2))


if __name__ == "__main__":
    main()
