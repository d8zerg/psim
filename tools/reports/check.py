"""Verify report projections, real SLA publication, authorization and PostgreSQL rebuilds."""
import csv
import io
import json
import os
from pathlib import Path
import re
import runpy
import ssl
import sys
import tempfile
import time
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(ROOT / "tools/history/check.py"))
HistoryCheck, uid, stamp, TASK = (helpers[k] for k in ("Check", "uid", "stamp", "TASK"))


class Check(HistoryCheck):
    def __init__(self, project, preset, count, benchmark=False):
        super().__init__(project, preset, count)
        self.env["PSIM_OIDC_ISSUER"] = "http://keycloak:8080/realms/psim"
        self.log.close()
        self.output = ROOT / "build/reports" / (preset + ("-bench" if benchmark else ""))
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        services = json.loads(self.override.read_text())["services"]
        base = services.pop("event-history")
        services.pop("history-2")
        services["projection-service"] = base | {"command": [f"/repo/build/debian13/{preset}/services/projection-service/psim-projection-service", "--config", "/repo/" + str(self.output.relative_to(ROOT)) + "/service.json"]}
        services["reports-2"] = services["projection-service"] | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": "reports-2"}}
        services["response-engine"] = base | {"command": [f"/repo/build/debian13/{preset}/services/response-engine/psim-response-engine", "--config", "/repo/" + str(self.output.relative_to(ROOT)) + "/response.json"]}
        self.override = self.output / "compose.json"
        self.override.write_text(json.dumps({"services": services}, indent=2))
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml"), "-f", str(self.override)]
        self.history_apps = ["projection-service", "reports-2"]
        self.apps = self.history_apps + ["incident-service", "response-engine"]
        self.type_id, self.policy = uid(), uid()
        self.report = {"passed": False, "checks": [], "count": count}
        self.from_ms = int(time.time() * 1000) - 30 * 86400000
        self.to_ms = int(time.time() * 1000) + 1000
        self.interval = {"filter.from": stamp(self.from_ms), "filter.to": stamp(self.to_ms)}

    def rq(self, role="chief", grouping="REPORT_GROUPING_SITE", export=False, service="projection-service", expected=200, **filters):
        path = "/v1/reports/incidents" + (":export" if export else "")
        return self.request("GET", path + "?" + urllib.parse.urlencode(self.interval | {"grouping": grouping} | filters, doseq=True), role=role, service=service, expected=expected)

    def prepare(self):
        self.env_command("sh tools/env/event_flow_tls.sh", False)
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem")
        secrets = ROOT / ".cache/processing"
        secrets.mkdir(parents=True, exist_ok=True)
        with tempfile.NamedTemporaryFile(mode="w", dir=secrets, delete=False) as temporary:
            temporary.write("psim-dev-clickhouse")
        os.chmod(temporary.name, 0o400)
        os.replace(temporary.name, secrets / "clickhouse.password")
        (ROOT / f"build/debian13/{self.preset}/profiles").mkdir(parents=True, exist_ok=True)
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keycloak", "keeper", "clickhouse", "otel-collector")
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.env_command("python3 -I tools/env/keycloak_security.py http://keycloak:8080")
        self.principals()
        self.report_scopes()
        common = {"service": {"instance": "reports-fixture"}, "runtime": {"shards": 1, "blocking_threads": 8}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.01},
                  "settings": {"allow_plaintext": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "listen_port": 8443, "issuer": "http://keycloak:8080/realms/psim"}}
        config = json.loads(json.dumps(common))
        config["settings"]["console_enabled"] = False
        config["settings"].update({"clickhouse_host": "clickhouse", "clickhouse_port": 9000, "clickhouse_password_file": "/repo/.cache/processing/clickhouse.password"})
        (self.output / "service.json").write_text(json.dumps(config))
        (self.output / "response.json").write_text(json.dumps(common))
        self.incident_config.write_text(json.dumps(common | {"settings": common["settings"] | {"legacy_types": True}}))
        self.compose("up", "-d", "--force-recreate", *self.apps)
        self.wait(lambda: self.rq().get("totals", {}).get("incidentsCreated", "0") == "0", 90)
        self.wait(lambda: self.pg("SELECT to_regclass('escalation.incidents')") == "escalation.incidents", 90)
        self.wait(lambda: self.pg("SELECT to_regclass('incident.incidents')") == "incident.incidents", 90)
        policy = {"policyId": self.policy, "name": "Report targets", "sla": [{"priority": "PRIORITY_P2", "acknowledgeWithin": "60s", "resolveWithin": "120s"}], "levels": [{"level": 1, "delay": "1s"}]}
        self.pg(f"INSERT INTO escalation.policies VALUES('{self.tenant}','{self.policy}', '{json.dumps(policy)}'::jsonb)")
        self.mark("two report instances with HTTPS, verified JWTs and real source migrations")

    def report_scopes(self):
        admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        clients = "/admin/realms/psim/clients"
        resource = self.keycloak("GET", clients + "?clientId=psim", admin)[0]["id"]
        role = self.keycloak("GET", clients + "/" + resource + "/roles/supervisor", admin)
        chief_id = "psim-history-" + self.fixtures + "-chief"
        reference = self.keycloak("GET", clients + "?clientId=" + chief_id, admin)[0]
        manager = {key: reference[key] for key in ("enabled", "serviceAccountsEnabled", "standardFlowEnabled", "publicClient", "protocol")}
        manager["protocolMappers"] = [{key: value for key, value in item.items() if key != "id"} for item in reference["protocolMappers"]]
        manager["clientId"] = "psim-reports-manager-" + self.fixtures
        manager["secret"] = "psim-dev-catalog"
        self.keycloak("POST", clients, admin, manager)
        manager_id = self.keycloak("GET", clients + "?clientId=" + manager["clientId"], admin)[0]["id"]
        account = self.keycloak("GET", clients + "/" + manager_id + "/service-account-user", admin)["id"]
        manager_role = self.keycloak("GET", clients + "/" + resource + "/roles/manager", admin)
        self.keycloak("POST", "/admin/realms/psim/users/" + account + "/role-mappings/clients/" + resource, admin, [manager_role])
        self.tokens["manager"] = {"client": manager["clientId"], "value": "", "expires": 0}
        for name in ("other", "tenant", "zone", "empty"):
            client_id = "psim-history-" + self.fixtures + "-" + name
            client = self.keycloak("GET", clients + "?clientId=" + client_id, admin)[0]["id"]
            account = self.keycloak("GET", clients + "/" + client + "/service-account-user", admin)["id"]
            self.keycloak("POST", "/admin/realms/psim/users/" + account + "/role-mappings/clients/" + resource, admin, [role])

    def send(self, records, **options):
        path = self.output / ("input-" + uid() + ".json")
        path.write_text(json.dumps({"records": records} | options))
        return self.env_command(f"build/debian13/{self.preset}/tools/reports/psim-report-probe /repo/{path.relative_to(ROOT)}")

    def record(self, incident, version=None):
        envelope = {"messageId": uid(), "tenantId": self.tenant, "sequence": str(version or incident["version"]), "schemaVersion": "1", "occurredAt": stamp(int(time.time() * 1000))}
        return {"key": incident["incidentId"], "record": {"envelope": envelope, "incidentCreated": {"incident": incident, "signalId": uid()}}}

    def basics(self):
        self.incidents = []
        for index in range(10):
            created = self.from_ms + 86400000 + index
            incident = {"incidentId": uid(), "incidentTypeId": self.type_id, "siteId": self.site_id, "zoneId": self.zone,
                        "state": "INCIDENT_STATE_RESOLVED", "priority": "PRIORITY_P2", "createdAt": stamp(created),
                        "acknowledgedAt": stamp(created + (30000 if index % 2 == 0 else 90000)), "resolvedAt": stamp(created + 120000),
                        "assigneeId": "operator-a" if index < 6 else "operator-b", "resolution": "RESOLUTION_FALSE_ALARM" if index < 2 else "RESOLUTION_CONFIRMED",
                        "version": "2", "escalationPolicyId": self.policy, "escalationLevel": 1 if index == 0 else 0}
            self.incidents.append(incident)
            body = json.dumps(incident)
            self.pg(f"INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) VALUES('{self.tenant}','{incident['incidentId']}','{incident['incidentId']}',0,'{body}'::jsonb)")
        self.send([self.record(i) for i in self.incidents], repeat=3)
        self.wait(lambda: int(self.rq().get("totals", {}).get("incidentsCreated", 0)) == 10)
        self.wait(lambda: self.ch(f"SELECT count() FROM incident_report_current WHERE tenant_id='{self.tenant}' AND ack_target_ms=60000") == "10")
        self.wait(lambda: self.rq()["totals"].get("ackSlaEvaluated") == "10")
        value = self.rq()["totals"]
        assert value["incidentsResolved"] == "10" and value["mtta"] == "60s" and value["mttr"] == "120s", value
        assert value["falseAlarmRate"] == 0.2 and value["ackSlaCompliance"] == 0.5 and value["resolveSlaCompliance"] == 1, value
        assert value["escalated"] == "1"
        assert self.rq(grouping="REPORT_GROUPING_OPERATOR")["rows"][0]["incidentsCreated"] == "6"
        assert self.rq(service="reports-2")["totals"] == value
        for grouping in ("REPORT_GROUPING_INCIDENT_TYPE", "REPORT_GROUPING_PRIORITY", "REPORT_GROUPING_DAY", "REPORT_GROUPING_NONE"):
            assert self.rq(grouping=grouping)["totals"] == value
        assert self.rq(**{"filter.incidentTypeIds": [self.type_id], "filter.priorities": ["PRIORITY_P2"]})["totals"] == value
        self.expected = value
        self.mark("exact counts, MTTA, MTTR, false alarms, original SLA targets, escalation and operator load despite duplicates")
        # An older aggregate cannot override its successor.
        old = self.incidents[0] | {"version": "1", "state": "INCIDENT_STATE_NEW"}
        old.pop("resolvedAt"); old.pop("acknowledgedAt")
        self.send([self.record(old)])
        assert self.rq()["totals"] == value
        self.send([self.record(self.incidents[0]) | {"malformed": True}])
        self.wait(lambda: self.pg("SELECT count(*) FROM psim_clickhouse.pending_batches") == "0")
        self.mark("stale versions and malformed input do not change report totals")

    def lifecycle(self):
        reopened = self.incidents[0] | {"version": "3", "state": "INCIDENT_STATE_IN_PROGRESS", "assigneeId": "operator-b"}
        reopened.pop("resolvedAt")
        reopened.pop("resolution")
        self.incidents[0] = reopened
        self.pg(f"UPDATE incident.incidents SET body='{json.dumps(reopened)}'::jsonb WHERE incident_id='{reopened['incidentId']}'")
        self.send([self.record(reopened)])
        self.wait(lambda: self.rq()["totals"]["incidentsResolved"] == "9")
        result = self.rq()["totals"]
        assert abs(result["falseAlarmRate"] - 1 / 9) < 1e-12, result
        assert result["resolveSlaCompliance"] == 0.9 and result["mttr"] == "120s", result
        assert self.rq(grouping="REPORT_GROUPING_OPERATOR")["rows"][0]["incidentsCreated"] == "5"
        self.pg(f"UPDATE escalation.policies SET body=jsonb_set(body,'{{sla,0,acknowledgeWithin}}','\"999s\"'::jsonb) WHERE id='{self.policy}'")
        changed = self.incidents[1] | {"version": "3", "priority": "PRIORITY_P4"}
        self.incidents[1] = changed
        self.pg(f"UPDATE incident.incidents SET body='{json.dumps(changed)}'::jsonb WHERE incident_id='{changed['incidentId']}'")
        self.send([self.record(changed)])
        self.wait(lambda: self.rq(grouping="REPORT_GROUPING_PRIORITY")["rows"][-1]["group"] == "4")
        assert self.rq()["totals"] == result
        self.expected = result
        self.mark("reopening removes the prior resolution; transfer changes operator load; policy and priority changes preserve SLA")

    def observations(self):
        now = int(time.time() * 1000)
        self.to_ms = now + 1000
        self.interval["filter.to"] = stamp(self.to_ms)
        extra = []
        for kind in ("overdue", "pending", "unconfigured"):
            incident = {"incidentId": uid(), "incidentTypeId": self.type_id, "siteId": self.site_id, "zoneId": self.zone,
                        "state": "INCIDENT_STATE_NEW", "priority": "PRIORITY_P2", "version": "1", "assigneeId": "operator-c",
                        "createdAt": stamp(now if kind == "pending" else self.from_ms + 2 * 86400000)}
            if kind != "unconfigured":
                incident["escalationPolicyId"] = self.policy
            self.pg(f"INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) VALUES('{self.tenant}','{incident['incidentId']}','{incident['incidentId']}',0,'{json.dumps(incident)}'::jsonb)")
            extra.append(incident)
        self.send([self.record(i) for i in extra], repeat=2)
        self.wait(lambda: self.rq()["totals"]["incidentsCreated"] == "13")
        self.wait(lambda: self.rq()["totals"]["ackSlaEvaluated"] == "11")
        self.wait(lambda: self.rq()["totals"]["escalated"] == "2")
        result = self.rq()["totals"]
        assert abs(result["ackSlaCompliance"] - 5 / 11) < 1e-12, result
        assert result["resolveSlaEvaluated"] == "10" and result["mtta"] == "60s", result
        self.wait(lambda: self.pg(f"SELECT count(*) FROM escalation.incidents WHERE tenant='{self.tenant}'") == "12")
        self.expected = result
        self.mark("overdue observations fail SLA; pending and unconfigured observations do not enter its denominator")

    def carryover(self):
        incident = {"incidentId": uid(), "incidentTypeId": self.type_id, "siteId": self.site_id, "zoneId": self.zone,
                    "state": "INCIDENT_STATE_RESOLVED", "priority": "PRIORITY_P2", "version": "1", "assigneeId": "operator-c",
                    "createdAt": stamp(self.from_ms - 2000), "acknowledgedAt": stamp(self.from_ms - 1000),
                    "resolvedAt": stamp(self.from_ms + 1000), "resolution": "RESOLUTION_FALSE_ALARM"}
        self.pg(f"INSERT INTO incident.incidents(tenant_id,incident_id,grouping_key,group_until_ns,body) VALUES('{self.tenant}','{incident['incidentId']}','{incident['incidentId']}',0,'{json.dumps(incident)}'::jsonb)")
        self.send([self.record(incident)])
        self.wait(lambda: self.rq()["totals"]["incidentsResolved"] == "10")
        result = self.rq()["totals"]
        assert result["incidentsCreated"] == "13" and result["mtta"] == "60s", result
        assert result["mttr"] == "108.300s" and result["falseAlarmRate"] == 0.2, result
        assert result["ackSlaEvaluated"] == "11" and result["resolveSlaEvaluated"] == "10", result
        self.expected = result
        self.mark("an incident created before the period contributes to resolution metrics without increasing created counts")

    def scopes(self):
        self.rq(role=None, expected=401)
        self.rq(role="operator", expected=403)
        self.rq(role="admin", expected=403)
        assert self.rq(role="manager")["totals"] == self.expected
        assert self.rq(role="manager", export=True).startswith(b"row_kind,")
        # Same report role, narrower scope and a foreign tenant cannot read the incident group.
        chief = self.tokens["chief"]
        self.tokens["chief"] = self.tokens["auditor"]
        try:
            assert self.rq()["totals"] == self.expected
        finally:
            self.tokens["chief"] = chief
        self.rq(role="empty", expected=403)
        assert self.rq(role="tenant").get("totals", {}).get("incidentsCreated", "0") == "0"
        assert self.rq(role="other").get("totals", {}).get("incidentsCreated", "0") == "0"
        assert self.rq(role="zone")["totals"] == self.expected
        assert self.rq(**{"filter.siteIds": [self.foreign_site]}).get("totals", {}).get("incidentsCreated", "0") == "0"
        self.rq(**{"filter.from": "invalid"}, expected=400)
        raw = self.rq(export=True)
        rows = list(csv.DictReader(io.StringIO(raw.decode())))
        assert rows[0]["incidents_created"] == self.expected["incidentsCreated"] and len(rows) == 2
        for action in ("report.read", "report.export", "access.denied"):
            self.wait(lambda action=action: int(self.pg(f"SELECT count(*) FROM reports.psim_outbox WHERE position(convert_to('{action}','UTF8') in payload)>0 AND published_at IS NOT NULL")) > 0)
        self.mark("role checks, scope filters, validated query, CSV and durable audit publication")

    def rebuild(self):
        before = self.rq()["totals"]
        # Rebuild into an empty destination and compare with the live Kafka projection.
        self.compose("stop", "-t", "35", *self.history_apps)
        for table in ("incident_facts", "incident_report_states"):
            self.ch(f"TRUNCATE TABLE {table} SYNC")
        command = f"PSIM_PG_HOST=postgres PSIM_PG_SSLMODE=disable PSIM_PG_PASSWORD=psim-dev-psim PSIM_CH_HOST=clickhouse PSIM_CH_PORT=9000 PSIM_CH_ALLOW_PLAINTEXT=1 PSIM_CH_PASSWORD_FILE=/repo/.cache/processing/clickhouse.password build/debian13/{self.preset}/services/projection-service/psim-report-tool rebuild {self.tenant} 0 {self.to_ms}"
        self.env_command(command)
        self.env_command(command)
        self.ch("TRUNCATE TABLE incident_report_states SYNC")
        self.env_command(command)
        self.compose("start", *self.history_apps)
        self.wait(lambda: self.rq()["totals"] == before, 90)
        self.mark("rebuild from a repeatable read PostgreSQL snapshot equals Kafka facts, including repeated rebuild")
        self.compose("kill", "-s", "SIGKILL", "projection-service")
        self.compose("start", "projection-service")
        self.wait(lambda: self.rq()["totals"] == before, 90)
        self.mark("SIGKILL and restart preserve report metrics")

    def benchmark(self):
        created = f"toInt64({self.from_ms}+86400000+intDiv(number*28*86400000,{self.count}))"
        self.ch(f"INSERT INTO incident_facts SELECT '{self.tenant}',toString(generateUUIDv4()),'{self.site_id}','{self.zone}','{self.type_id}','operator',toUInt64(1),toUInt8(1),toUInt8(2),toUInt8(4),toUInt8(1),toUInt32(0),{created}+30000,{created}+120000,toInt64(0),toInt64(0),fromUnixTimestamp64Milli({created}) FROM numbers({self.count})")

        durations = []
        for _ in range(5):
            start = time.monotonic()
            result = self.rq()
            durations.append(time.monotonic() - start)
            assert int(result["totals"]["incidentsCreated"]) == self.count
        assert max(durations) <= 3, durations
        self.report["benchmark"] = {"incidents": self.count, "seconds": durations}
        self.mark("report over a 30 day interval completes within three seconds")

    def cleanup(self):
        try:
            self.compose("stop", "-t", "35", *self.apps)
            logs = self.compose("logs", "--no-color", *self.apps)
            (self.output / "servers.log").write_text(logs)
            if re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs):
                self.report["passed"] = False
                raise AssertionError("service sanitizer diagnostics")
        finally:
            try:
                if self.owned and os.environ.get("PSIM_REPORTS_KEEP_ENV") != "1":
                    self.compose("--profile", "history-test", "down", "-v", "--remove-orphans", "--timeout", "10")
            finally:
                (self.output / "report.json").write_text(json.dumps(self.report, indent=2))
                self.log.close()


def main():
    benchmark = len(sys.argv) > 4 and sys.argv[4] == "bench"
    if benchmark and sys.argv[2] != "release":
        raise ValueError("report benchmark requires release")
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]), benchmark)
    try:
        check.prepare()
        if benchmark:
            check.benchmark()
        else:
            check.basics()
            check.lifecycle()
            check.observations()
            check.carryover()
            check.scopes()
            check.rebuild()
        check.report["passed"] = True
    finally:
        check.cleanup()


if __name__ == "__main__":
    main()
