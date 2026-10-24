"""Verify operational writes, concurrent leases, history, exercises and model boundaries."""
import concurrent.futures
import datetime as dt
import json
import runpy
import shutil
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
base = runpy.run_path(str(ROOT / "tools/incidents/check.py"))
Base, uid = base["Check"], base["uid"]


class Check(Base):
    def prepare(self):
        super().prepare()
        override = json.loads(self.override.read_text())
        image = self.run(["docker", "images", "--format", "{{.Repository}}:{{.Tag}}", "psim-platform/api-check"]).strip().splitlines()[0]
        override["services"]["model"] = {"image": image, "command": ["python3", "-I", "/repo/tools/operations/provider.py"],
            "volumes": [str(ROOT) + ":/repo:ro"], "read_only": True, "cap_drop": ["ALL"], "security_opt": ["no-new-privileges:true"]}
        self.override.write_text(json.dumps(override))
        config = json.loads(self.config.read_text())
        config["settings"] |= {"assistant_endpoint": "http://model:11434/api/chat", "assistant_model": "protocol-fixture"}
        self.config.write_text(json.dumps(config))
        self.compose("up", "-d", "model")
        self.compose("up", "-d", "--force-recreate", "incident-service", "incident-2")
        self.wait(lambda: self.request("GET", "/v1/incidents"), 60)
        self.zone, self.type_id = uid(), uid()
        self.request("POST", "/v1/incident-types", {"incidentTypeId": self.type_id, "code": self.fixtures,
            "name": "Operational incident", "defaultPriority": "PRIORITY_P1", "groupingWindow": "60s"}, role="admin", expected=201)
        self.emit(count=1)
        self.value = self.wait(lambda: (items[0] if (items := self.request("GET", "/v1/incidents")["incidents"]) else None))
        self.path = "/v1/incidents/" + self.value["incidentId"]

    def current(self):
        return self.request("GET", self.path)["incident"]

    def operate(self, action, body, role="operator", expected=200, value=None, **options):
        value = value or self.current()
        return self.request("POST", self.path + ":" + action, body, role=role, version=value["version"], expected=expected, **options)

    def alarms(self):
        initial = self.current()
        assert initial["operational"]["active"] and not initial["operational"].get("acknowledged", False)
        self.operate("update-alarm", {"action": "return", "reason": "condition cleared"}, expected=403)
        value = self.operate("update-alarm", {"action": "return", "reason": "condition cleared"}, role="chief")["incident"]
        assert not value["operational"].get("active", False) and value["state"] == "INCIDENT_STATE_NEW"
        value = self.operate("update-alarm", {"action": "acknowledge"})["incident"]
        assert value["operational"]["acknowledged"] and value["state"] == "INCIDENT_STATE_NEW"
        value = self.operate("update-alarm", {"action": "activate", "reason": "condition returned"}, role="chief")["incident"]
        assert value["operational"]["active"] and not value["operational"].get("acknowledged", False)
        until = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=2)).isoformat().replace("+00:00", "Z")
        body = {"action": "shelve", "reason": "inspection", "until": until}
        key = uid()
        first = self.operate("update-alarm", body, key=key)
        assert self.operate("update-alarm", body, value=value, key=key, service="incident-2") == first
        self.operate("update-alarm", body, value=value, expected=412)
        self.operate("update-alarm", {"action": "suppress", "reason": "policy", "until": until}, expected=403)
        self.operate("update-alarm", {"action": "shelve", "reason": "missing end"}, expected=400)
        self.wait(lambda: "shelved" not in self.current()["operational"], 10)
        assert self.current()["operational"]["active"]
        later = (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=30)).isoformat().replace("+00:00", "Z")
        self.operate("update-alarm", {"action": "suppress", "reason": "policy", "until": later}, role="chief")
        self.operate("update-alarm", {"action": "unsuppress", "reason": "policy ended"}, expected=403)
        self.operate("update-alarm", {"action": "unsuppress", "reason": "policy ended"}, role="chief")
        self.operate("update-alarm", {"action": "shelve", "reason": "inspection", "until": later})
        self.operate("update-alarm", {"action": "unshelve", "reason": "inspection complete"})
        self.operate("update-alarm", {"action": "invalid"}, expected=400)
        self.operate("update-alarm", {"action": "shelve", "reason": "invalid time", "until": "invalid"}, expected=400)
        self.request("GET", self.path + "/operations", role="other", expected=403)
        self.request("GET", self.path + "/snapshots", role="tenant", expected=404)
        self.operate("update-alarm", {"action": "acknowledge"}, role="auditor", expected=403)
        assert int(self.sql("SELECT count(*) FROM incident.psim_outbox WHERE topic='psim.audit.v1' AND position('incident.operational.update-alarm.shelve' in encode(payload,'escape'))>0 AND position('inspection' in encode(payload,'escape'))>0")) > 0
        self.mark("independent condition/acknowledgement, version conflicts, two-instance repeats and durable expiry")

    def sla(self):
        assert self.request("GET", self.path + "/operations")["slaRisk"] == "unknown"
        now = dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")
        record = {"topic": "psim.response.events.v1", "key": self.value["incidentId"], "value": {
            "envelope": {"messageId": uid(), "tenantId": self.tenant, "sequence": "1", "schemaVersion": "1",
                "occurredAt": now, "producedAt": now, "actor": {"id": "response-engine", "kind": "ACTOR_KIND_SERVICE"}},
            "slaPolicyPinned": {"incidentId": self.value["incidentId"], "createdAt": self.value["createdAt"],
                "acknowledgeWithin": "3600s", "resolveWithin": "7200s"}}}
        fixture = self.output / "sla.json"
        fixture.write_text(json.dumps({"records": [record, record]}))
        self.env_command(f"build/debian13/{self.preset}/tools/api-gateway/psim-api-probe /repo/{fixture.relative_to(ROOT)}")
        self.wait(lambda: self.request("GET", self.path + "/operations")["slaRisk"] == "on-time", 30)
        original = self.current()["operational"]["resolveDue"]
        value = self.command(self.current(), "acknowledge")
        requested = self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": value["version"], "recipientId": self.subjects["bob"]}]})["results"][0]["incident"]
        transfer = requested["pendingTransfer"]["transferId"]
        session = uid()
        requested = self.operate("set-presence", {"sessionId": session, "ttlSeconds": 30}, role="bob", value=requested)["incident"]
        lease = next(row["leaseId"] for row in requested["operational"]["presence"] if row["sessionId"] == session)
        assert requested["pendingTransfer"]["requestedVersion"] == requested["version"]
        confirmed = self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": requested["version"], "transferId": transfer}]}, role="bob")["results"][0]["incident"]
        assert confirmed["operational"]["resolveDue"] == original
        assert self.current()["operational"]["resolveDue"] == original
        returned = self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": confirmed["version"], "recipientId": self.subjects["operator"]}]}, role="bob")["results"][0]["incident"]
        self.request("POST", self.path + "/comments", {"text": "Briefing changed"}, version=returned["version"], role="bob")
        renewed = self.operate("set-presence", {"sessionId": session, "leaseId": lease, "ttlSeconds": 30}, role="bob")["incident"]
        rejected = self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": renewed["version"], "transferId": returned["pendingTransfer"]["transferId"]}]})["results"][0]
        assert rejected["errorCode"] == "COMMON_CONCURRENT_MODIFICATION"
        returned = self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": renewed["version"], "recipientId": self.subjects["operator"]}]}, role="bob")["results"][0]["incident"]
        self.request("POST", "/v1/incidents:transfer", {"items": [{"incidentId": value["incidentId"],
            "expectedVersion": returned["version"], "transferId": returned["pendingTransfer"]["transferId"]}]})["results"][0]["incident"]
        self.operate("set-presence", {"sessionId": session, "leaseId": lease, "release": True}, role="bob")
        assert self.current()["operational"]["resolveDue"] == original
        self.mark("pinned SLA replay is idempotent and confirmed responsibility transfer preserves original deadlines")

    def presence(self):
        value = self.current()
        session = uid()
        key = uid()
        body = {"sessionId": session, "editing": True, "ttlSeconds": 2}
        lease = self.operate("set-presence", body, key=key)["incident"]
        assert len(lease["operational"]["presence"]) == 1
        token = lease["operational"]["presence"][0]["leaseId"]
        self.operate("set-presence", {"sessionId": uid(), "editing": True}, role="bob", expected=412, value=value)
        self.operate("set-presence", {"sessionId": uid(), "editing": True}, role="bob", expected=412)
        self.operate("set-presence", {"sessionId": session, "leaseId": uid()}, expected=412)
        self.wait(lambda: not self.current()["operational"].get("presence"), 10)
        self.operate("set-presence", body, key=key, value=value, service="incident-2", expected=412)
        self.operate("set-presence", {"sessionId": session, "leaseId": token}, expected=412)
        value = self.current()
        def claim(role):
            try:
                result = self.operate("set-presence", {"sessionId": uid(), "editing": True, "ttlSeconds": 30},
                    role=role, value=value, service="incident-2" if role == "bob" else "incident-service")
                return result
            except AssertionError:
                return None
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            results = list(pool.map(claim, ["operator", "bob"]))
        assert sum(item is not None for item in results) == 1
        current = self.current()
        entry = current["operational"]["presence"][0]
        owner = "operator" if entry["actorId"] == self.subjects["operator"] else "bob"
        self.operate("set-presence", {"sessionId": entry["sessionId"], "leaseId": entry["leaseId"], "release": True}, role=owner)
        assert not self.current()["operational"].get("presence")
        self.mark("fenced lease expiry, competing instances, stale renewals, replay and explicit release")

    def history(self):
        snapshots = self.request("GET", self.path + "/snapshots?pageSize=2")
        assert snapshots["historical"] and len(snapshots["snapshots"]) == 2
        next_page = self.request("GET", self.path + "/snapshots?afterVersion=" + snapshots["nextVersion"])
        assert next_page["snapshots"]
        old = snapshots["snapshots"][0]
        at = old["capturedAt"]
        historical = self.request("GET", self.path + "/snapshots?at=" + at)
        assert historical["snapshots"][0]["incident"]["version"] == old["incident"]["version"]
        recovery = self.request("GET", self.path + "/recovery?knownVersion=" + old["incident"]["version"])
        assert int(recovery["changedVersions"]) > 0
        self.compose("restart", "incident-2")
        self.wait(lambda: self.request("GET", self.path + "/snapshots?pageSize=2", service="incident-2") == snapshots, 60)
        self.mark("immutable paginated snapshots, historical selection and recovery across process restart")

    def assistant(self):
        before = self.current()
        for question in ["Explain status", "unavailable", "missing-source", "execute-command"]:
            result = self.operate("ask-assistant", {"question": question})
            assert result["sources"][0]["version"] == before["version"]
            assert result["unavailable"] == (question != "Explain status")
            if question == "Explain status":
                assert result["claims"][0]["assumption"]
        self.operate("ask-assistant", {"question": "x", "url": "http://forbidden"}, expected=400)
        self.operate("ask-assistant", {"question": "x"}, role="other", expected=403)
        assert self.current() == before
        with concurrent.futures.ThreadPoolExecutor(1) as pool:
            pending = pool.submit(self.operate, "ask-assistant", {"question": "slow"}, expected=412, value=before)
            time.sleep(0.4)
            started = time.monotonic()
            self.operate("update-alarm", {"action": "acknowledge"}, value=before)
            assert time.monotonic() - started < 1.5, "model retained the incident lock"
            pending.result()
        before = self.current()
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            pending = [pool.submit(self.operate, "ask-assistant", {"question": "slow"}, value=before) for _ in range(2)]
            time.sleep(0.4)
            self.operate("ask-assistant", {"question": "Explain status"}, expected=429, value=before)
            for result in pending:
                assert not result.result()["unavailable"]
        with concurrent.futures.ThreadPoolExecutor(1) as pool:
            pending = pool.submit(self.operate, "ask-assistant", {"question": "slow"}, expected=403, value=before)
            time.sleep(0.4)
            path = "/admin/realms/psim/users/" + self.subjects["operator"]
            account = self.keycloak("GET", path, self.admin)
            changed = json.loads(json.dumps(account))
            changed["attributes"]["psim_scopes"] = [self.foreign_site]
            self.keycloak("PUT", path, self.admin, changed)
            try:
                pending.result()
            finally:
                self.keycloak("PUT", path, self.admin, account)
        assert self.current() == before
        assert int(self.sql("SELECT count(*) FROM incident.psim_outbox WHERE topic='psim.audit.v1'")) >= 4
        self.mark("local provider protocol, source validation, unavailable fallback, forbidden actions and unchanged production state")

    def exercises(self):
        before = self.current()
        created = self.operate("create-exercise", {"title": "Isolated drill"})["exercise"]
        assert created["incident"]["incidentId"] != before["incidentId"]
        self.request("GET", "/v1/incidents/" + created["incident"]["incidentId"], expected=404)
        path = self.path + "/exercises/" + created["exerciseId"]
        self.request("GET", path, role="bob", expected=404)
        self.request("POST", path + ":act", {"action": "command", "reason": "out of order"}, version=created["version"], expected=409)
        value = created
        for action in ["acknowledge", "start-work", "command", "resolve"]:
            key = uid()
            body = {"action": action, "reason": "training"}
            result = self.request("POST", path + ":act", body, version=value["version"], key=key)
            assert self.request("POST", path + ":act", body, version=value["version"], key=key, service="incident-2") == result
            value = result["exercise"]
        assert value["score"] == 100
        assert self.current() == before
        assert self.request("GET", path)["exercise"] == value
        self.mark("versioned private exercise, ordered scoring, repeats and no incident or device command effects")


def main():
    check = Check(sys.argv[1], sys.argv[2], 1)
    try:
        check.prepare()
        check.alarms()
        check.sla()
        check.presence()
        check.history()
        check.assistant()
        check.exercises()
        check.report["passed"] = True
    finally:
        try:
            check.cleanup()
        finally:
            artifacts = ROOT / "build/operations" / check.preset
            artifacts.mkdir(parents=True, exist_ok=True)
            for name in ("report.json", "servers.log", "commands.log"):
                if (check.output / name).exists():
                    shutil.copyfile(check.output / name, artifacts / name)


if __name__ == "__main__":
    main()
