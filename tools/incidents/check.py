"""Verify incidents against two real processes, Kafka, PostgreSQL and Keycloak."""
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
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/catalog"))
from check import Check as HttpCheck, TASK, uid  # noqa: E402
import check as http_helpers  # noqa: E402


class Check(HttpCheck):
    def __init__(self, project, preset, count):
        super().__init__(project, preset, count)
        self.tenant = uid()
        http_helpers.TENANT = self.tenant
        self.log.close()
        self.output = ROOT / "build/incidents" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.override = self.output / "compose.json"
        self.config = self.output / "service.json"
        self.secret = self.output / "directory.secret"
        base = {"image": "debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5", "profiles": ["incident-test"],
                "user": f"{os.getuid()}:{os.getgid()}", "read_only": True, "cap_drop": ["ALL"], "security_opt": ["no-new-privileges:true"] + (["seccomp=unconfined"] if preset == "tsan" else []),
                "volumes": [str(ROOT) + ":/repo:ro", str(ROOT / f"build/debian13/{preset}/profiles") + ":/profiles"], "tmpfs": ["/tmp"],
                "command": [f"/repo/build/debian13/{preset}/services/incident-service/psim-incident-service", "--config", "/repo/" + str(self.config.relative_to(ROOT))],
                "environment": {"LLVM_PROFILE_FILE": "/profiles/%p-%m.profraw"}, "ports": ["127.0.0.1::8443", "127.0.0.1::9100"], "stop_grace_period": "35s"}
        services = {"incident-service": base, "incident-2": base | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": "incident-2"}},
                    "keycloak": {"ports": ["127.0.0.1::8080"]}, "registry": {"ports": ["127.0.0.1::8080"]}}
        self.override.write_text(json.dumps({"services": services}, indent=2))
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml"), "-f", str(self.override)]
        self.legacy_tenant, self.legacy_id, self.legacy_site = uid(), uid(), uid()
        self.admin = None
        self.subjects = {}
        self.report = {"passed": False, "signals": count, "checks": []}

    def port(self, service, port):
        identity = (service, port)
        if identity not in self.ports:
            self.ports[identity] = self.compose("port", service, str(port)).strip().rsplit(":", 1)[1]
        return self.ports[identity]

    def env_command(self, command, network=True):
        return super().env_command(f"LLVM_PROFILE_FILE=/repo/build/debian13/{self.preset}/profiles/%p-%m.profraw " + command, network)

    def request(self, method, path, body=None, role="operator", version=None, expected=200, key=None, content_type=None, service="incident-service"):
        return super().request(method, path, body, role, version, expected, key, content_type, service)

    def principals(self):
        self.admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        clients = "/admin/realms/psim/clients"
        resource = self.keycloak("GET", clients + "?clientId=psim", self.admin)[0]["id"]
        self.roles_client = resource
        cases = {"admin": ("admin", ["*"], self.tenant), "operator": ("operator", [self.site_id], self.tenant),
                 "bob": ("operator", [self.site_id], self.tenant), "chief": ("supervisor", [self.site_id], self.tenant),
                 "other": ("operator", [self.foreign_site], self.tenant), "auditor": ("auditor", ["*"], self.tenant),
                 "empty": ("operator", [], self.tenant), "tenant": ("operator", ["*"], self.legacy_tenant)}
        for name, (role, scopes, tenant) in cases.items():
            client_id = "psim-incident-" + self.fixtures + "-" + name
            self.keycloak("POST", clients, self.admin, {"clientId": client_id, "enabled": True, "publicClient": False, "serviceAccountsEnabled": True, "standardFlowEnabled": False, "secret": "psim-dev-catalog", "protocol": "openid-connect", "protocolMappers": [
                {"name": "audience", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper", "config": {"included.client.audience": "psim", "access.token.claim": "true"}},
                {"name": "tenant", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_tenant", "claim.value": tenant, "jsonType.label": "String", "access.token.claim": "true"}},
                {"name": "scope", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_scopes", "claim.value": json.dumps(scopes), "jsonType.label": "JSON", "access.token.claim": "true"}},
            ]})
            found = self.keycloak("GET", clients + "?clientId=" + client_id, self.admin)[0]["id"]
            account = self.keycloak("GET", clients + "/" + found + "/service-account-user", self.admin)
            self.subjects[name] = account["id"]
            account["attributes"] = {"psim_tenant": [tenant], "psim_scopes": scopes}
            self.keycloak("PUT", "/admin/realms/psim/users/" + account["id"], self.admin, account)
            role_body = self.keycloak("GET", clients + "/" + resource + "/roles/" + role, self.admin)
            self.keycloak("POST", "/admin/realms/psim/users/" + account["id"] + "/role-mappings/clients/" + resource, self.admin, [role_body])
            self.tokens[name] = {"client": client_id, "value": "", "expires": 0}
        directory_id = "psim-incident-directory-" + self.fixtures
        self.keycloak("POST", clients, self.admin, {"clientId": directory_id, "enabled": True, "serviceAccountsEnabled": True, "standardFlowEnabled": False, "secret": "psim-dev-directory"})
        client = self.keycloak("GET", clients + "?clientId=" + directory_id, self.admin)[0]["id"]
        account = self.keycloak("GET", clients + "/" + client + "/service-account-user", self.admin)["id"]
        management = self.keycloak("GET", clients + "?clientId=realm-management", self.admin)[0]["id"]
        roles = [self.keycloak("GET", clients + "/" + management + "/roles/" + name, self.admin) for name in ("view-users", "view-clients")]
        self.keycloak("POST", "/admin/realms/psim/users/" + account + "/role-mappings/clients/" + management, self.admin, roles)
        self.directory_client = directory_id

    def prepare(self):
        self.env_command("sh tools/env/event_flow_tls.sh", network=False)
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem")
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keycloak", "otel-collector")
        def registry_ready():
            with urllib.request.urlopen("http://localhost:" + self.port("registry", 8080) + "/apis/ccompat/v7/subjects", timeout=5) as response:
                return response.status == 200
        self.wait(registry_ready, 120)
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.env_command("python3 -I tools/env/keycloak_security.py http://keycloak:8080")
        self.principals()
        self.secret.unlink(missing_ok=True)
        self.secret.write_text("psim-dev-directory")
        self.secret.chmod(0o400)
        self.config.write_text(json.dumps({"service": {"instance": "incident-1"}, "runtime": {"shards": 1}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001},
          "settings": {"allow_plaintext": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "introspection_secret_file": "/repo/.cache/event-flow/introspection.secret", "listen_port": 8443,
                       "issuer": "http://keycloak:8080/realms/psim", "directory_base": "http://keycloak:8080", "directory_client": self.directory_client,
                       "directory_roles_client": self.roles_client, "directory_secret_file": "/repo/" + str(self.secret.relative_to(ROOT))}}, indent=2))
        (ROOT / f"build/debian13/{self.preset}/profiles").mkdir(parents=True, exist_ok=True)
        legacy=self.sql("SELECT to_regclass('incident.incidents') IS NULL")=="t"
        if legacy:
            self.env_command(f"PSIM_PG_HOST=postgres PSIM_PG_SSLMODE=disable PSIM_PG_PASSWORD=psim-dev-psim build/debian13/{self.preset}/tools/pg-migrate/psim-pg-migrate expand tools/incidents/fixtures/initial-schema.json")
            now=dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00","Z")
            body={"incidentId":self.legacy_id,"incidentTypeId":uid(),"siteId":self.legacy_site,"state":"INCIDENT_STATE_NEW","priority":"PRIORITY_P2","prioritySource":"PRIORITY_SOURCE_COMPUTED","groupingKey":"legacy","groupUntil":now,"createdAt":now,"title":"Existing incident","signalIds":[uid()],"version":"1"}
            self.sql("INSERT INTO incident.incidents VALUES('"+self.legacy_tenant+"','"+self.legacy_id+"','legacy',0,'"+json.dumps(body)+"'::jsonb)")
        self.compose("up", "-d", "--force-recreate", "incident-service", "incident-2")
        if legacy:
            restored=self.wait(lambda:self.request("GET","/v1/incidents/"+self.legacy_id,role="tenant")["incident"],60)
            assert restored["version"]=="1" and restored["title"]=="Existing incident" and restored["signalCount"]=="1"
            self.mark("initial schema expands without changing existing incident identities or versions")
        self.wait(lambda: self.request("GET", "/v1/incidents"), 60)
        self.mark("two HTTPS instances, verified JWTs and read-only identity directory")

    def emit(self, **values):
        manifest = {"tenant": self.tenant, "site": self.site_id, "zone": self.zone, "type": self.type_id, "seed": uid()} | values
        path = self.output / ("signals-" + uid() + ".json")
        path.write_text(json.dumps(manifest))
        raw = self.env_command(f"build/debian13/{self.preset}/tools/incidents/psim-incident-probe /repo/{path.relative_to(ROOT)}")
        return json.loads(raw.strip().splitlines()[-1])

    def incident(self, identity):
        return self.request("GET", "/v1/incidents/" + identity)["incident"]

    def command(self, value, action, body=None, role="operator", **options):
        result = self.request("POST", "/v1/incidents/" + value["incidentId"] + ":" + action, body or {}, role=role, version=value["version"], **options)
        return result.get("incident", result)

    def basics(self):
        self.zone, self.type_id = uid(), uid()
        self.type_body = {"incidentTypeId": self.type_id, "code": self.fixtures, "name": "Intrusion", "defaultPriority": "PRIORITY_P2", "groupingWindow": "60s", "autoCloseAfter": "300s", "priorityMatrix": {"3": "PRIORITY_P2"}}
        self.type_value = self.request("POST", "/v1/incident-types", self.type_body, role="admin", expected=201)["incidentType"]
        self.emit(count=3, repeat=2, seed=uid(), group="basic-" + self.fixtures, priority=3)
        incidents = self.wait(lambda: (items if (items := self.request("GET", "/v1/incidents")["incidents"]) and items[0]["signalCount"]=="3" else None))
        assert len(incidents) == 1, incidents
        value = self.incident(incidents[0]["incidentId"])
        assert value["version"] == "3" and value["priority"] == "PRIORITY_P2", value
        self.metrics()
        assert self.report["eventToIncidentSamples"] == 3, "repeated signals changed the pipeline histogram"
        assert self.report["persistedSignals"] == 3, "repeated signals changed the persistence counter"
        self.request("GET", "/v1/incidents/" + value["incidentId"], role=None, expected=401)
        self.request("GET", "/v1/incidents/" + value["incidentId"], role="other", expected=403)
        self.request("GET", "/v1/incidents/" + value["incidentId"], role="tenant", expected=404)
        assert self.request("GET", "/v1/incidents", role="empty")["incidents"] == []
        self.request("POST", "/v1/incidents/" + value["incidentId"] + ":acknowledge", {}, role="admin", version=value["version"], expected=403)
        self.request("GET", "/v1/incidents?unknown=value", expected=400)
        self.command(value, "start-work", expected=409)
        self.request("POST", "/v1/incidents/" + value["incidentId"] + ":acknowledge", {}, expected=428)
        self.request("POST", "/v1/incidents/" + value["incidentId"] + ":acknowledge", {"unknown": 1}, version=value["version"], expected=400)
        key = uid()
        acknowledged = self.command(value, "acknowledge", key=key)
        assert self.command(value, "acknowledge", key=key, service="incident-2") == acknowledged
        self.command(value, "acknowledge", expected=412)
        working = self.command(acknowledged, "start-work")
        self.command(working, "resolve", {"resolution": "RESOLUTION_CONFIRMED", "knownResponseRunVersion": "9"}, expected=409)
        comment = self.request("POST", "/v1/incidents/" + working["incidentId"] + "/comments", {"text": "checked"}, version=working["version"])["entry"]
        assert comment["text"] == "checked"
        working = self.incident(working["incidentId"])
        self.request("POST", "/v1/incidents/" + working["incidentId"] + "/links", {"url": "javascript:alert(1)"}, version=working["version"], expected=400)
        self.request("POST", "/v1/incidents/" + working["incidentId"] + "/links", {"url": "https://example.test/video", "title": "camera"}, version=working["version"])
        working = self.incident(working["incidentId"])
        resolved = self.command(working, "resolve", {"resolution": "RESOLUTION_CONFIRMED", "note": "complete"})
        reopened = self.command(resolved, "reopen", {"reason": "new evidence"})
        assert reopened["state"] == "INCIDENT_STATE_IN_PROGRESS" and "resolvedAt" not in reopened
        resolved = self.command(reopened, "resolve", {"resolution": "RESOLUTION_FALSE_ALARM"})
        closed = self.command(resolved, "close", role="chief")
        assert closed["state"] == "INCIDENT_STATE_CLOSED"
        self.command(closed, "reopen", {"reason": "forbidden"}, role="chief", expected=409)
        timeline = self.request("GET", "/v1/incidents/" + closed["incidentId"] + "/timeline?pageSize=2")
        assert len(timeline["entries"]) == 2 and timeline["nextPageToken"]
        signals = self.request("GET", "/v1/incidents/" + closed["incidentId"] + "/signals")
        assert len(signals["signals"]) == 3
        self.mark("lifecycle, canonical requests, idempotency, scope, timeline and signal pagination")

    def transfer(self):
        self.emit(group="transfer-" + self.fixtures)
        value = self.wait(lambda: next((item for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == "transfer-" + self.fixtures), None))
        value = self.command(value, "acknowledge")
        value = self.command(value, "set-priority", {"priority": "PRIORITY_P1", "reason": "urgent"}, role="chief")
        self.emit(mode="response",incident=value["incidentId"],run=uid(),runVersion=4,blocking=["mandatory"],repeat=2)
        value=self.wait(lambda: (current if (current := self.incident(value["incidentId"]))["responseRunVersion"]=="4" else None))
        path = "/v1/incidents:transfer"
        before = value
        requested = self.request("POST", path, {"items": [{"incidentId": value["incidentId"], "expectedVersion": value["version"], "recipientId": self.subjects["bob"], "comment": "handover"}]})["results"][0]["incident"]
        assert requested["assigneeId"] == self.subjects["operator"]
        transfer = requested["pendingTransfer"]["transferId"]
        item = {"incidentId": value["incidentId"], "expectedVersion": requested["version"], "transferId": transfer}
        refused = self.request("POST", path, {"items": [item]})["results"][0]
        assert refused["errorCode"] == "COMMON_CONCURRENT_MODIFICATION", refused
        key = uid()
        confirmed = self.request("POST", path, {"items": [item]}, role="bob", key=key)["results"][0]["incident"]
        assert self.request("POST", path, {"items": [item]}, role="bob", key=key, service="incident-2")["results"][0]["incident"] == confirmed
        assert confirmed["assigneeId"] == self.subjects["bob"] and confirmed["acknowledgedAt"] == before["acknowledgedAt"]
        assert confirmed["priority"] == before["priority"] and confirmed["responseRunVersion"] == before["responseRunVersion"]
        assert confirmed["blockingSteps"]==before["blockingSteps"]
        invalid = self.request("POST", path, {"items": [{"incidentId": confirmed["incidentId"], "expectedVersion": confirmed["version"], "recipientId": self.subjects["other"]}]}, role="bob")["results"][0]
        assert invalid["errorCode"] == "INCIDENT_ASSIGNEE_INVALID", invalid
        account = self.keycloak("GET", "/admin/realms/psim/users/" + self.subjects["operator"], self.admin)
        account["enabled"] = False
        self.keycloak("PUT", "/admin/realms/psim/users/" + account["id"], self.admin, account)
        invalid = self.request("POST", path, {"items": [{"incidentId": confirmed["incidentId"], "expectedVersion": confirmed["version"], "recipientId": self.subjects["operator"]}]}, role="bob")["results"][0]
        assert invalid["errorCode"] == "INCIDENT_ASSIGNEE_INVALID", invalid
        account["enabled"] = True
        self.keycloak("PUT", "/admin/realms/psim/users/" + account["id"], self.admin, account)
        self.mark("confirmed transfer, cross-instance replay, unchanged timestamps, recipient scope and active account")

    def grouping(self):
        group = "window-" + self.fixtures
        self.emit(group=group)
        first = self.wait(lambda: next((item for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == group), None))
        self.emit(group=group, offsetNs=120_000_000_000)
        values = self.wait(lambda: (items if len(items := [item for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == group]) == 2 else None))
        assert sum(item["relatedIncidentId"] == first["incidentId"] for item in values) == 1
        assert all(item["state"] == "INCIDENT_STATE_NEW" for item in values)
        self.mark("expired grouping creates related incident while prior incident stays open")

    def configuration_and_commands(self):
        self.request("POST", "/v1/incident-types", self.type_body, role="admin", expected=409)
        self.request("POST", "/v1/incident-types", self.type_body | {"incidentTypeId": uid(), "groupingWindow": "0s"}, role="admin", expected=400)
        self.request("POST", "/v1/incident-types", self.type_body | {"unknown": 1}, role="admin", expected=400)
        self.request("PUT", "/v1/incident-types/" + self.type_id, self.type_body, role="admin", expected=428)
        self.request("PUT", "/v1/incident-types/" + self.type_id, self.type_body, role="admin", version=999, expected=412)
        assert self.request("GET", "/v1/incident-types", role="operator")["incidentTypes"]
        self.request("GET", "/v1/incidents?pageSize=1001", expected=400)
        self.request("GET", "/v1/incidents?pageToken=bad", expected=400)
        self.request("GET", "/v1/incidents?states=INCIDENT_STATE_CLOSED&priorities=PRIORITY_P2")
        self.request("GET", "/v1/unknown", expected=404)
        self.request("POST", "/v1/incidents:transfer", {"items": []}, expected=400)
        self.emit(group="commands-" + self.fixtures)
        value = self.wait(lambda: next((item for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == "commands-" + self.fixtures), None))
        self.command(value, "acknowledge", role="auditor", expected=403)
        self.command(value, "assign", {"assigneeId": uid()}, role="chief", expected=400)
        self.command(value, "assign", {"assigneeId": self.subjects["admin"]}, role="chief", expected=400)
        value = self.command(value, "assign", {"assigneeId": self.subjects["operator"]}, role="chief")
        value = self.command(value, "acknowledge")
        self.command(value, "assign", {"assigneeId": self.subjects["bob"]}, expected=403)
        value = self.command(value, "assign", {"assigneeId": self.subjects["bob"]}, role="chief")
        self.command(value, "start-work", expected=403)
        value = self.command(value, "start-work", role="bob")
        self.command(value, "set-priority", {"priority": "PRIORITY_P1"}, role="bob", expected=403)
        value = self.command(value, "set-priority", {"priority": "PRIORITY_P1"}, role="chief")
        self.emit(group="commands-" + self.fixtures, priority=4)
        previous_version = int(value["version"])
        value = self.wait(lambda: (current if int((current := self.request("GET", "/v1/incidents/" + value["incidentId"], role="bob")["incident"])["version"]) > previous_version else None))
        assert value["priority"] == "PRIORITY_P1" and value["prioritySource"] == "PRIORITY_SOURCE_MANUAL"
        run=uid()
        self.emit(mode="response",incident=value["incidentId"],run=run,runVersion=7,blocking=["mandatory"],repeat=2)
        value=self.wait(lambda: (current if (current := self.incident(value["incidentId"]))["responseRunVersion"]=="7" else None))
        assert len(value["blockingSteps"])==1
        self.emit(mode="response",incident=value["incidentId"],run=run,runVersion=6,blocking=[])
        self.emit(mode="response",incident=value["incidentId"],level=2,repeat=2)
        value=self.wait(lambda: (current if (current := self.incident(value["incidentId"]))["escalationLevel"]==2 else None))
        assert value["responseRunVersion"]=="7" and len(value["blockingSteps"])==1
        self.command(value, "resolve", {"resolution": "RESOLUTION_CONFIRMED", "knownResponseRunVersion": "7"}, role="bob", expected=409)
        resolved = self.command(value, "resolve", {"resolution": "RESOLUTION_TEST", "knownResponseRunVersion": "7"}, role="chief")
        assert resolved["state"] == "INCIDENT_STATE_RESOLVED"
        key = uid()
        closed = self.command(resolved, "close", role="chief", key=key)
        self.command(resolved, "close", {"incidentId": resolved["incidentId"]}, role="chief", key=key)
        assert self.request("GET", "/v1/incidents/" + closed["incidentId"], role="auditor")["incident"]["state"] == "INCIDENT_STATE_CLOSED"
        self.mark("type configuration, role matrix, assignment, manual priority and mandatory response guards")

    def large_group(self):
        expected=self.emit(count=1002,group="large-"+self.fixtures)
        value=self.wait(lambda: next((item for item in self.request("GET","/v1/incidents")["incidents"] if item["groupingKey"]=="large-"+self.fixtures and item["signalCount"]=="1002"),None),120)
        assert len(value["signalIds"])==1000 and value["signalIdsTruncated"]
        first=self.request("GET","/v1/incidents/"+value["incidentId"]+"/signals?pageSize=1000")
        second=self.request("GET","/v1/incidents/"+value["incidentId"]+"/signals?pageSize=1000&pageToken="+first["nextPageToken"])
        assert set(expected)=={item["signalId"] for item in first["signals"]+second["signals"]}
        self.mark("group beyond 1000 signals has a bounded preview and complete paginated evidence")

    def auto_close(self):
        body = self.type_body | {"incidentTypeId": uid(), "code": uid(), "autoCloseAfter": "2s"}
        self.request("POST", "/v1/incident-types", body, role="admin", expected=201)
        self.emit(type=body["incidentTypeId"], group="automatic-" + self.fixtures)
        value = self.wait(lambda: next((item for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == "automatic-" + self.fixtures), None))
        value = self.command(value, "acknowledge")
        self.command(value, "resolve", {"resolution": "RESOLUTION_CONFIRMED"})
        self.compose("stop", "-t", "30", "incident-service", "incident-2")
        self.compose("up", "-d", "incident-service", "incident-2")
        self.wait(lambda: self.incident(value["incidentId"])["state"] == "INCIDENT_STATE_CLOSED", 30)
        self.mark("durable automatic close after both processes restart")

    def metrics(self):
        values = {}
        pipeline_samples = 0
        persisted = 0
        for service in ("incident-service", "incident-2"):
            with urllib.request.urlopen("http://localhost:" + self.port(service, 9100) + "/metrics", timeout=5) as response:
                body = response.read().decode()
            for boundary, count in re.findall(r'psim_incident_signal_seconds_bucket\{le="([^"]+)"\} ([0-9.]+)', body):
                values[float(boundary)] = values.get(float(boundary), 0) + float(count)
            observed = re.search(r'^psim_event_to_incident_seconds_count ([0-9.]+)$', body, re.MULTILINE)
            assert observed, "event-to-incident histogram is missing"
            pipeline_samples += float(observed[1])
            committed = re.search(r'^psim_incident_signals_persisted_total ([0-9.]+)$', body, re.MULTILINE)
            assert committed, "signal persistence counter is missing"
            persisted += float(committed[1])
        self.report["eventToIncidentSamples"] = pipeline_samples
        self.report["persistedSignals"] = persisted
        return values

    def performance(self):
        before = self.metrics()
        pipeline_before = self.report["eventToIncidentSamples"]
        expected = self.emit(count=self.count, uniqueGroups=True, group="load-" + self.fixtures)
        self.wait(lambda: int(self.sql("SELECT count(*) FROM incident.signals WHERE tenant='" + self.tenant + "' AND body->'signal'->>'groupingKey' LIKE 'load-%'")) == self.count, 180)
        after = self.metrics()
        assert self.report["eventToIncidentSamples"] - pipeline_before >= self.count
        samples = after[float("inf")] - before.get(float("inf"), 0)
        assert samples >= self.count
        upper = min(bound for bound, count in after.items() if count - before.get(bound, 0) >= 0.99 * samples)
        self.report["p99SignalTransactionUpperMs"] = upper * 1000
        self.report["measuredSignals"] = int(samples)
        if self.preset == "release":
            assert upper <= 0.05, self.report
        received = set(self.sql("SELECT id::text FROM incident.signals WHERE tenant='" + self.tenant + "'").splitlines())
        assert set(expected).issubset(received)
        self.mark("signal persistence histogram and complete load identity reconciliation")

    def failures(self):
        self.sql("CREATE OR REPLACE FUNCTION incident.slow_insert() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN PERFORM pg_sleep(0.005); RETURN NEW; END $$; CREATE TRIGGER incident_slow_insert BEFORE INSERT ON incident.incidents FOR EACH ROW EXECUTE FUNCTION incident.slow_insert()")
        try:
            with concurrent.futures.ThreadPoolExecutor(max_workers=1) as executor:
                pending=executor.submit(self.emit,count=self.count,repeat=2,uniqueGroups=True,group="restart-"+self.fixtures)
                time.sleep(0.2)
                self.compose("kill", "-s", "SIGKILL", "incident-service")
                self.compose("up", "-d", "incident-service")
                expected=pending.result(timeout=120)
        finally:
            self.sql("DROP TRIGGER IF EXISTS incident_slow_insert ON incident.incidents; DROP FUNCTION IF EXISTS incident.slow_insert()")
        def reconciled():
            rows = self.sql("SELECT id::text FROM incident.signals WHERE tenant='" + self.tenant + "' ORDER BY id").splitlines()
            return set(expected).issubset(rows)
        self.wait(reconciled, 120)
        rows = self.sql("SELECT id::text FROM incident.signals WHERE tenant='" + self.tenant + "' ORDER BY id").splitlines()
        assert len(rows) == len(set(rows))
        assert set(expected).issubset(rows)
        self.wait(lambda: self.sql("SELECT count(*) FROM incident.psim_outbox WHERE published_at IS NULL") == "0", 60)
        assert int(self.sql("SELECT count(*) FROM incident.psim_outbox WHERE topic='psim.audit.v1'")) > self.count
        self.mark("SIGKILL, partition reassignment, repeated Kafka delivery, full identity reconciliation and durable audit")

    def cleanup(self):
        try:
            self.compose("stop", "-t", "35", "incident-service", "incident-2")
            logs = self.compose("logs", "--no-color", "incident-service", "incident-2")
            (self.output / "servers.log").write_text(logs)
            if re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs):
                self.report["passed"] = False
                raise AssertionError("service sanitizer diagnostics; see servers.log")
        finally:
            try:
                if self.owned and os.environ.get("PSIM_INCIDENT_KEEP_ENV") != "1":
                    self.compose("--profile", "incident-test", "down", "-v", "--remove-orphans", "--timeout", "10")
            finally:
                (self.output / "report.json").write_text(json.dumps(self.report, indent=2))
                self.log.close()


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.basics()
        check.transfer()
        check.grouping()
        check.configuration_and_commands()
        check.large_group()
        check.auto_close()
        check.performance()
        check.failures()
        check.report["passed"] = True
    finally:
        check.cleanup()


if __name__ == "__main__":
    main()
