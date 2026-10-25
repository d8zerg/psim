"""Run the operator browser against native services and real infrastructure."""
import json
import os
from pathlib import Path
import runpy
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
notification = runpy.run_path(str(ROOT / "tools/notifications/check.py"))
api = runpy.run_path(str(ROOT / "tools/api-gateway/check.py"))
PLAYWRIGHT = "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27"


def main(project, preset):
    check = notification["Check"](project, preset, 1)
    gateway = None
    output = ROOT / "build/operator" / preset
    output.mkdir(parents=True, exist_ok=True)
    report = {"passed": False}
    try:
        check.prepare()
        gateway = api["Check"](project, preset)
        settings_path = gateway.output / "api.json"
        config = json.loads(settings_path.read_text())
        config["settings"]["allowed_origins"] = ["http://localhost:4173"]
        settings_path.write_text(json.dumps(config))
        gateway.prepare()
        # The public catalog uses the same tenant as the command fixture.
        site = gateway.location("SITE", identity=check.site_id)
        building = gateway.location("BUILDING", site)
        floor = gateway.location("FLOOR", building)
        zone = gateway.location("ZONE", floor, identity=check.zone)
        catalog_type = gateway.request("POST", "/v1/device-types", check.type_body_command | {"code": "operator-" + check.fixtures, "name": "Operator device type", "sourceTemplates": [{"kind": "alarm", "name": "Alarm"}]})["deviceType"]
        gateway.request("PUT", "/v1/device-types/" + check.device_type, catalog_type, version=catalog_type["version"])
        gateway.request("PUT", "/v1/locations/" + floor["locationId"] + "/floor-plan?width=100&height=100", api["png"](100, 100), version=floor["version"], content_type="image/png")
        device = check.device_body | {"name": "Operator device", "externalId": "operator-" + check.fixtures, "position": {"x": 25, "y": 25}, "sources": [{"externalRef": "operator-alarm-" + check.fixtures, "kind": "alarm", "name": "Alarm"}]}
        saved_device = gateway.request("POST", "/v1/devices", device)["device"]
        gateway.request("PUT", "/v1/devices/" + check.device, saved_device, version=saved_device["version"])
        check.wait(lambda: check.sql("SELECT count(*) FROM command.resources WHERE tenant='" + check.tenant + "' AND id IN ('" + check.device + "','" + check.device_type + "') AND version=2") == "2", 60)
        # Publish a manual checklist and decision before creating the browser's incident.
        plan = check.publish(check.plan([
            {"stepId": "check", "title": "Verify safety", "mandatory": True, "nextStepId": "decision", "checklist": {"items": [{"itemId": "safe", "text": "Safety verified", "required": True}]}},
            {"stepId": "decision", "title": "Choose outcome", "mandatory": True, "decision": {"question": "Observed condition", "options": [{"optionId": "confirmed", "label": "Confirmed", "nextStepId": "operate"}, {"optionId": "false", "label": "False alarm", "nextStepId": "finish"}]}},
            {"stepId": "operate", "title": "Operate device", "mandatory": True, "nextStepId": "finish", "command": {"deviceId": check.device, "capability": "arm"}},
            {"stepId": "finish", "title": "Record outcome", "mandatory": True, "instruction": {"text": "Outcome recorded"}}
        ]))
        main_policy = check.rq("POST", "/v1/escalation-policies", {"policyId": notification["uid"](), "name": "Operator response SLA", "sla": [{"priority": "PRIORITY_P2", "acknowledgeWithin": "3600s", "resolveWithin": "7200s"}], "levels": [{"level": 1, "delay": "0s", "recipients": [{"userId": check.subjects["operator"]}], "channels": ["NOTIFICATION_CHANNEL_IN_APP"]}]}, role="admin")["policy"]
        incident_type = next(item for item in check.request("GET", "/v1/incident-types", role="admin")["incidentTypes"] if item["incidentTypeId"] == check.type_id)
        check.request("PUT", "/v1/incident-types/" + check.type_id, incident_type | {"escalationPolicyId": main_policy["policyId"]}, role="admin", version=incident_type["version"])
        check.wait_response_consumers()
        incident = check.new_incident()
        check.at(incident, "check")
        check.incident_id = incident
        original_due = check.wait(lambda: check.incident(incident).get("operational", {}).get("resolveDue"), 30)
        # A dedicated normal user exercises actual browser PKCE and current directory permissions.
        check.refresh_admin()
        username = "operator-browser-" + check.fixtures
        password = "psim-dev-Passw0rd"
        users = "/admin/realms/psim/users"
        check.keycloak("POST", users, check.admin, {"username": username, "enabled": True, "emailVerified": True, "requiredActions": ["CONFIGURE_TOTP"],
            "firstName": "Operator", "lastName": "Browser", "email": username + "@example.test",
            "attributes": {"psim_tenant": [check.tenant], "psim_scopes": [check.site_id]},
            "credentials": [{"type": "password", "value": password, "temporary": False}]})
        user = check.keycloak("GET", users + "?username=" + username, check.admin)[0]["id"]
        roles = [check.keycloak("GET", "/admin/realms/psim/clients/" + check.roles_client + "/roles/" + name, check.admin) for name in ("operator", "supervisor")]
        check.keycloak("POST", users + "/" + user + "/role-mappings/clients/" + check.roles_client, check.admin, roles)
        recipient_username = "operator-recipient-" + check.fixtures
        check.keycloak("POST", users, check.admin, {"username": recipient_username, "enabled": True, "emailVerified": True, "requiredActions": ["CONFIGURE_TOTP"], "firstName": "Recipient", "lastName": "Browser", "email": recipient_username + "@example.test", "attributes": {"psim_tenant": [check.tenant], "psim_scopes": [check.site_id]}, "credentials": [{"type": "password", "value": password, "temporary": False}]})
        recipient = check.keycloak("GET", users + "?username=" + recipient_username, check.admin)[0]["id"]
        check.keycloak("POST", users + "/" + recipient + "/role-mappings/clients/" + check.roles_client, check.admin, roles)
        directory = {key: value for key, value in json.loads(check.config.read_text())["settings"].items() if key.startswith("directory_")}
        response_config = json.loads(check.response_config.read_text())
        response_config["settings"].update(directory)
        check.response_config.write_text(json.dumps(response_config))
        # A real acknowledgement deadline creates SLA escalation while the browser is connected.
        policy = check.rq("POST", "/v1/escalation-policies", {"policyId": notification["uid"](), "name": "Operator SLA", "sla": [{"priority": "PRIORITY_P2", "acknowledgeWithin": "8s", "resolveWithin": "3600s"}], "levels": [{"level": 1, "delay": "0s", "recipients": [{"userId": user}], "channels": ["NOTIFICATION_CHANNEL_IN_APP"]}]}, role="admin")["policy"]
        # A second incident type isolates the SLA timer from setup and the main response run.
        type_id = notification["uid"]()
        check.request("POST", "/v1/incident-types", {"incidentTypeId": type_id, "code": "sla-" + check.fixtures, "name": "Operator SLA incident", "defaultPriority": "PRIORITY_P2", "groupingWindow": "60s", "escalationPolicyId": policy["policyId"]}, role="admin", expected=201)
        manifest = {"username": username, "password": password, "incidentId": incident, "deviceId": check.device, "floorId": floor["locationId"], "siteId": check.site_id, "recipientId": recipient, "recipientUsername": recipient_username, "planId": plan["planId"], "preset": preset, "originalResolveDue": original_due, "planVersion": plan["version"]}
        (output / "fixture.json").write_text(json.dumps(manifest))
        config_file = ROOT / ".cache/event-flow" / (project + ".config.js")
        config_file.write_text('window.psimConfig={"issuer":"http://keycloak:8080/realms/psim"};\n')
        assets = output / "console"
        shutil.rmtree(assets, ignore_errors=True)
        shutil.copytree(ROOT / "web/apps/console/dist", assets)
        override = output / "console.json"
        override.write_text(json.dumps({"services": {"console": {"volumes": [str(assets) + ":/repo/web/apps/console/dist:ro"], "environment": {"PSIM_API_UPSTREAM": "https://api-gateway:8443", "NODE_EXTRA_CA_CERTS": "/repo/.cache/event-flow/ca.pem"}}}}))
        check.run(gateway.compose_cmd + ["-f", str(override), "--profile", "event-flow", "up", "-d", "--force-recreate", "console"])
        command = ["docker", "run", "--rm", "--name", project + "-operator-browser", "--network", "container:" + project + "-console-1", "--ipc=host", "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/cache/home", "-e", "COREPACK_HOME=/cache/corepack", "-e", "PLAYWRIGHT_BROWSERS_PATH=/ms-playwright", "-e", "PSIM_OPERATOR_PRESET=" + preset, "-v", str(ROOT) + ":/repo", "-v", str(ROOT / ".cache/web") + ":/cache", "-w", "/repo/web", PLAYWRIGHT, "node", "apps/console/node_modules/@playwright/test/cli.js", "test", "-c", "apps/console/playwright.operator.config.ts"]
        ready = output / "browser-ready"
        ready.unlink(missing_ok=True)
        restart = output / "response-restart-ready"
        restored = output / "response-restart-done"
        restart.unlink(missing_ok=True)
        restored.unlink(missing_ok=True)
        with (output / "playwright.log").open("w") as log:
            browser = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
            try:
                check.wait(lambda: ready.exists() or browser.poll() is not None, 90)
                if browser.poll() is not None:
                    raise RuntimeError((output / "playwright.log").read_text())
                check.emit(count=1, group="operator-sla-" + check.fixtures, type=type_id)
                check.wait(lambda: restart.exists() or browser.poll() is not None, 90)
                assert browser.poll() is None, (output / "playwright.log").read_text()
                check.compose("stop", "-t", "35", "response-engine", "response-2")
                check.compose("up", "-d", "response-engine", "response-2")
                pinned = check.at(incident, "decision")
                assert pinned["planVersion"] == plan["version"]
                replacement = check.publish(check.plan([{"stepId": "new", "title": "New definition", "instruction": {"text": "New version"}}], planId=plan["planId"]))
                assert replacement["version"] > plan["version"]
                assert check.current_run(incident)["planVersion"] == plan["version"]
                restored.write_text("ready\n")
                result = browser.wait(timeout=180)
                assert result == 0, (output / "playwright.log").read_text()
            finally:
                if browser.poll() is None:
                    subprocess.run(["docker", "rm", "-f", project + "-operator-browser"], capture_output=True)
                    browser.wait(timeout=20)
        assert check.request("GET", "/v1/incidents/" + incident)["incident"]["state"] == "INCIDENT_STATE_CLOSED"
        completed = check.current_run(incident)
        assert completed["state"] == "RUN_STATE_COMPLETED"
        linked = next(step for step in completed["steps"] if step["stepId"] == "operate")["result"]["commandIds"]
        assert len(linked) == 1
        assert len([item for item in check.executions() if item["id"] == linked[0]]) == 1
        report |= {"passed": True, "checks": ["real PKCE", "incident acceptance and response checklist/decision", "previewed command from the active response step and device execution", "SLA escalation and in-app notification", "private floor plan", "responsibility transfer request", "original SLA preservation", "critical command approved by a different supervisor", "device rejection and timeout reach final states in the UI", "responsibility transfer accepted through the UI", "mandatory step blocks resolution", "response restart and immutable plan version", "completed response and resolved then closed incident"]}
    finally:
        (output / "report.json").write_text(json.dumps(report, indent=2) + "\n")
        if os.environ.get("PSIM_OPERATOR_KEEP_ENV") != "1":
            if gateway:
                gateway.cleanup()
            check.cleanup()
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
