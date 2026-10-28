"""Verify console read models, scoped snapshots, cache fallback and Kafka reconstruction."""
import json
import os
from pathlib import Path
import re
import runpy
import sys
import urllib.parse

ROOT = Path(__file__).resolve().parents[2]
api = runpy.run_path(str(ROOT / "tools/api-gateway/check.py"))
Base, uid, helpers = api["Check"], api["uid"], api["helpers"]


class Check(Base):
    def principal_cases(self):
        return super().principal_cases() | {"foreign_operator": ("operator", [self.foreign_site], helpers["TENANT"])}

    def envelope(self, sequence):
        return {"messageId": uid(), "tenantId": helpers["TENANT"], "sequence": str(sequence), "occurredAt": helpers["utc"](), "producedAt": helpers["utc"](), "schemaVersion": "1", "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "projection-fixture"}}

    def event(self, sequence, age=0, source_epoch=None, session=None):
        source_epoch = self.current_epoch if source_epoch is None else source_epoch
        return {"topic": "psim.events.normalized.v1", "key": self.site_id + ":" + self.device_row["zoneId"], "value": {"envelope": self.envelope(sequence), "event": {"type": "intrusion.zone.alarm", "eventClass": "EVENT_CLASS_ALARM", "severity": "SEVERITY_HIGH", "siteId": self.site_id, "zoneId": self.device_row["zoneId"], "deviceId": self.device_row["deviceId"], "sourceId": self.source["sourceId"], "connectorId": self.connector, "sourceEpoch": str(source_epoch), "gatewaySessionId": session or self.runtime_lease["session"], "liveObservation": True, "stateObservedAt": helpers["utc"](age)}}}

    def state_report(self, sequence, age=0, source_epoch=None):
        source_epoch = self.current_epoch if source_epoch is None else source_epoch
        return {"topic": "psim.ingest.state.v1", "key": self.connector, "value": {"envelope": self.envelope(sequence), "connectorId": self.connector, "sessionId": self.runtime_lease["session"], "stateReport": {"snapshot": True, "complete": True, "observations": [{"cursor": {"sourceRef": self.source["externalRef"], "sourceEpoch": str(source_epoch), "sourceSeq": str(sequence)}, "observedAt": helpers["utc"](age), "availability": "DEVICE_AVAILABILITY_ONLINE", "values": {"armed": "true"}}]}}}

    def snapshot(self, role="operator"):
        return self.request("GET", "/v1/feed/locations/" + self.floor["locationId"] + "/status", role=role)

    def device_status(self):
        return next((row for row in self.snapshot()["devices"] if row["deviceId"] == self.device_row["deviceId"]), None)

    def positions(self, topic):
        return int(self.sql("SELECT COALESCE(sum(next_offset),0) FROM console.positions WHERE replica=(SELECT generation FROM console.active) AND topic='" + topic + "'"))

    def history(self):
        at = helpers["utc"]()
        path = "/v1/feed/snapshot?" + urllib.parse.urlencode({"at": at, "locationId": self.floor["locationId"]})
        result = self.request("GET", path, role="operator")
        assert result["historical"] and result["availableFrom"]
        assert result["catalogDevices"] and result["catalogLocations"]
        assert not result["feed"].get("resumeToken") and not result["location"].get("resumeToken")
        assert all(not row.get("online", False) for row in result["location"].get("devices", []))
        self.request("GET", path, role="foreign_operator", expected=403)
        before = self.positions("psim.events.normalized.v1")
        self.probe([self.event(12)])
        self.wait(lambda: self.positions("psim.events.normalized.v1") > before, 30)
        assert self.request("GET", path, role="operator") == result
        self.mark("historical feed and plan catalog preserve versions without live leases or resume tokens")

    def metrics_text(self):
        return self.env_command("python3 -c 'import urllib.request; print(urllib.request.urlopen(\"http://projection-service:9100/metrics\").read().decode())'")

    def prepare_models(self):
        assert self.sql("SELECT to_regclass('access.incident_feed') IS NULL AND to_regclass('gateway_state.resources') IS NULL") == "t", "Gateway still creates private projection tables"
        self.device_row = self.request("POST", "/v1/devices", self.device(301))["device"]
        self.source = self.device_row["sources"][0]
        self.current_epoch = 1
        self.runtime_lease = {"tenant": helpers["TENANT"], "connector": self.connector, "session": uid(), "ttlSeconds": 300}
        self.probe(leases=[self.runtime_lease])
        self.wait(lambda: self.device_status(), 30)
        self.request("GET", "/v1/feed/locations/" + self.floor["locationId"] + "/status", role="foreign_operator", expected=403)
        self.probe([self.event(10)])
        fresh = self.wait(lambda: (row if (row := self.device_status())["quality"] == "DATA_QUALITY_ACTUAL" else None), 30)
        assert not fresh["online"] and fresh["sources"][0]["availability"] == "unknown"
        self.probe([self.state_report(10)])
        confirmed = self.wait(lambda: (row if (row := self.device_status())["online"] else None), 30)
        assert confirmed["sources"][0]["values"] == {"armed": "true"}
        # The newer event and the older report must restore identically across topic ordering.
        self.probe([self.event(11)])
        self.wait(lambda: self.device_status()["confirmedAt"] != confirmed["confirmedAt"], 30)
        self.probe([self.state_report(11)])
        self.wait(lambda: self.device_status()["online"], 30)
        before = self.positions("psim.ingest.state.v1")
        obsolete = [self.state_report(100, source_epoch=0), self.state_report(2)]
        for record in obsolete:
            observation = record["value"]["stateReport"]["observations"][0]
            observation["values"]["armed"] = "false"
            observation["availability"] = "DEVICE_AVAILABILITY_OFFLINE"
        self.probe(obsolete)
        self.wait(lambda: self.positions("psim.ingest.state.v1") >= before + 2, 30)
        assert self.device_status()["sources"][0]["values"] == {"armed": "true"} and self.device_status()["online"]
        self.current_epoch = 2
        before = self.positions("psim.events.normalized.v1")
        self.probe([self.event(12)])
        self.wait(lambda: self.positions("psim.events.normalized.v1") >= before + 1, 30)
        assert not self.device_status()["online"]
        assert self.device_status()["sources"][0]["valuesQuality"] == "DATA_QUALITY_STALE"
        self.probe([self.state_report(12)])
        self.wait(lambda: self.device_status()["online"], 30)
        for sequence, online in [(1, False), (2, True)]:
            before = self.positions("psim.ingest.state.v1")
            self.probe([{"topic": "psim.ingest.state.v1", "key": self.connector, "value": {"envelope": self.envelope(sequence), "connectorId": self.connector, "sessionId": self.runtime_lease["session"], "connection": {"online": online, "reason": "Projection verification", "quality": "unknown"}}}])
            self.wait(lambda: self.positions("psim.ingest.state.v1") >= before + 1, 30)
            connector = next(row for row in self.snapshot()["connectors"] if row["connectorId"] == self.connector)
            assert connector["connection"] == ("online" if online else "offline") and connector["resourceState"] == "RESOURCE_STATE_ACTIVE"
            assert self.device_status()["online"] == online
        self.maintenance_id, self.duty_user, self.fallback_user = uid(), uid(), uid()
        window = {"maintenanceId": self.maintenance_id, "siteId": self.site_id, "zoneId": self.device_row["zoneId"], "startsAt": helpers["utc"](-60), "endsAt": helpers["utc"](3600), "reason": "Projection verification", "responsible": "operator", "suppression": {"eventTypes": ["intrusion.zone.alarm"]}, "state": "MAINTENANCE_STATE_ACTIVE", "version": "1"}
        duty = {"siteId": self.site_id, "userId": self.duty_user, "fallbackUserId": self.fallback_user, "version": "1", "updatedAt": helpers["utc"]()}
        self.probe([{"topic": "psim.config.v1", "key": "maintenance/" + helpers["TENANT"] + "/" + self.maintenance_id, "value": {"envelope": self.envelope(1), "maintenanceWindow": window}}, {"topic": "psim.response.events.v1", "key": self.site_id, "value": {"envelope": self.envelope(1), "siteDutyChanged": duty}}])
        self.wait(lambda: any(item["maintenanceId"] == self.maintenance_id for item in self.snapshot()["maintenanceWindows"]), 30)
        def duty_ready():
            value = self.snapshot()
            return value if any(item["userId"] == self.duty_user for item in value["siteDuties"]) else None
        snapshot = self.wait(duty_ready, 30)
        assert json.loads(snapshot["resumeToken"])["generation"]
        loads = self.request("GET", "/v1/feed/operators", role="supervisor")["operators"]
        assert self.site_id in next(item for item in loads if item["userId"] == self.duty_user)["dutySiteIds"]
        assert self.site_id in next(item for item in loads if item["userId"] == self.fallback_user)["fallbackSiteIds"]
        connector = self.request("GET", "/v1/connectors/" + self.connector)["connector"]
        for version, state in [(int(connector["version"]) + 1, "RESOURCE_STATE_DISABLED"), (int(connector["version"]) + 2, "RESOURCE_STATE_ACTIVE")]:
            before = self.positions("psim.catalog.v1")
            self.probe([{"topic": "psim.catalog.v1", "key": "connector:" + self.connector, "value": {"envelope": self.envelope(version), "connector": connector | {"version": str(version), "state": state}}}])
            self.wait(lambda: self.positions("psim.catalog.v1") >= before + 1, 30)
            runtime = next(row for row in self.snapshot()["connectors"] if row["connectorId"] == self.connector)
            assert runtime["resourceState"] == state
            assert self.device_status()["online"] == (state == "RESOURCE_STATE_ACTIVE")
        self.device_maintenance = uid()
        device_window = dict(window, maintenanceId=self.device_maintenance, deviceId=self.device_row["deviceId"])
        device_window.pop("zoneId")
        self.probe([{"topic": "psim.config.v1", "key": "maintenance/" + helpers["TENANT"] + "/" + self.device_maintenance, "value": {"envelope": self.envelope(1), "maintenanceWindow": device_window}}])
        self.wait(lambda: any(row["maintenanceId"] == self.device_maintenance for row in self.snapshot()["maintenanceWindows"]), 30)
        self.mark("shared scoped models, independent event/report cursors, maintenance and duty projections")

    def ignored_events(self):
        topic = "psim.events.normalized.v1"
        self.compose("stop", "-t", "30", "projection-service")
        before = self.positions(topic)
        revision_sql = "SELECT revision FROM console.generations WHERE id=(SELECT generation FROM console.active)"
        revision = int(self.sql(revision_sql))
        observation_sql = "SELECT body::text FROM console.resources WHERE replica=(SELECT generation FROM console.active) AND kind='observation' AND id='" + self.source["sourceId"] + "'"
        observation = self.sql(observation_sql)
        records = [self.event(sequence) for sequence in range(1000, 1512)]
        for record in records:
            record["value"]["event"]["eventClass"] = "EVENT_CLASS_INFO"
        try:
            self.probe(records)
        finally:
            self.compose("start", "projection-service")
        self.wait(lambda: self.positions(topic) >= before + len(records), 60)
        assert self.sql(observation_sql) == observation
        assert int(self.sql(revision_sql)) - revision < 32, "ignored events caused per-message projection writes"
        self.compose("restart", "-t", "30", "projection-service")
        self.wait(lambda: self.device_status(), 60)
        assert self.positions(topic) >= before + len(records)
        assert self.sql(observation_sql) == observation
        self.mark("ignored events checkpoint in batches and survive restart without changing device observations")

    def cache_fallback(self):
        self.snapshot()
        self.snapshot()
        assert re.search(r"psim_console_cache_hits_total [1-9]", self.metrics_text())
        self.compose("stop", "-t", "10", "valkey")
        try:
            self.request("GET", "/v1/feed/locations/" + self.floor["locationId"] + "/status", role="operator", expected=503)
        finally:
            self.compose("start", "--wait", "valkey")
        self.probe(releases=[self.runtime_lease])
        self.probe(leases=[self.runtime_lease])
        self.probe([self.state_report(12)])
        self.wait(lambda: self.device_status()["online"], 30)
        self.compose("exec", "-T", "valkey", "valkey-cli", "--no-auth-warning", "-a", "psim-dev-valkey", "FLUSHDB")
        assert not self.device_status()["online"]
        self.probe(leases=[self.runtime_lease])
        self.probe([self.state_report(13)])
        self.wait(lambda: self.device_status()["online"], 30)
        self.mark("Redis cache hits, fail-closed REST quota, PostgreSQL reads after cache eviction and lease loss without false online")

    def rebuild_command(self, expected=True):
        command = "PSIM_PG_PASSWORD_FILE=/repo/.cache/event-flow/postgres.secret PSIM_PG_SSLMODE=disable PSIM_KAFKA_ALLOW_PLAINTEXT=1 " + f"build/debian13/{self.preset}/services/projection-service/psim-console-tool rebuild"
        try:
            result = self.env_command(command)
        except RuntimeError as error:
            assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", str(error)), str(error)
            if expected:
                raise
            return str(error)
        assert expected, result
        return result

    def digest(self):
        return self.sql("SELECT md5(COALESCE(jsonb_agg(jsonb_build_array(tenant::text,kind,id::text,version,confirmed,session,body) ORDER BY tenant,kind,id)::text,'')) FROM console.resources WHERE replica=(SELECT generation FROM console.active)")

    def reconstruction(self):
        assert "stop the console writer" in self.rebuild_command(False)
        self.compose("stop", "-t", "30", "projection-service")
        try:
            before, generation = self.digest(), self.sql("SELECT generation FROM console.active")
            self.rebuild_command()
            assert self.digest() == before and self.sql("SELECT generation FROM console.active") != generation
            current = self.sql("SELECT generation FROM console.active")
            missing = uid()
            self.sql("INSERT INTO console.resources VALUES('" + current + "','" + helpers["TENANT"] + "','device','" + missing + "',1,NULL,'','{}'::jsonb)")
            baseline = self.digest()
            assert "retained Kafka history differs" in self.rebuild_command(False)
            assert self.sql("SELECT generation FROM console.active") == current and self.digest() == baseline
            self.sql("DELETE FROM console.resources WHERE replica='" + current + "' AND id='" + missing + "'")
        finally:
            self.compose("start", "projection-service")
        self.wait(lambda: self.device_status(), 90)
        assert any(item["maintenanceId"] == self.maintenance_id for item in self.snapshot()["maintenanceWindows"])
        self.mark("exclusive writer fencing, identical Kafka rebuild and atomic rejection of incomplete history")

    def latency(self):
        def count(text, bucket=False):
            if not bucket:
                return float(re.search(r"^psim_console_update_seconds_count ([0-9.e+-]+)$", text, re.MULTILINE)[1])
            values = re.findall(r'^psim_console_update_seconds_bucket\{le="([^"]+)"\} ([0-9.e+-]+)$', text, re.MULTILINE)
            return next(float(value) for label, value in values if float(label) == 0.1)
        before = self.metrics_text()
        total = 300
        history_sql = "SELECT count(*) FROM console.history WHERE kind='observation' AND id='" + self.source["sourceId"] + "'"
        history_before = int(self.sql(history_sql))
        self.probe([self.event(sequence) for sequence in range(20, 20 + total)])
        self.wait(lambda: self.sql("SELECT version FROM console.resources WHERE replica=(SELECT generation FROM console.active) AND kind='observation' AND id='" + self.source["sourceId"] + "'") == str(19 + total), 60)
        assert int(self.sql(history_sql)) - history_before == total, "batched projection lost intermediate history"
        after = self.metrics_text()
        measured = count(after) - count(before)
        slow = measured - (count(after, True) - count(before, True))
        assert measured >= total, measured
        if self.preset in ("debug", "release", "relwithdebinfo"):
            assert slow / measured <= 0.01, (slow, measured)
        self.report["consoleUpdateLatency"] = {"samples": measured, "over100ms": slow, "p99UpperBoundMs": 100 if slow / measured <= 0.01 else None, "source": "Kafka CreateTime to PostgreSQL COMMIT"}
        self.mark("live console projection latency measured independently of replay")

    def atomic_batch_failure(self):
        self.compose("stop", "-t", "30", "projection-service")
        before = self.digest()
        position = self.positions("psim.events.normalized.v1")
        history = self.sql("SELECT count(*) FROM console.history")
        records = [self.event(sequence) for sequence in range(5000, 5130)]
        records[-1]["value"]["event"]["sourceId"] = "invalid-source"
        self.probe(records)
        self.compose("start", "projection-service")
        self.wait(lambda: "PROCESSING_PAYLOAD_INVALID" in self.compose("logs", "--no-color", "--tail", "100", "projection-service"), 60)
        self.compose("stop", "-t", "30", "projection-service")
        assert self.digest() == before, "failed projection batch changed resources"
        assert self.positions("psim.events.normalized.v1") == position, "failed projection batch advanced offsets"
        assert self.sql("SELECT count(*) FROM console.history") == history, "failed projection batch changed history"
        self.mark("invalid batch rolls back pipelined resource writes, history and Kafka positions together")

    def expiry_after_rebuild(self):
        device = self.request("POST", "/v1/devices", self.device(902))["device"]
        previous, self.device_row, self.source = (self.device_row, self.source), device, device["sources"][0]
        self.probe([self.event(1, -120), self.state_report(1, -120)])
        self.wait(lambda: self.device_status() and self.device_status()["quality"] == "DATA_QUALITY_STALE", 30)
        self.compose("stop", "-t", "30", "projection-service")
        try:
            self.rebuild_command()
        finally:
            self.compose("start", "projection-service")
        stale = self.wait(lambda: self.device_status(), 90)
        assert stale["quality"] == "DATA_QUALITY_STALE" and not stale["online"]
        self.probe([self.state_report(2)])
        self.wait(lambda: self.device_status()["online"], 30)
        self.device_row, self.source = previous
        self.mark("historical replay remains stale until a fresh state report of the current session")



    def zone_scope(self):
        admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        root = "/admin/realms/psim/clients/"
        client = self.keycloak("GET", root + "?clientId=" + self.tokens["operator"]["client"], admin)[0]["id"]
        mapping = next(row for row in self.keycloak("GET", root + client + "/protocol-mappers/models", admin) if row.get("config", {}).get("claim.name") == "psim_scopes")
        mapping["config"]["claim.value"] = json.dumps(["zone:" + self.device_row["zoneId"]])
        self.keycloak("PUT", root + client + "/protocol-mappers/models/" + mapping["id"], admin, mapping)
        self.tokens["operator"]["expires"] = 0
        snapshot = self.request("GET", "/v1/feed/locations/" + self.device_row["zoneId"] + "/status", role="operator")
        assert {row["zoneId"] for row in snapshot["zones"]} == {self.device_row["zoneId"]}
        assert any(row["deviceId"] == self.device_row["deviceId"] for row in snapshot["devices"])
        assert any(row["maintenanceId"] == self.device_maintenance for row in snapshot["maintenanceWindows"])
        self.request("GET", "/v1/feed/locations/" + self.zones[0]["locationId"] + "/status", role="operator", expected=403)
        self.mark("zone scopes share cached data without exposing other zones and retain device maintenance")

    def cleanup(self):
        self.compose("stop", "-t", "30", "projection-service")
        logs = self.compose("logs", "--no-color", "projection-service")
        (self.output / "projection-servers.log").write_text(logs)
        try:
            if self.report["passed"]:
                identity = self.compose("ps", "-aq", "projection-service").strip()
                state = json.loads(self.run(["docker", "inspect", identity]))[0]["State"]
                assert state["ExitCode"] == 0, state
                assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs), "projection sanitizer finding"
        finally:
            super().cleanup()


def main():
    check = Check(*sys.argv[1:])
    try:
        check.prepare()
        check.security()
        check.basic()
        check.settings()
        check.streaming()
        check.snapshots()
        check.feeds()
        check.prepare_models()
        check.ignored_events()
        check.history()
        check.cache_fallback()
        check.reconstruction()
        check.latency()
        check.expiry_after_rebuild()
        check.zone_scope()
        check.contracts()
        check.metrics()
        check.cancel_inflight()
        check.atomic_batch_failure()
        check.report["passed"] = True
    finally:
        try:
            check.cleanup()
        except Exception:
            check.report["passed"] = False
            raise
        finally:
            (check.output / "projection-result.json").write_text(json.dumps(check.report, indent=2) + "\n")
    print(json.dumps(check.report, indent=2))


if __name__ == "__main__":
    main()
