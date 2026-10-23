"""Exercise the public Gateway with real JWTs, Catalog, Kafka and private file storage."""
import concurrent.futures
import http.client
import json
import os
from pathlib import Path
import runpy
import ssl
import subprocess
import sys
import urllib.error
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(ROOT / "tools/catalog/check.py"))
Base, uid, png = (helpers[k] for k in ("Check", "uid", "png"))


class Check(Base):
    def __init__(self, project, preset):
        super().__init__(project, preset, 1)
        self.captures = []
        self.public = False
        self.runtime_lease = None
        self.env["PSIM_OIDC_ISSUER"] = "http://keycloak:8080/realms/psim"
        self.report = {"passed": False, "checks": []}
        services = {"api-gateway": {"ports": ["127.0.0.1::8443"], "command": [f"/repo/build/debian13/{preset}/services/api-gateway/psim-api-gateway", "--config", "/repo/build/resource-catalog/" + preset + "/api.json"]}}
        with self.override.open("a") as file:
            file.write("  api-gateway:\n    mem_limit: " + ("1024m" if preset in ("asan", "tsan") else "128m") + "\n    ports: ['127.0.0.1::8443']\n    command: " + json.dumps(services["api-gateway"]["command"]) + "\n")
        settings = {"allow_plaintext": True, "listen_port": 8443, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "issuer": "http://keycloak:8080/realms/psim", "tls_certificate_file": "/repo/.cache/event-flow/api-server.pem", "tls_private_key_file": "/repo/.cache/event-flow/api-server.key", "upstream_ca_file": "/repo/.cache/event-flow/ca.pem", "allowed_origins": ["https://console.example"], "redis_host": "valkey", "redis_password_file": "/repo/.cache/event-flow/redis.secret", "rate_per_second": 100, "rate_burst": 100, "ip_rate_per_second": 10000, "ip_rate_burst": 10000}
        config = {"service": {"instance": "public-api"}, "runtime": {"shards": 1}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.01}, "settings": settings}
        (self.output / "api.json").write_text(json.dumps(config))

    def principal_cases(self):
        return super().principal_cases() | {"supervisor": ("supervisor", [self.site_id], helpers["TENANT"])}

    def request(self, method, path, *args, **kwargs):
        if self.public:
            kwargs.setdefault("service", "api-gateway")
        try:
            result = super().request(method, path, *args, **kwargs)
        except AssertionError as error:
            if "got 401" in str(error) and kwargs.get("expected", 200) != 401:
                import base64
                role = kwargs.get("role", "admin")
                payload = self.token(role).split(".")[1]
                claims = json.loads(base64.urlsafe_b64decode(payload + "=" * (-len(payload) % 4)))
                self.report["unexpectedAuthentication"] = {key: claims.get(key) for key in ("iss", "aud", "typ", "iat", "exp", "psim_tenant", "psim_scopes", "resource_access")}
            raise
        if self.public and isinstance(result, dict):
            status = kwargs.get("expected", 200)
            self.captures.append({"method": method, "path": path, "status": status, "media": "application/problem+json" if status >= 400 else "application/json", "body": result})
        return result

    def public_request(self, method, path, headers=None, body=None):
        headers = {"Authorization": "Bearer " + self.token("admin")} | (headers or {})
        connection = http.client.HTTPSConnection("localhost", int(self.port("api-gateway", 8443)), context=self.ca, timeout=30)
        connection.request(method, path, body=body, headers=headers)
        response = connection.getresponse()
        result = response.status, dict(response.getheaders()), response.read()
        connection.close()
        return result

    def prepare(self):
        super().prepare()
        self.compose("up", "-d", "--force-recreate", "projection-service", "api-gateway")
        self.wait(lambda: self.request("GET", "/v1/me", service="api-gateway"), 90)
        self.public = True
        self.mark("public HTTPS Gateway with verified Keycloak tokens and TLS upstream")

    def security(self):
        self.request("GET", "/v1/me", role=None, expected=401)
        self.request("GET", "/v1/settings", role="operator", expected=403)
        self.request("GET", "/v1/incidents/" + uid(), role="operator", expected=503)
        status, headers, raw = self.public_request("GET", "/v1/me", {"Authorization": "Bearer invalid"})
        assert status == 401 and headers["Content-Type"] == "application/problem+json", raw
        status, headers, _ = self.public_request("OPTIONS", "/v1/locations", {"Origin": "https://console.example", "Access-Control-Request-Method": "POST", "Authorization": ""})
        assert status == 204 and headers["Access-Control-Allow-Origin"] == "https://console.example"
        status, _, _ = self.public_request("GET", "/v1/me", {"Origin": "https://denied.example"})
        assert status == 403
        status, headers, _ = self.public_request("GET", "/v1/locations")
        assert status == 200 and headers["X-Content-Type-Options"] == "nosniff"
        assert "Content-Security-Policy" in headers
        status, _, _ = self.public_request("POST", "/v1/locations", {"Content-Type": "application/json"}, b"{}")
        assert status == 400
        self.wait(lambda: int(self.sql("SELECT count(*) FROM access.psim_outbox")) >= 3, 30)
        self.mark("CORS, bearer failures, role checks, request preconditions and durable denial audit")

    def settings(self):
        current = self.request("GET", "/v1/settings")
        original = current["retention"]
        body = original | {"eventHistoryAll": "86400s"}
        key = uid()
        first = self.request("PUT", "/v1/settings/retention", body, version=original["version"], key=key)
        assert first == self.request("PUT", "/v1/settings/retention", body, version=original["version"], key=key)
        self.request("PUT", "/v1/settings/retention", body, version=original["version"], expected=412)
        self.request("PUT", "/v1/settings/retention", body | {"audit": "-1s"}, version=first["retention"]["version"], expected=400)
        mapping = {"connectorType": "gateway-" + self.fixtures, "taxonomyVersion": "1", "rules": [{"rawCode": "ALARM*", "condition": 'raw.raw_code.startsWith("ALARM")', "type": "intrusion.zone.alarm"}]}
        path = "/v1/mappings/" + mapping["connectorType"]
        saved = self.request("PUT", path, mapping, version=0)["mappingRuleSet"]
        assert self.request("GET", path)["mappingRuleSet"] == saved
        self.request("PUT", path, mapping | {"rules": [{"rawCode": "A*B", "type": "intrusion.zone.alarm"}]}, version=saved["version"], expected=400)
        self.request("PUT", path, mapping | {"rules": [{"rawCode": "A", "condition": "raw.execute()", "type": "intrusion.zone.alarm"}]}, version=saved["version"], expected=400)
        assert any(item["connectorType"] == mapping["connectorType"] for item in self.request("GET", "/v1/mappings")["mappingRuleSets"])
        self.request("GET", path, role="site", expected=403)
        processing = current["processing"]
        updated = self.request("PUT", "/v1/settings/processing", processing | {"sourceIdleTimeout": "90s"}, version=processing["version"])["processing"]
        self.request("PUT", "/v1/settings/processing", updated | {"significanceThreshold": "SEVERITY_UNSPECIFIED"}, version=updated["version"], expected=400)
        self.wait(lambda: self.sql("SELECT count(*) FROM catalog.psim_outbox WHERE published_at IS NULL") == "0", 30)
        self.compose("stop", "-t", "30", "resource-catalog", "catalog-2")
        self.sql("DELETE FROM catalog.configuration WHERE tenant='" + helpers["TENANT"] + "'::uuid")
        self.compose("start", "resource-catalog", "catalog-2")
        self.wait(lambda: self.request("GET", path)["mappingRuleSet"] == saved, 60)
        recovered = self.request("GET", "/v1/settings")
        assert recovered["retention"] == first["retention"] and recovered["processing"] == updated
        self.mark("mapping and processing versions, optimistic concurrency, idempotency, outbox and Kafka bootstrap recovery")

    def probe(self, records=None, leases=None, releases=None):
        path = self.output / "api-fixture.json"
        path.write_text(json.dumps({"records": records or [], "leases": leases or [], "releases": releases or []}))
        self.env_command(f"build/debian13/{self.preset}/tools/api-gateway/psim-api-probe /repo/{path.relative_to(ROOT)}")

    def snapshots(self):
        device = self.request("POST", "/v1/devices", self.device(100))["device"]
        path = "/v1/feed/locations/" + self.floor["locationId"] + "/status"
        def status():
            response = self.request("GET", path, role="operator")
            assert response["resumeToken"] and response["snapshotAt"]
            return next((item for item in response["devices"] if item["deviceId"] == device["deviceId"]), None)
        self.wait(lambda: (found if (found := status()) and found["quality"] == "DATA_QUALITY_UNKNOWN" else None), 30)
        self.request("GET", path, role="other", expected=403)
        session = uid()
        lease = {"tenant": helpers["TENANT"], "connector": self.connector, "session": session}
        self.probe(leases=[lease])
        self.runtime_lease = lease
        source = device["sources"][0]
        def event(sequence, age=0, live=True):
            envelope = {"messageId": uid(), "tenantId": helpers["TENANT"], "sequence": str(sequence), "occurredAt": helpers["utc"](age), "producedAt": helpers["utc"](), "schemaVersion": "1", "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "api-probe"}}
            value = {"type": "intrusion.zone.alarm", "eventClass": "EVENT_CLASS_ALARM", "severity": "SEVERITY_HIGH", "siteId": self.site_id, "zoneId": device["zoneId"], "deviceId": device["deviceId"], "sourceId": source["sourceId"], "connectorId": self.connector, "sourceEpoch": 1, "gatewaySessionId": session, "liveObservation": live, "stateObservedAt": helpers["utc"](age)}
            return {"topic": "psim.events.normalized.v1", "key": self.site_id + ":" + device["zoneId"], "value": {"envelope": envelope, "event": value}}
        self.probe([event(1, -120)])
        self.wait(lambda: status()["quality"] == "DATA_QUALITY_STALE", 30)
        self.probe([event(10)])
        current = self.wait(lambda: (found if (found := status())["quality"] == "DATA_QUALITY_ACTUAL" else None), 30)
        assert not current["online"] and current["connection"] == "unknown"
        def report(sequence):
            origin = {"messageId": uid(), "tenantId": helpers["TENANT"], "sequence": "1", "occurredAt": helpers["utc"](), "producedAt": helpers["utc"](), "schemaVersion": "1", "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "api-probe"}}
            return {"topic": "psim.ingest.state.v1", "key": self.connector, "value": {"envelope": origin, "connectorId": self.connector, "sessionId": session, "stateReport": {"snapshot": True, "complete": True, "observations": [{"cursor": {"sourceRef": source["externalRef"], "sourceEpoch": 1, "sourceSeq": str(sequence)}, "observedAt": helpers["utc"](), "availability": "DEVICE_AVAILABILITY_ONLINE"}]}}}
        self.probe([report(10)])
        self.wait(lambda: status()["online"], 30)
        boundary = int(self.sql("SELECT COALESCE(sum(next_offset),0) FROM console.positions WHERE replica=(SELECT generation FROM console.active) AND topic='psim.ingest.state.v1'"))
        self.probe([report(2)])
        self.wait(lambda: int(self.sql("SELECT COALESCE(sum(next_offset),0) FROM console.positions WHERE replica=(SELECT generation FROM console.active) AND topic='psim.ingest.state.v1'")) >= boundary + 1, 30)
        assert status()["online"]
        self.probe([event(11)])
        current = self.wait(lambda: (found if (found := status())["state"] == "EVENT_CLASS_ALARM" else None), 30)
        confirmed = current["confirmedAt"]
        before = int(self.sql("SELECT COALESCE(sum(next_offset),0) FROM console.positions WHERE replica=(SELECT generation FROM console.active) AND topic='psim.events.normalized.v1'"))
        info = event(20, 2)
        info["value"]["event"].update({"eventClass": "EVENT_CLASS_INFO", "type": "access.door.granted"})
        self.probe([event(20, -120, False), event(2, 1), info])
        self.wait(lambda: int(self.sql("SELECT COALESCE(sum(next_offset),0) FROM console.positions WHERE replica=(SELECT generation FROM console.active) AND topic='psim.events.normalized.v1'")) >= before + 3, 30)
        assert status()["confirmedAt"] == confirmed
        assert status()["state"] == "EVENT_CLASS_ALARM"
        self.probe(releases=[lease])
        session = uid()
        lease = lease | {"session": session}
        self.probe(leases=[lease])
        self.runtime_lease = lease
        self.probe([event(12)])
        rotated = self.wait(lambda: (found if (found := status())["quality"] == "DATA_QUALITY_ACTUAL" and found["confirmedAt"] != confirmed else None), 30)
        assert not rotated["online"] and rotated["connection"] == "unknown"
        self.probe(releases=[lease])
        self.wait(lambda: status()["quality"] == "DATA_QUALITY_STALE", 30)
        self.compose("restart", "api-gateway")
        self.wait(lambda: status()["quality"] == "DATA_QUALITY_STALE", 60)
        assert not status()["online"]
        self.mark("scoped snapshots, quality, resume positions, replay ordering and freshness after restart")

    def feeds(self):
        identity, assignee = uid(), uid()
        incident = {"incidentId": identity, "incidentTypeId": uid(), "siteId": self.site_id, "zoneId": self.zones[0]["locationId"], "state": "INCIDENT_STATE_IN_PROGRESS", "priority": "PRIORITY_P1", "title": "API fixture", "assigneeId": assignee, "escalationLevel": 1, "createdAt": helpers["utc"](), "version": "1"}
        def emit(version, change, body):
            envelope = {"messageId": uid(), "tenantId": helpers["TENANT"], "sequence": str(version), "occurredAt": helpers["utc"](), "producedAt": helpers["utc"](), "schemaVersion": "1", "actor": {"kind": "ACTOR_KIND_SERVICE", "id": "api-probe"}}
            self.probe([{"topic": "psim.incidents.events.v1", "key": identity, "value": {"envelope": envelope, change: {"incident": body | {"version": str(version)}}}}])
        emit(1, "incidentCreated", incident)
        feed = "/v1/feed/incidents"
        self.wait(lambda: any(item["incidentId"] == identity for item in self.request("GET", feed, role="operator")["incidents"]), 30)
        filtered = self.request("GET", feed + "?siteIds=" + self.foreign_site, role="operator")
        assert filtered["incidents"] == []
        self.request("GET", feed + "?siteIds=invalid", role="operator", expected=400)
        self.request("GET", "/v1/feed/operators", role="operator", expected=403)
        loads = self.request("GET", "/v1/feed/operators?siteIds=" + self.site_id, role="supervisor")
        assert loads["resumeToken"] and loads["snapshotAt"]
        assert next(item for item in loads["operators"] if item["userId"] == assignee)["escalated"] == 1
        emit(2, "incidentClosed", incident | {"state": "INCIDENT_STATE_CLOSED"})
        self.wait(lambda: all(item["incidentId"] != identity for item in self.request("GET", feed, role="operator")["incidents"]), 30)
        assert all(item["userId"] != assignee for item in self.request("GET", "/v1/feed/operators", role="supervisor")["operators"])
        self.mark("scoped incident feed and supervisor loads follow lifecycle changes")

    def streaming(self):
        path = "/v1/locations/" + self.floor["locationId"] + "/floor-plan"
        small = png()
        self.floor = self.request("PUT", path + "?width=100&height=100", small, version=self.floor["version"], content_type="image/png")["location"]
        identity = self.compose("ps", "-q", "api-gateway").strip()
        def memory():
            return int(self.run(["docker", "exec", identity, "sh", "-c", "sed -n 's/^VmRSS:[[:space:]]*\\([0-9]*\\).*/\\1/p' /proc/1/status"]).strip()) * 1024
        before = memory()
        data = png(padding=20 * 1024 * 1024 - len(png()) - 20)
        assert len(data) == 20 * 1024 * 1024
        self.floor = self.request("PUT", path + "?width=100&height=100", data, version=self.floor["version"], content_type="image/png")["location"]
        status, headers, downloaded = self.public_request("GET", path)
        assert status == 200 and downloaded == data
        assert headers["Content-Type"] == "image/png" and headers["Content-Disposition"].startswith("attachment;")
        assert headers["ETag"] and headers["X-Content-Type-Options"] == "nosniff"
        after = memory()
        self.report["gatewayRssGrowthBytes"] = max(0, after - before)
        self.report["gatewayRssBytes"] = after
        self.report["gatewayMemoryLimitBytes"] = (1024 if self.preset in ("asan", "tsan") else 128) * 1024 * 1024
        if self.preset in ("asan", "tsan"):
            assert after < 1024 * 1024 * 1024, after
        else:
            assert after - before < 12 * 1024 * 1024, (before, after)
            assert after < 128 * 1024 * 1024, after
        self.request("GET", path, role="other", expected=403)
        connection = http.client.HTTPSConnection("localhost", int(self.port("api-gateway", 8443)), context=self.ca, timeout=10)
        connection.putrequest("PUT", path)
        connection.putheader("Authorization", "Bearer " + self.token("admin"))
        connection.putheader("If-Match", '"' + self.floor["version"] + '"')
        connection.putheader("Content-Length", str(len(data) + 1))
        connection.endheaders()
        response = connection.getresponse()
        assert response.status == 413, response.status
        response.read()
        connection.close()
        self.mark("exact 20 MiB upload/download, metadata headers, scope and bounded Gateway memory")

    def metrics(self):
        self.request("GET", "/v1/me", role=None, expected=401)
        self.request("GET", "/v1/incidents/" + uid(), role="operator", expected=503)
        text = self.env_command("python3 -c 'import urllib.request; print(urllib.request.urlopen(\"http://api-gateway:9100/metrics\").read().decode())'")
        import re
        for name in ("psim_api_access_denied_total", "psim_api_proxy_failures_total"):
            found = re.search(r"^" + name + r" ([0-9.e+-]+)$", text, re.MULTILINE)
            assert found and float(found[1]) >= 1, name
        assert 'psim_api_requests_total{operation="CatalogService_UploadFloorPlan"}' in text
        assert "psim_api_request_seconds_bucket" in text
        assert "psim_api_requests_active" in text
        self.mark("bounded REST operation metrics, access denials and proxy failures exported")

    def cancel_inflight(self):
        import time
        self.compose("kill", "-s", "SIGSTOP", "resource-catalog")
        pool = concurrent.futures.ThreadPoolExecutor(max_workers=1)
        future = pool.submit(self.public_request, "GET", "/v1/locations")
        try:
            identity = self.compose("ps", "-q", "resource-catalog").strip()
            def connected():
                rows = self.run(["docker", "exec", identity, "cat", "/proc/net/tcp"]).splitlines()[1:]
                return any((fields := row.split())[1].endswith(":20FB") and fields[3] == "01" for row in rows)
            self.wait(connected, 10)
            started = time.monotonic()
            self.compose("stop", "-t", "5", "api-gateway")
            assert time.monotonic() - started < 5, "Gateway drain exceeded cancellation budget"
            gateway = self.compose("ps", "-aq", "api-gateway").strip()
            assert json.loads(self.run(["docker", "inspect", gateway]))[0]["State"]["ExitCode"] == 0
            try:
                future.result(timeout=5)
            except (OSError, http.client.HTTPException):
                pass
            else:
                raise AssertionError("stalled request unexpectedly completed")
        finally:
            self.compose("kill", "-s", "SIGCONT", "resource-catalog")
            pool.shutdown(wait=True)
        self.mark("Gateway drain cancels a stalled TLS upstream without waiting for its deadline")

    def contracts(self):
        capture_path = self.output / "api-responses.json"
        capture_path.write_text(json.dumps(self.captures))
        self.run(["docker", "run", "--rm", "-u", f"{os.getuid()}:{os.getgid()}", "--network", "none", "-v", str(ROOT) + ":/repo:ro", "-w", "/repo", "psim-platform/api-check:" + __import__("hashlib").sha256((ROOT / "tools/api-gateway/image/Dockerfile").read_bytes()).hexdigest()[:12], "python3", "-I", "tools/api-gateway/schema_check.py", str(capture_path.relative_to(ROOT))])
        self.mark("public JSON responses conform to committed OpenAPI")

    def cleanup(self):
        if self.runtime_lease:
            try:
                self.probe(releases=[self.runtime_lease])
            except RuntimeError as error:
                print("lease cleanup failed: " + str(error), flush=True)
            self.runtime_lease = None
        self.compose("stop", "-t", "30", "api-gateway")
        logs = self.compose("logs", "--no-color", "api-gateway")
        (self.output / "api-servers.log").write_text(logs)
        if self.report["passed"]:
            import re
            identity = self.compose("ps", "-aq", "api-gateway").strip()
            state = json.loads(self.run(["docker", "inspect", identity]))[0]["State"]
            assert state["ExitCode"] == 0, state["ExitCode"]
            assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs), "Gateway sanitizer finding"
        self.compose("stop", "-t", "30", "projection-service")
        try:
            super().cleanup()
        finally:
            subprocess.run(self.compose_cmd + ["stop", "-t", "30", "resource-catalog", "catalog-2", "connector-gateway", "projection-service"], cwd=ROOT, env=self.env, check=True, capture_output=True)


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
        check.contracts()
        check.metrics()
        check.cancel_inflight()
        check.report["passed"] = True
    finally:
        try:
            check.cleanup()
        except Exception:
            check.report["passed"] = False
            raise
        finally:
            (check.output / "api-result.json").write_text(json.dumps(check.report, indent=2) + "\n")
    print(json.dumps(check.report, indent=2))


if __name__ == "__main__":
    main()
