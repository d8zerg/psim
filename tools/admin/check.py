"""Run administrative browser acceptance against native services and public HTTPS APIs."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import shutil
import sys

ROOT = Path(__file__).resolve().parents[2]
api = runpy.run_path(str(ROOT / "tools/api-gateway/check.py"))
PLAYWRIGHT = "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27"


class Check(api["Check"]):
    def principal_cases(self):
        return super().principal_cases() | {"reader": ("operator", ["*"], api["helpers"]["TENANT"])}

    def cleanup(self):
        self.compose_cmd += ["--profile", "rules-test"]
        super().cleanup()


def main(project, preset):
    check = Check(project, preset)
    output = ROOT / "build/admin" / preset
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False}
    try:
        config_path = check.output / "api.json"
        config = json.loads(config_path.read_text())
        config["settings"]["allowed_origins"] = ["http://localhost:4173"]
        config_path.write_text(json.dumps(config))
        check.prepare()
        response_path = output / "response.json"
        response_path.write_text(json.dumps({"service": {"instance": "administration-response"}, "runtime": {"shards": 1}, "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001}, "settings": {"allow_plaintext": True, "listen_port": 8443, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "introspection_secret_file": "/repo/.cache/event-flow/introspection.secret", "issuer": "http://keycloak:8080/realms/psim"}}))
        assets = output / "console"
        shutil.rmtree(assets, ignore_errors=True)
        shutil.copytree(ROOT / "web/apps/console/dist", assets)
        extra = output / "compose.json"
        base = {"image": "debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5", "user": f"{os.getuid()}:{os.getgid()}", "read_only": True, "cap_drop": ["ALL"], "security_opt": ["no-new-privileges:true"], "volumes": [str(ROOT) + ":/repo:ro"], "tmpfs": ["/tmp"], "command": [f"/repo/build/debian13/{preset}/services/response-engine/psim-response-engine", "--config", "/repo/" + str(response_path.relative_to(ROOT))]}
        replay_secret = output / "replay.secret"
        replay_secret.unlink(missing_ok=True)
        replay_secret.write_text("psim-dev-clickhouse")
        replay_secret.chmod(0o400)
        rule_path = output / "rules.json"
        rule_config = json.loads(response_path.read_text())
        rule_config["settings"].update({"role": "management", "tls_ca_file": "/repo/.cache/event-flow/ca.pem", "tls_certificate_file": "/repo/.cache/event-flow/rule-server.pem", "tls_private_key_file": "/repo/.cache/event-flow/rule-server.key", "taxonomy_file": "/repo/contracts/taxonomy/v1/taxonomy.yaml", "clickhouse_password_file": "/repo/" + str(replay_secret.relative_to(ROOT))})
        rule_path.write_text(json.dumps(rule_config))
        rule_service = base | {"command": [f"/repo/build/debian13/{preset}/services/correlation-engine/psim-correlation-engine", "--config", "/repo/" + str(rule_path.relative_to(ROOT))]}
        extra.write_text(json.dumps({"services": {"response-engine": base, "rule-management": rule_service, "incident-service": base | {"command": [f"/repo/build/debian13/{preset}/services/incident-service/psim-incident-service", "--config", "/repo/" + str(response_path.relative_to(ROOT))]}, "console": {"volumes": [str(assets) + ":/repo/web/apps/console/dist:ro"], "environment": {"PSIM_API_UPSTREAM": "https://api-gateway:8443", "NODE_EXTRA_CA_CERTS": "/repo/.cache/event-flow/ca.pem"}}}}))
        check.compose_cmd += ["-f", str(extra), "--profile", "event-flow"]
        (ROOT / ".cache/event-flow" / (project + ".config.js")).write_text('window.psimConfig={"issuer":"http://keycloak:8080/realms/psim"};\n')
        check.compose("up", "-d", "--force-recreate", "incident-service", "rule-management", "response-engine", "console")
        check.wait(lambda: check.request("GET", "/v1/incident-types"), 90)
        check.wait(lambda: check.request("GET", "/v1/rules"), 90)
        check.wait(lambda: check.request("GET", "/v1/plans"), 90)
        admin = check.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        users = "/admin/realms/psim/users"
        username = "admin-browser-" + check.fixtures
        password = "psim-dev-Passw0rd"
        check.keycloak("POST", users, admin, {"username": username, "enabled": True, "emailVerified": True, "firstName": "Admin", "lastName": "Browser", "email": username + "@example.test", "attributes": {"psim_tenant": [api["helpers"]["TENANT"]], "psim_scopes": ["*"]}, "credentials": [{"type": "password", "value": password, "temporary": False}]})
        user = check.keycloak("GET", users + "?username=" + username, admin)[0]["id"]
        client = check.keycloak("GET", "/admin/realms/psim/clients?clientId=psim", admin)[0]["id"]
        role = check.keycloak("GET", "/admin/realms/psim/clients/" + client + "/roles/admin", admin)
        check.keycloak("POST", users + "/" + user + "/role-mappings/clients/" + client, admin, [role])
        scope_site = check.location("SITE", identity=check.site_id)
        foreign_site = check.location("SITE", identity=check.foreign_site)
        foreign_building = check.location("BUILDING", foreign_site)
        foreign_floor = check.location("FLOOR", foreign_building)
        scoped_username = "scoped-" + username
        check.keycloak("POST", users, admin, {"username": scoped_username, "enabled": True, "emailVerified": True, "firstName": "Scoped", "lastName": "Browser", "email": scoped_username + "@example.test", "attributes": {"psim_tenant": [api["helpers"]["TENANT"]], "psim_scopes": [scope_site["locationId"]]}, "credentials": [{"type": "password", "value": password, "temporary": False}]})
        scoped_user = check.keycloak("GET", users + "?username=" + scoped_username, admin)[0]["id"]
        check.keycloak("POST", users + "/" + scoped_user + "/role-mappings/clients/" + client, admin, [role])
        connector = api["uid"]()
        certificate_dir = output / ("certificate-" + check.fixtures)
        certificate_dir.mkdir(exist_ok=True)
        ext = certificate_dir / "extensions"
        ext.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\nsubjectAltName=URI:psim://connector/" + connector + "\n")
        private, csr, pem = (certificate_dir / name for name in ("private.key", "request.pem", "certificate.pem"))
        check.run(["openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-noenc", "-subj", "/CN=psim-admin-browser", "-keyout", str(private), "-out", str(csr)])
        private.chmod(0o400)
        check.run(["openssl", "x509", "-req", "-days", "1", "-in", str(csr), "-CA", str(ROOT / ".cache/event-flow/ca.pem"), "-CAkey", str(ROOT / ".cache/event-flow/ca.key"), "-CAcreateserial", "-extfile", str(ext), "-out", str(pem)])
        renewed_key, renewed_csr, renewed_pem = (certificate_dir / name for name in ("renewed.key", "renewed-request.pem", "renewed.pem"))
        check.run(["openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-noenc", "-subj", "/CN=psim-admin-renewed", "-keyout", str(renewed_key), "-out", str(renewed_csr)])
        renewed_key.chmod(0o400)
        check.run(["openssl", "x509", "-req", "-days", "1", "-in", str(renewed_csr), "-CA", str(ROOT / ".cache/event-flow/ca.pem"), "-CAkey", str(ROOT / ".cache/event-flow/ca.key"), "-CAcreateserial", "-extfile", str(ext), "-out", str(renewed_pem)])
        (output / "plan.png").write_bytes(api["png"](100, 100))
        (output / "fixture.json").write_text(json.dumps({"username": username, "password": password, "userId": user, "scopedUsername": scoped_username, "foreignFloorId": foreign_floor["locationId"], "renewedCertificatePem": renewed_pem.read_text(), "connectorId": connector, "connectorType": "admin-browser-" + connector, "certificatePem": pem.read_text(), "preset": preset}))
        check.run(["docker", "run", "--rm", "--name", project + "-admin-browser", "--network", "container:" + project + "-console-1", "--ipc=host", "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/cache/home", "-e", "COREPACK_HOME=/cache/corepack", "-e", "PLAYWRIGHT_BROWSERS_PATH=/ms-playwright", "-e", "PSIM_ADMIN_PRESET=" + preset, "-v", str(ROOT) + ":/repo", "-v", str(ROOT / ".cache/web") + ":/cache", "-w", "/repo/web", PLAYWRIGHT, "node", "apps/console/node_modules/@playwright/test/cli.js", "test", "-c", "apps/console/playwright.admin.config.ts"])
        browser = json.loads((output / "browser.json").read_text())
        assert browser["importRows"] == 50000 and browser["importMs"] <= 300000
        assert len(browser["floorIds"]) == 3 and len(browser["zoneIds"]) == 20
        site = browser["siteId"]
        assert check.request("GET", "/v1/locations/" + browser["zoneId"])["location"]["siteId"] == site
        assert check.request("GET", "/v1/devices/" + browser["deviceId"])["device"]["connectorId"] == connector
        assert check.request("GET", "/v1/mappings/admin-browser-" + connector)["mappingRuleSet"]["rules"][0]["type"] == "intrusion.zone.alarm"
        assert check.request("GET", "/v1/plans/" + browser["planId"])["plan"]["state"] == "PLAN_STATE_PUBLISHED"
        assert check.request("GET", "/v1/rules/" + browser["ruleId"])["rule"]["state"] == "RULE_STATE_ACTIVE"
        check.wait(lambda: check.sql("SELECT count(*) FROM catalog.psim_outbox WHERE published_at IS NULL") == "0", 30)
        check.compose("up", "-d", "--force-recreate", "normalizer", "correlation-engine")
        source_tls = output / ("source-" + check.fixtures)
        source_tls.mkdir()
        for source, target in ((renewed_pem, "client.pem"), (renewed_key, "client.key"), (ROOT / ".cache/event-flow/ca.pem", "ca.pem")):
            shutil.copyfile(source, source_tls / target)
        (source_tls / "client.key").chmod(0o400)
        emitted = check.env_command(f"build/debian13/{preset}/tools/event-flow/psim-flow-simulator --tls /repo/{source_tls.relative_to(ROOT)} --source browser-alarm --code ALARM --count 1 --interval-ms 0")
        event = next(json.loads(line)["event_id"] for line in emitted.splitlines() if line.startswith('{') and '"event_id"' in line)
        incident = check.wait(lambda: next((item for item in check.request("GET", "/v1/incidents", role="reader")["incidents"] if item["incidentTypeId"] == browser["incidentTypeId"]), None), 90)
        run = check.wait(lambda: check.request("GET", "/v1/incidents/" + incident["incidentId"] + "/run", role="reader").get("run"), 60)
        assert run["planId"] == browser["planId"]
        browser["checks"].append("new connector event through mTLS, normalization mapping and active rule into incident and response run")
        report["eventId"] = event
        report["incidentId"] = incident["incidentId"]
        report |= {"passed": True, "checks": browser["checks"], "sourceHash": hashlib.sha256((ROOT / "web/apps/console/dist/index.html").read_bytes()).hexdigest()}
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        if os.environ.get("PSIM_ADMIN_KEEP_ENV") != "1":
            check.cleanup()
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
