"""Verify durable device commands through real HTTPS services, Kafka, gateway and SDK."""
import concurrent.futures
import datetime as dt
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import time
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
incident_helpers = runpy.run_path(str(ROOT / "tools/incidents/check.py"))
IncidentCheck, uid = incident_helpers["Check"], incident_helpers["uid"]


class Check(IncidentCheck):
    def __init__(self, project, preset, count):
        super().__init__(project, preset, count)
        self.tenant = "11111111-1111-4111-8111-111111111111"
        self.device, self.device_type = uid(), uid()
        self.connector = "22222222-2222-4222-8222-222222222222"
        previous = self.config
        services = json.loads(self.override.read_text())["services"]
        self.log.close()
        self.output = ROOT / "build/commands" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.config = self.output / "incident.json"
        self.secret = self.output / "directory.secret"
        services = json.loads(json.dumps(services).replace(str(previous.relative_to(ROOT)), str(self.config.relative_to(ROOT))))
        config = self.output / "service.json"
        base = services["incident-service"] | {"profiles": ["command-test"], "command": [f"/repo/build/debian13/{preset}/services/command-service/psim-command-service", "--config", "/repo/" + str(config.relative_to(ROOT))]}
        services.update({"command-service": base, "command-2": base | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": "command-2"}}})
        self.fixture = self.output / ("fixture-" + self.fixtures)
        self.fixture.mkdir()
        fixture_config = self.fixture / "manifest.json"
        fixture_config.write_text(json.dumps({"directory": "/repo/" + str(self.fixture.relative_to(ROOT))}))
        services["command-fixture"] = base | {"ports": [], "read_only": False, "volumes": [str(ROOT) + ":/repo"], "working_dir": "/repo", "command": [f"/repo/build/debian13/{preset}/tools/commands/psim-command-probe", "fixture", "/repo/" + str(fixture_config.relative_to(ROOT))]}
        self.override = self.output / "compose.json"
        self.override.write_text(json.dumps({"services": services}, indent=2))
        if preset == "tsan":
            self.compose_cmd[-2:-2] = ["-f", str(ROOT / "deploy/local/compose.sanitizer.yaml")]
        self.compose_cmd[-1] = str(self.override)
        self.compose_cmd += ["--profile", "command-test", "--profile", "event-flow"]
        self.command_config = config
        self.report = {"passed": False, "commands": count, "checks": []}

    def cq(self, method, path="", body=None, **options):
        return self.request(method, "/v1/commands" + path, body, service=options.pop("service", "command-service"), **options)

    def send(self, capability="arm", role="operator", **options):
        body = {"commandId": uid(), "deviceId": self.device, "capability": capability, "incidentId": self.incident_id}
        body.update(options.pop("fields", {}))
        return self.cq("POST", body=body, role=role, key=body["commandId"], **options)

    def current(self, identity):
        return self.cq("GET", "/" + identity)["command"]

    def final(self, identity, state="EXECUTED"):
        expected = "COMMAND_STATE_" + state
        return self.wait(lambda: value if (value := self.current(identity))["state"] == expected else None, 45)

    def emit_records(self, records=None, **options):
        path = self.output / ("probe-" + uid() + ".json")
        path.write_text(json.dumps({"records": records or []} | options))
        self.env_command(f"build/debian13/{self.preset}/tools/commands/psim-command-probe emit /repo/{path.relative_to(ROOT)}")

    def executions(self):
        path = self.fixture / "executions.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def prepare(self):
        super().prepare()
        self.compose("up", "-d", "--wait", "--wait-timeout", "120", "valkey")
        self.compose("up", "-d", "--force-recreate", "--wait", "--wait-timeout", "120", "connector-gateway")
        bootstrap = self.fixture / "bootstrap"
        bootstrap.mkdir()
        for source in (ROOT / ".cache/event-flow").iterdir():
            if source.suffix in (".pem", ".key"):
                target = bootstrap / source.name
                target.write_bytes(source.read_bytes())
                target.chmod(source.stat().st_mode & 0o777)
        self.env_command(f"python3 -I tools/env/gateway_bootstrap.py http://keycloak:8080 connector-gateway:8080 build/debian13/{self.preset}/tools/gateway-admin/psim-gateway-admin {bootstrap.relative_to(ROOT)}")
        self.command_config.write_text(json.dumps({"service": {"instance": "command-1"}, "runtime": {"shards": 1}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001}, "settings": {"allow_plaintext": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "redis_password_file": "/repo/.cache/event-flow/redis.secret", "listen_port": 8443, "issuer": "http://keycloak:8080/realms/psim", "command_timeout_seconds": 12}}, indent=2))
        self.compose("up", "-d", "--force-recreate", "command-service", "command-2", "command-fixture")
        self.wait(lambda: (self.fixture / "ready").exists(), 90)
        self.zone, self.type_id = uid(), uid()
        self.type_body = {"incidentTypeId": self.type_id, "code": self.fixtures, "name": "Command incident", "defaultPriority": "PRIORITY_P2", "groupingWindow": "60s", "autoCloseAfter": "300s"}
        self.request("POST", "/v1/incident-types", self.type_body, role="admin", expected=201)
        self.emit(count=1, group="commands-" + self.fixtures)
        self.incident_id = self.wait(lambda: next((item["incidentId"] for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == "commands-" + self.fixtures), None))
        capabilities = [{"code": name, "critical": name == "unlock"} for name in ("arm", "unlock", "hold", "reject")]
        capabilities.append({"code": "preset", "parameters": [{"name": "index", "type": "PARAMETER_TYPE_INTEGER", "required": True, "allowedValues": ["1", "2"]}]})
        self.device_body = {"deviceId": self.device, "deviceTypeId": self.device_type, "siteId": self.site_id, "zoneId": self.zone, "connectorId": self.connector, "externalId": "command-device", "state": "RESOURCE_STATE_ACTIVE", "version": "1"}
        self.type_body_command = {"deviceTypeId": self.device_type, "code": "command-test", "capabilities": capabilities, "state": "RESOURCE_STATE_ACTIVE", "version": "1"}
        self.emit_records([{"topic": "psim.catalog.v1", "key": "device_type:" + self.device_type, "value": {"envelope": {"tenantId": self.tenant}, "deviceType": self.type_body_command}}, {"topic": "psim.catalog.v1", "key": "device:" + self.device, "value": {"envelope": {"tenantId": self.tenant}, "device": self.device_body}}])
        self.wait(lambda: self.sql("SELECT count(*) FROM command.resources WHERE tenant='" + self.tenant + "' AND id IN ('" + self.device + "','" + self.device_type + "','" + self.incident_id + "','" + self.connector + "')") == "4", 90)
        self.mark("two authenticated HTTPS services, independent projections, real mTLS gateway and durable SDK")

    def basics_commands(self):
        self.cq("GET", role=None, expected=401)
        self.send("missing", expected=400)
        self.send("preset", expected=400)
        self.send("preset", fields={"parameters": {"index": "abc"}}, expected=400)
        self.send("arm", fields={"parameters": {"extra": "x"}}, expected=400)
        self.send(role="other", expected=403)
        self.send(role="auditor", expected=403)
        assert self.cq("GET", role="other")["commands"] == []
        assert self.cq("GET", role="empty")["commands"] == []
        self.cq("GET", "?unknown=x", expected=400)
        self.cq("GET", "?states=COMMAND_STATE_UNSPECIFIED", expected=400)
        self.cq("GET", "?pageSize=1001", expected=400)
        body = {"commandId": uid(), "deviceId": self.device, "capability": "arm", "incidentId": self.incident_id}
        self.cq("POST", body=body, key=uid(), expected=400)
        value = self.send("preset", fields={"parameters": {"index": "1"}})["command"]
        value = self.final(value["commandId"])
        assert value["finishedAt"]
        identity = value["commandId"]
        body = {"commandId": identity, "deviceId": self.device, "capability": "preset", "parameters": {"index": "1"}, "incidentId": self.incident_id}
        assert self.cq("POST", body=body, key=identity, service="command-2")["command"] == value
        self.cq("POST", body=body | {"capability": "arm"}, key=identity, expected=422)
        self.cq("POST", body=body, key=identity, role="bob", expected=422)
        self.cq("GET", "/" + identity, role="other", expected=403)
        self.cq("GET", "/" + identity, role="tenant", expected=404)
        self.emit_records(duplicate=identity)
        assert len([item for item in self.executions() if item["id"] == identity]) == 1
        self.final(self.send("reject")["command"]["commandId"], "FAILED")
        self.wait(lambda: any(entry.get("refId") == identity and entry["kind"] == "TIMELINE_ENTRY_KIND_COMMAND" for entry in self.request("GET", "/v1/incidents/" + self.incident_id + "/timeline")["entries"]))
        self.mark("catalog parameters, access scopes, repeat requests and Kafka delivery, execution result and incident timeline")

    def approvals(self):
        own = self.send("unlock", role="chief")["command"]
        assert own["state"] == "COMMAND_STATE_AWAITING_APPROVAL" and own["critical"]
        path = "/" + own["commandId"] + ":approve"
        self.cq("POST", path, {}, role="chief", version=own["version"], expected=403)
        self.cq("POST", path, {}, role="operator", version=own["version"], expected=403)
        pending = self.send("unlock")["command"]
        path = "/" + pending["commandId"] + ":approve"
        self.cq("POST", path, {}, role="chief", expected=428)
        self.cq("POST", path, {}, role="chief", version=999, expected=412)
        key = uid()
        self.cq("POST", path, {}, role="chief", version=pending["version"], key=key)
        value = self.final(pending["commandId"])
        assert value["requestedBy"]["id"] != value["approvedBy"]["id"]
        assert self.cq("POST", path, {}, role="chief", version=pending["version"], key=key)["command"] == value
        cancelled = self.send("unlock")["command"]
        self.cq("POST", "/" + cancelled["commandId"] + ":cancel", {}, version=cancelled["version"])
        self.final(cancelled["commandId"], "CANCELLED")
        rejected = self.send("unlock")["command"]
        self.cq("POST", "/" + rejected["commandId"] + ":reject", {"reason": "unsafe"}, role="chief", version=rejected["version"])
        self.final(rejected["commandId"], "REJECTED")
        self.cq("POST", "/" + value["commandId"] + ":cancel", {}, version=value["version"], expected=409)
        self.final(own["commandId"], "TIMED_OUT")
        self.mark("criticality from catalog, separate approver, transactional approval, cancellation, rejection and approval deadline")

    def failures_commands(self):
        hold = self.send("hold")["command"]
        self.wait(lambda: any(item["id"] == hold["commandId"] for item in self.executions()))
        timed = self.final(hold["commandId"], "TIMED_OUT")
        (self.fixture / "release").touch()
        assert timed["resultCode"] == "COMMAND_TIMED_OUT"
        self.final(self.send()["command"]["commandId"])
        (self.fixture / "release").unlink()
        hold = self.send("hold")["command"]
        self.wait(lambda: any(item["id"] == hold["commandId"] for item in self.executions()))
        self.compose("kill", "-s", "SIGKILL", "command-fixture")
        unknown = self.final(hold["commandId"], "UNKNOWN")
        assert unknown["resultCode"] == "COMMAND_OUTCOME_UNKNOWN"
        offline = self.final(self.send()["command"]["commandId"], "FAILED")
        assert offline["resultCode"] == "COMMAND_CONNECTOR_OFFLINE"
        self.compose("up", "-d", "command-fixture")
        self.wait(lambda: self.sql("SELECT count(*) FROM ingest.sessions WHERE connector_id='" + self.connector + "' AND online AND expires_at>clock_timestamp()") == "1", 60)
        self.emit_records(duplicate=hold["commandId"])
        self.final(self.send()["command"]["commandId"])
        assert len([item for item in self.executions() if item["id"] == hold["commandId"]]) == 1
        pending = self.send("unlock")["command"]
        self.compose("kill", "-s", "SIGKILL", "command-service", "command-2")
        self.compose("up", "-d", "command-service", "command-2")
        self.final(pending["commandId"], "TIMED_OUT")
        self.mark("execution timeout, late result, session loss, offline connector, fenced replay after reconnect and restored deadline after SIGKILL")

    def projections_and_results(self):
        waiting = self.send("unlock")["command"]
        def publish_type(value):
            self.emit_records([{"topic": "psim.catalog.v1", "key": "device_type:" + self.device_type, "value": {"envelope": {"tenantId": self.tenant}, "deviceType": value}}])
        disabled = self.type_body_command | {"state": "RESOURCE_STATE_DISABLED", "version": "2"}
        publish_type(disabled)
        self.wait(lambda: self.sql("SELECT version FROM command.resources WHERE tenant='" + self.tenant + "' AND id='" + self.device_type + "'") == "2")
        publish_type(self.type_body_command)
        self.send(expected=400)
        self.cq("POST", "/" + waiting["commandId"] + ":approve", {}, role="chief", version=waiting["version"], expected=400)
        publish_type(self.type_body_command | {"version": "3"})
        self.wait(lambda: self.sql("SELECT version FROM command.resources WHERE tenant='" + self.tenant + "' AND id='" + self.device_type + "'") == "3")
        self.cq("POST", "/" + waiting["commandId"] + ":approve", {}, role="chief", version=waiting["version"])
        value = self.final(waiting["commandId"])
        session = self.sql("SELECT session FROM command.commands WHERE tenant='" + self.tenant + "' AND id='" + value["commandId"] + "'")
        def result(identity, session_id, **options):
            return {"topic": "psim.commands.results.v1", "key": identity, "repeat": 3, "value": {"envelope": {"tenantId": self.tenant, "actor": {"kind": "ACTOR_KIND_CONNECTOR", "id": self.connector}}, "sessionId": session_id, "commandOutcome": {"commandId": identity, "success": True} | options}}
        self.emit_records([result(value["commandId"], session), result(uid(), session)])
        def ignored():
            total = 0
            for service in ("command-service", "command-2"):
                with urllib.request.urlopen("http://localhost:" + self.port(service, 9100) + "/metrics", timeout=5) as response:
                    text = response.read().decode()
                found = re.search(r"^psim_command_results_ignored_total (\d+)", text, re.M)
                total += int(found[1]) if found else 0
            return total >= 6
        self.wait(ignored, 60)
        assert self.current(value["commandId"]) == value
        self.emit_records([result(value["commandId"], uid(), success=False)])
        assert self.current(value["commandId"]) == value
        before = self.incident(self.incident_id)
        versions = (int(value["version"]) + 10, int(value["version"]) + 9)
        records = []
        for version in versions:
            command = value | {"version": str(version), "state": "COMMAND_STATE_SENT"}
            command.pop("finishedAt", None)
            records.append({"topic": "psim.response.events.v1", "key": self.incident_id, "repeat": 3, "value": {"envelope": {"tenantId": self.tenant, "sequence": str(version), "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "command-service"}}, "commandUpdated": {"command": command}}})
        self.emit_records(records)
        self.wait(lambda: self.sql("SELECT count(*) FROM incident.command_versions WHERE tenant='" + self.tenant + "' AND id='" + value["commandId"] + "' AND version IN (" + ",".join(map(str, versions)) + ")") == "2")
        assert self.incident(self.incident_id) == before
        self.mark("monotonic catalog versions, capability recheck, ignored results, session validation and every delayed command version in timeline without incident changes")

    def outbox_recovery(self):
        lock = subprocess.Popen(self.compose_cmd + ["exec", "-T", "-e", "PGAPPNAME=psim-command-lock", "postgres", "psql", "-U", "psim", "-d", "psim", "-At", "-c", "SELECT pg_advisory_lock(hashtextextended('command:outbox',0)); SELECT pg_sleep(30)"], cwd=ROOT, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            self.wait(lambda: self.sql("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND granted AND pid IN (SELECT pid FROM pg_stat_activity WHERE application_name='psim-command-lock')") == "1")
            identity = uid()
            body = {"commandId": identity, "deviceId": self.device, "capability": "arm", "incidentId": self.incident_id}
            pending = self.cq("POST", body=body, key=identity)["command"]
            assert pending["state"] == "COMMAND_STATE_SENT"
            assert int(self.sql("SELECT count(*) FROM command.psim_outbox WHERE topic='psim.commands.v1' AND published_at IS NULL")) >= 1
            assert not any(item["id"] == identity for item in self.executions())
            self.compose("kill", "-s", "SIGKILL", "command-service")
        finally:
            self.sql("SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='psim-command-lock'")
            lock.communicate(timeout=10)
        value = self.wait(lambda: current if (current := self.cq("GET", "/" + identity, service="command-2")["command"])["state"] == "COMMAND_STATE_EXECUTED" else None, 45)
        assert self.cq("POST", body=body, key=identity, service="command-2")["command"] == value
        assert len([item for item in self.executions() if item["id"] == identity]) == 1
        self.compose("up", "-d", "command-service")
        self.wait(lambda: self.current(identity))
        self.mark("SIGKILL after command commit before publication: second instance drains durable outbox with one execution")

    def performance_commands(self):
        def stable_group():
            state = self.compose("exec", "-T", "kafka-1", "/opt/kafka/bin/kafka-consumer-groups.sh", "--bootstrap-server", "kafka-1:19092", "--describe", "--group", "psim-command-service", "--state")
            return re.search(r"^psim-command-service\s+.*\bStable\s+2(?:\s|$)", state, re.M | re.I)
        self.wait(stable_group, 60)
        starts = {}
        rate = 10
        origin = time.monotonic()
        def send(index):
            delay = origin + index / rate - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            identity = uid()
            body = {"commandId": identity, "deviceId": self.device, "capability": "arm", "incidentId": self.incident_id}
            starts[identity] = time.time()
            self.cq("POST", body=body, key=identity, service="command-service" if index % 2 else "command-2")
        with concurrent.futures.ThreadPoolExecutor(max_workers=2) as executor:
            list(executor.map(send, range(self.count)))
        def reconcile():
            entries = {entry["id"]: entry for entry in self.executions() if entry["id"] in starts}
            return entries if len(entries) == len(starts) else None
        entries = self.wait(reconcile, 90)
        latency = sorted((dt.datetime.fromisoformat(entries[identity]["at"].replace("Z", "+00:00")).timestamp() - start) * 1000 for identity, start in starts.items())
        p99 = latency[min(len(latency) - 1, int(len(latency) * 0.99))]
        assert len([item for item in self.executions() if item["id"] in starts]) == self.count
        (self.output / "delivery.json").write_text(json.dumps({"expected": sorted(starts), "observed": sorted(entries), "latency_ms": latency}, indent=2))
        self.report["unique_executions"] = len(entries)
        self.report["duplicate_executions"] = len([item for item in self.executions() if item["id"] in starts]) - len(entries)
        self.report["requests_per_second"] = rate
        self.report["offered_requests_per_second"] = (len(starts) - 1) / (max(starts.values()) - min(starts.values()))
        self.report["request_to_connector_p99_ms"] = p99
        self.wait(lambda: set(starts).issubset(set(self.sql("SELECT id::text FROM command.commands WHERE tenant='" + self.tenant + "' AND body->>'state'='COMMAND_STATE_EXECUTED'").splitlines())), 60)
        if self.preset == "release":
            assert self.report["offered_requests_per_second"] >= rate * 0.95
            assert p99 <= 200, p99
        self.wait(lambda: self.sql("SELECT count(*) FROM command.psim_outbox WHERE published_at IS NULL") == "0", 60)
        assert int(self.sql("SELECT count(*) FROM command.psim_outbox WHERE topic='psim.audit.v1'")) > self.count
        self.mark(f"two-instance delivery: {self.count} unique executions, p99={p99:.2f} ms, no duplicate effects, durable audit")

    def cleanup(self):
        try:
            (self.fixture / "release").touch()
            (self.fixture / "stop").touch()
            self.compose("stop", "-t", "35", "command-service", "command-2", "command-fixture", "connector-gateway")
            for service in ("command-service", "command-2"):
                identity = self.compose("ps", "-aq", service).strip()
                if identity:
                    state = json.loads(self.run(["docker", "inspect", identity]))[0]["State"]
                    assert not state["Running"] and state["ExitCode"] == 0, (service, state)
            logs = self.compose("logs", "--no-color", "command-service", "command-2", "command-fixture", "connector-gateway")
            (self.output / "command-servers.log").write_text(logs)
            if re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs):
                self.report["passed"] = False
                raise AssertionError("command sanitizer diagnostics")
        finally:
            super().cleanup()


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.failures_commands()
        check.outbox_recovery()
        check.basics_commands()
        check.approvals()
        check.projections_and_results()
        check.performance_commands()
        check.report["passed"] = True
    finally:
        check.cleanup()


if __name__ == "__main__":
    main()
