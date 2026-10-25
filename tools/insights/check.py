"""Verify audit and reporting browsers against native services, signed files and database tampering."""
import csv
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
reports = runpy.run_path(str(ROOT / "tools/reports/check.py"))
audit = runpy.run_path(str(ROOT / "tools/audit/check.py"))
PLAYWRIGHT = "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27"
uid = reports["uid"]


class Check(reports["Check"]):
    sql = audit["Check"].sql
    emit_records = audit["Check"].emit_records

    def __init__(self, project, preset):
        super().__init__(project, preset, 10)
        previous = self.output
        self.log.close()
        self.output = ROOT / "build/insights" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        for artifact in ("browser.json", "audit-browser.json", "privacy-browser.json", "psim-audit.jsonl", "psim-incidents.csv"):
            (self.output / artifact).unlink(missing_ok=True)
        services = json.loads(self.override.read_text())["services"]
        services = json.loads(json.dumps(services).replace(str(previous.relative_to(ROOT)), str(self.output.relative_to(ROOT))))
        self.incident_config = self.output / "incident.json"
        services["incident-service"]["command"][-1] = "/repo/" + str(self.incident_config.relative_to(ROOT))
        self.override = self.output / "compose.json"
        self.override.write_text(json.dumps({"services": services}))
        self.compose_cmd[-1] = str(self.override)
        self.compose_cmd += ["--profile", "history-test", "--profile", "event-flow"]
        self.archive = self.output / ("checkpoints-" + self.fixtures)
        self.archive.mkdir()
        self.tool = f"build/debian13/{preset}/services/audit-service/psim-audit-tool"

    def setup_browser(self):
        self.admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        clients, users = "/admin/realms/psim/clients", "/admin/realms/psim/users"
        self.roles_client = roles_client = self.keycloak("GET", clients + "?clientId=psim", self.admin)[0]["id"]
        directory = "psim-insights-directory-" + self.fixtures
        self.keycloak("POST", clients, self.admin, {"clientId": directory, "enabled": True, "serviceAccountsEnabled": True, "standardFlowEnabled": False, "secret": "psim-dev-directory"})
        client = self.keycloak("GET", clients + "?clientId=" + directory, self.admin)[0]["id"]
        account = self.keycloak("GET", clients + "/" + client + "/service-account-user", self.admin)["id"]
        management = self.keycloak("GET", clients + "?clientId=realm-management", self.admin)[0]["id"]
        roles = [self.keycloak("GET", clients + "/" + management + "/roles/" + name, self.admin) for name in ("view-users", "view-clients", "view-events", "view-realm")]
        self.keycloak("POST", users + "/" + account + "/role-mappings/clients/" + management, self.admin, roles)
        self.keycloak("PUT", "/admin/realms/psim/events/config", self.admin, {"eventsEnabled": True, "eventsExpiration": 86400, "enabledEventTypes": ["LOGIN", "LOGIN_ERROR", "LOGOUT", "REFRESH_TOKEN_ERROR"], "adminEventsEnabled": True, "adminEventsDetailsEnabled": False})
        secret = self.output / "directory.secret"
        secret.unlink(missing_ok=True); secret.write_text("psim-dev-directory"); secret.chmod(0o400)
        incident = json.loads(self.incident_config.read_text())
        incident["settings"].update(directory_base="http://keycloak:8080", directory_client=directory, directory_roles_client=roles_client, directory_secret_file="/repo/" + str(secret.relative_to(ROOT)))
        self.incident_config.write_text(json.dumps(incident))
        fixture = {"password": "psim-dev-Passw0rd", "siteId": self.site_id, "foreignSiteId": self.foreign_site, "from": self.interval["filter.from"], "to": self.interval["filter.to"]}
        for name, role, scopes in (("auditor", "auditor", ["*"]), ("manager", "manager", ["*"]), ("scoped", "auditor", [self.site_id])):
            username = "insights-" + name + "-" + self.fixtures
            self.keycloak("POST", users, self.admin, {"username": username, "enabled": True, "emailVerified": True, "requiredActions": ["CONFIGURE_TOTP"], "firstName": name, "lastName": "Browser", "email": username + "@example.test", "attributes": {"psim_tenant": [self.tenant], "psim_scopes": scopes}, "credentials": [{"type": "password", "value": fixture["password"], "temporary": False}]})
            user = self.keycloak("GET", users + "?username=" + username, self.admin)[0]["id"]
            body = self.keycloak("GET", clients + "/" + roles_client + "/roles/" + role, self.admin)
            self.keycloak("POST", users + "/" + user + "/role-mappings/clients/" + roles_client, self.admin, [body])
            fixture[name] = username
            fixture[name + "UserId"] = user
        self.sql("CREATE ROLE psim_audit LOGIN PASSWORD 'psim-test-audit'")
        self.sql("GRANT CONNECT ON DATABASE psim TO psim_audit")
        self.env_command(f"PSIM_PG_HOST=postgres PSIM_PG_SSLMODE=disable PSIM_PG_PASSWORD=psim-dev-psim {self.tool} migrate psim_audit")
        signing, trusted, password = (self.output / name for name in ("signing.pem", "trusted-keys.json", "audit.secret"))
        signing.unlink(missing_ok=True)
        self.env_command("openssl genpkey -algorithm ED25519 -out " + str(signing.relative_to(ROOT)))
        public = self.env_command("openssl pkey -in " + str(signing.relative_to(ROOT)) + " -pubout")
        self.env_command("openssl pkey -in " + str(signing.relative_to(ROOT)) + " -pubout -outform DER -out " + str((self.output / "public.der").relative_to(ROOT)))
        identity = hashlib.sha256((self.output / "public.der").read_bytes()[-32:]).hexdigest()
        trusted.write_text(json.dumps({identity: public})); signing.chmod(0o400)
        password.unlink(missing_ok=True); password.write_text("psim-test-audit"); password.chmod(0o400)
        config = json.loads((self.output / "response.json").read_text())
        config["settings"].update(pg_user="psim_audit", pg_password_file="/repo/" + str(password.relative_to(ROOT)), signing_key_file="/repo/" + str(signing.relative_to(ROOT)), trusted_keys_file="/repo/" + str(trusted.relative_to(ROOT)), checkpoint_directory="/checkpoints", checkpoint_interval_ms=1000, checkpoint_records=10, identity_base="http://keycloak:8080", identity_client=directory, identity_secret_file="/repo/" + str(secret.relative_to(ROOT)), identity_security_tenant=self.tenant)
        audit_config = self.output / "audit.json"; audit_config.write_text(json.dumps(config))
        gateway_config = json.loads((self.output / "response.json").read_text())
        gateway_config["settings"].update(tls_certificate_file="/repo/.cache/event-flow/api-server.pem", tls_private_key_file="/repo/.cache/event-flow/api-server.key", upstream_ca_file="/repo/.cache/event-flow/ca.pem", allowed_origins=["http://localhost:4173"], redis_host="valkey", redis_password_file="/repo/.cache/event-flow/redis.secret", rate_per_second=100, rate_burst=100, ip_rate_per_second=10000, ip_rate_burst=10000)
        gateway_path = self.output / "gateway.json"; gateway_path.write_text(json.dumps(gateway_config))
        services = json.loads(self.override.read_text())["services"]
        base = services["response-engine"]
        services["audit-service"] = base | {"command": [f"/repo/build/debian13/{self.preset}/services/audit-service/psim-audit-service", "--config", "/repo/" + str(audit_config.relative_to(ROOT))], "volumes": base["volumes"] + [str(self.archive) + ":/checkpoints"]}
        services["api-gateway"] = base | {"command": [f"/repo/build/debian13/{self.preset}/services/api-gateway/psim-api-gateway", "--config", "/repo/" + str(gateway_path.relative_to(ROOT))]}
        assets = self.output / "console"; shutil.rmtree(assets, ignore_errors=True); shutil.copytree(ROOT / "web/apps/console/dist", assets)
        services["console"] = {"volumes": [str(assets) + ":/repo/web/apps/console/dist:ro"], "environment": {"PSIM_API_UPSTREAM": "https://api-gateway:8443", "NODE_EXTRA_CA_CERTS": "/repo/.cache/event-flow/ca.pem"}}
        self.override.write_text(json.dumps({"services": services}))
        (ROOT / ".cache/event-flow" / (self.project + ".config.js")).write_text('window.psimConfig={"issuer":"http://keycloak:8080/realms/psim"};\n')
        self.apps += ["audit-service", "api-gateway", "console"]
        self.compose("up", "-d", "--wait", "valkey")
        self.compose("up", "-d", "--force-recreate", "incident-service", "audit-service", "api-gateway", "console")
        entries = [{"topic": "psim.audit.v1", "key": self.tenant, "value": {"envelope": {"tenantId": self.tenant, "causationId": uid()}, "auditEntry": {"actor": {"id": "browser-fixture", "kind": "ACTOR_KIND_SERVICE"}, "action": "fixture.ui.action", "objectType": "incident", "objectId": self.fixtures, "siteId": self.site_id, "outcome": "AUDIT_OUTCOME_SUCCESS", "details": {"message": "browser acceptance"}}}} for _ in range(101)]
        self.emit_records(entries)
        self.wait(lambda: self.sql("SELECT count(*) FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='fixture.ui.action'") == "101", 90)
        first = json.loads(self.sql("SELECT body::text FROM audit.entries WHERE tenant='" + self.tenant + "' AND body->'entry'->>'action'='fixture.ui.action' ORDER BY seq LIMIT 1"))
        fixture["tamperSeq"] = first["auditSeq"]
        (self.output / "fixture.json").write_text(json.dumps(fixture))
        self.wait(lambda: self.request("GET", "/v1/audit/records", role="auditor", service="api-gateway"), 90)
        return first

    def browser(self, original):
        for stage in ("modify", "delete", "restore", "revoke"):
            for suffix in ("ready", "done"): (self.output / (stage + "-" + suffix)).unlink(missing_ok=True)
        command = ["docker", "run", "--rm", "--name", self.project + "-insights-browser", "--network", "container:" + self.project + "-console-1", "--ipc=host", "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/cache/home", "-e", "PLAYWRIGHT_BROWSERS_PATH=/ms-playwright", "-e", "PSIM_INSIGHTS_PRESET=" + self.preset, "-v", str(ROOT) + ":/repo", "-v", str(ROOT / ".cache/web") + ":/cache", "-w", "/repo/web", PLAYWRIGHT, "node", "apps/console/node_modules/@playwright/test/cli.js", "test", "-c", "apps/console/playwright.insights.config.ts"]
        with (self.output / "playwright.log").open("w") as log:
            browser = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                where = " WHERE tenant='" + self.tenant + "' AND seq=" + original["auditSeq"]
                for stage in ("modify", "delete", "restore", "revoke"):
                    self.wait(lambda: (self.output / (stage + "-ready")).exists() or browser.poll() is not None, 180)
                    assert browser.poll() is None, (self.output / "playwright.log").read_text()
                    if stage == "revoke":
                        token = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
                        data = json.loads((self.output / "fixture.json").read_text())
                        role = self.keycloak("GET", "/admin/realms/psim/clients/" + self.roles_client + "/roles/auditor", token)
                        self.keycloak("DELETE", "/admin/realms/psim/users/" + data["scopedUserId"] + "/role-mappings/clients/" + self.roles_client, token, [role])
                        (self.output / (stage + "-done")).write_text("ready\n")
                        continue
                    self.compose("stop", "-t", "35", "audit-service")
                    if stage == "modify":
                        self.sql("ALTER TABLE audit.entries DISABLE TRIGGER immutable_entries")
                        self.sql("UPDATE audit.entries SET body=jsonb_set(body,'{entry,action}','\"modified\"')" + where)
                    elif stage == "delete": self.sql("DELETE FROM audit.entries" + where)
                    else:
                        source = original["sourceEventId"]
                        body = json.dumps(original).replace("'", "''")
                        self.sql(f"INSERT INTO audit.entries VALUES('{self.tenant}',{original['auditSeq']},'{source}','{body}'::jsonb)")
                        self.sql("ALTER TABLE audit.entries ENABLE TRIGGER immutable_entries")
                    self.compose("up", "-d", "audit-service")
                    self.wait(lambda: self.request("GET", "/v1/audit/records", role="auditor", service="api-gateway"), 90)
                    (self.output / (stage + "-done")).write_text("ready\n")
                assert browser.wait(timeout=180) == 0, (self.output / "playwright.log").read_text()
            finally:
                if browser.poll() is None:
                    subprocess.run(["docker", "rm", "-f", self.project + "-insights-browser"], capture_output=True)
                    browser.wait(timeout=20)
        raw = self.output / "psim-audit.jsonl"
        frozen = self.output / ("external-copy-" + self.fixtures); shutil.copytree(self.archive, frozen)
        footer = json.loads(raw.read_bytes().splitlines()[-1])
        head = int(footer["headSeq"])
        assert (frozen / self.tenant / (str(head) + ".json")).is_file()
        for checkpoint in (frozen / self.tenant).glob("*.json"):
            if int(checkpoint.stem) > head: checkpoint.unlink()
        result = self.env_command(f"{self.tool} verify /repo/{raw.relative_to(ROOT)} /repo/{(self.output / 'trusted-keys.json').relative_to(ROOT)} /repo/{frozen.relative_to(ROOT)}", network=False)
        assert json.loads(result.strip().splitlines()[-1])["intact"]
        with (self.output / "psim-incidents.csv").open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        assert len(rows) == 2 and rows[0]["row_kind"] == "total"
        for key, expected in {"incidents_created": 10, "incidents_resolved": 10, "mtta_seconds": 60, "mttr_seconds": 120, "false_alarm_rate": 0.2, "ack_sla_compliance": 0.5, "resolve_sla_compliance": 1, "escalated": 1, "ack_sla_evaluated": 10, "resolve_sla_evaluated": 10}.items():
            assert float(rows[0][key]) == expected, (key, rows[0][key])
        self.mark("independent offline verification of the browser's downloaded audit chain and external checkpoint")


def main(project, preset):
    check = Check(project, preset)
    try:
        check.prepare(); check.basics()
        first = check.setup_browser(); check.browser(first)
        browser = json.loads((check.output / "browser.json").read_text())
        browser["audit"] = json.loads((check.output / "audit-browser.json").read_text())
        browser["privacy"] = json.loads((check.output / "privacy-browser.json").read_text())
        assert browser["audit"]["passed"] and browser["privacy"]["passed"]
        deployed = (check.output / "console/index.html").read_bytes()
        assert deployed == (ROOT / "web/apps/console/dist/index.html").read_bytes(), "console build changed during acceptance"
        check.report |= {"passed": True, "browser": browser, "sourceHash": hashlib.sha256(deployed).hexdigest()}
    finally: check.cleanup()
    print(json.dumps(check.report, indent=2))


if __name__ == "__main__": main(sys.argv[1], sys.argv[2])
