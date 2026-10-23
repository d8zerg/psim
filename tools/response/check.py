"""Verify response execution, process recovery, timers and isolated replay using real services."""
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

ROOT = Path(__file__).resolve().parents[2]
helpers = runpy.run_path(str(ROOT / "tools/commands/check.py"))
CommandCheck, uid = helpers["Check"], helpers["uid"]


class Check(CommandCheck):
    def __init__(self, project, preset, count):
        super().__init__(project, preset, count)
        self.log.close()
        self.output = ROOT / "build/response" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.response_config = self.output / "service.json"
        services = json.loads(self.override.read_text())["services"]
        base = services["command-service"] | {"command": [f"/repo/build/debian13/{preset}/services/response-engine/psim-response-engine", "--config", "/repo/" + str(self.response_config.relative_to(ROOT))]}
        services.update({"response-engine": base, "response-2": base | {"environment": base["environment"] | {"PSIM__SERVICE__INSTANCE": "response-2"}}})
        self.handler_directory = self.output / ("handler-" + self.fixtures)
        self.handler_directory.mkdir()
        services["response-handler"] = base | {"image": self.toolchain, "read_only": True, "volumes": [str(ROOT) + ":/repo:ro", str(self.handler_directory) + ":/fixture"], "command": ["python3", "-I", "/repo/tools/response/handler.py", "/fixture"], "ports": []}
        old_override = str(self.override)
        self.override = self.output / "compose.json"
        self.override.write_text(json.dumps({"services": services}, indent=2))
        self.compose_cmd[self.compose_cmd.index(old_override)] = str(self.override)
        self.report = {"passed": False, "checks": []}

    def rq(self, method, path, body=None, **options):
        return self.request(method, path, body, service=options.pop("service", "response-engine"), **options)

    def plan(self, steps, **options):
        return {"planId": uid(), "name": "Response fixture", "startStepId": steps[0]["stepId"], "steps": steps,
                "bindings": [{"incidentTypeId": self.type_id, "siteIds": [self.site_id], "zoneIds": [self.zone]}]} | options

    def publish(self, plan):
        saved = self.rq("POST", "/v1/plans", plan, role="admin")["plan"]
        return self.rq("POST", "/v1/plans/" + saved["planId"] + ":publish", {"version": saved["version"]}, role="admin")["plan"]

    def archive(self, plan):
        return self.rq("POST", "/v1/plans/" + plan["planId"] + ":archive", {}, role="admin")

    def new_incident(self):
        group = "response-" + uid()
        self.emit(count=1, group=group)
        return self.wait(lambda: next((item["incidentId"] for item in self.request("GET", "/v1/incidents")["incidents"] if item["groupingKey"] == group), None), 60)

    def current_run(self, incident, **options):
        return self.rq("GET", "/v1/incidents/" + incident + "/run", **options)["run"]

    def at(self, incident, step=None, state=None):
        def ready():
            value = self.current_run(incident)
            return value if (value.get("activeStepIds", []) == [step] if step else value["state"] == state) else None
        return self.wait(ready, 60)

    def action(self, run, action="complete", body=None, **options):
        result = self.rq("POST", "/v1/runs/" + run["runId"] + "/steps/" + run["activeStepIds"][0] + ":" + action,
                       body or {}, version=options.pop("version", run["version"]), **options)
        return result.get("run", result)

    def calls(self):
        path = self.handler_directory / "calls.jsonl"
        return [json.loads(line) for line in path.read_text().splitlines()] if path.exists() else []

    def prepare(self):
        super().prepare()
        clients = "/admin/realms/psim/clients/" + self.roles_client
        supervisor = self.keycloak("GET", clients + "/roles/supervisor", self.admin)
        self.keycloak("POST", "/admin/realms/psim/users/" + self.subjects["bob"] + "/role-mappings/clients/" + self.roles_client, self.admin, [supervisor])
        self.tokens["bob"]["expires"] = 0
        self.response_config.write_text(json.dumps({"service": {"instance": "response-1"}, "runtime": {"shards": 1},
          "telemetry": {"otlp_endpoint": "otel-collector:4317", "otlp_tls": False, "sampling_ratio": 0.001},
          "settings": {"allow_plaintext": True, "pg_password_file": "/repo/.cache/event-flow/postgres.secret", "introspection_secret_file": "/repo/.cache/event-flow/introspection.secret", "listen_port": 8443,
          "issuer": "http://keycloak:8080/realms/psim", "handler_ca_file": "/repo/.cache/event-flow/ca.pem",
          "handler_urls": ["https://response-handler:8443/" + name for name in ("ok", "fail", "hold")]}}, indent=2))
        self.compose("up", "-d", "--force-recreate", "response-engine", "response-2", "response-handler")
        self.response_ready()
        self.wait(lambda: self.sql("SELECT count(*) FROM response.resources WHERE tenant='" + self.tenant + "' AND id IN ('" + self.device + "','" + self.device_type + "')") == "2", 90)
        self.mark("two response processes, monotonic catalog projections and bounded HTTPS handler")

    def kill_response(self, *services):
        self.compose("kill", "-s", "SIGKILL", *services)
        for service in services:
            identity = self.compose("ps", "-aq", service).strip()
            self.wait(lambda: self.run(["docker", "inspect", "-f", "{{.State.Running}}", identity]).strip() == "false")

    def response_ready(self):
        for service in ("response-engine", "response-2"):
            self.wait(lambda: self.rq("GET", "/v1/plans", role="admin", service=service), 90)

    def coverage_checkpoint(self):
        if self.preset != "coverage":
            return
        # Normal exit flushes coverage before a later SIGKILL discards the current process counters.
        self.compose("stop", "-t", "35", "response-engine", "response-2")
        self.compose("up", "-d", "response-engine", "response-2")
        self.response_ready()

    def missing_plan(self):
        self.type_body["responseRequired"] = True
        self.request("PUT", "/v1/incident-types/" + self.type_id, self.type_body, role="admin", version=1)
        incident = self.new_incident()
        self.wait(lambda: any(entry.get("details", {}).get("warning") == "response_plan_unavailable"
            for entry in self.request("GET", "/v1/incidents/" + incident + "/timeline")["entries"]))
        self.rq("GET", "/v1/incidents/" + incident + "/run", expected=404)
        self.mark("required response plan missing: one timeline warning and no fabricated run")

    def definitions(self):
        self.rq("GET", "/v1/plans", role=None, expected=401)
        self.rq("GET", "/v1/plans?unknown=x", role="admin", expected=400)
        invalid = self.plan([{"stepId": "a", "title": "A", "nextStepId": "missing", "instruction": {}}])
        assert not self.rq("POST", "/v1/plans:validate", invalid, role="admin")["valid"]
        self.rq("POST", "/v1/plans", invalid, role="operator", expected=403)
        self.rq("POST", "/v1/plans", invalid, role="admin", expected=400)
        simple = self.plan([{"stepId": "a", "title": "A", "instruction": {}}])
        draft = self.rq("POST", "/v1/plans", simple, role="admin")["plan"]
        self.rq("POST", "/v1/plans/" + draft["planId"] + ":publish", {"version": 999}, role="admin", expected=404)
        pub = self.rq("POST", "/v1/plans/" + draft["planId"] + ":publish", {"version": draft["version"]}, role="admin")["plan"]
        conflicting = self.rq("POST", "/v1/plans", simple | {"planId": uid()}, role="admin")["plan"]
        self.rq("POST", "/v1/plans/" + conflicting["planId"] + ":publish", {"version": conflicting["version"]}, role="admin", expected=400)
        incident = self.new_incident()
        run = self.wait(lambda: self.current_run(incident))
        self.rq("GET", "/v1/incidents/" + incident + "/run", role="other", expected=403)
        self.rq("GET", "/v1/incidents/" + incident + "/run", role="tenant", expected=404)
        self.action(run, "skip", {"reason": "not needed"})
        self.archive(pub)
        self.mark("plan validation, publication, ambiguity rejection, scoped run reads and optional skip")

    def lifecycle(self):
        steps = [
            {"stepId": "read", "title": "Read", "mandatory": True, "nextStepId": "decision", "instruction": {"text": "Read instructions"}},
            {"stepId": "decision", "title": "Decide", "decision": {"options": [{"optionId": "real", "label": "Real", "nextStepId": "check"}, {"optionId": "false", "label": "False", "nextStepId": "end"}]}},
            {"stepId": "check", "title": "Check", "mandatory": True, "nextStepId": "wait", "checklist": {"items": [{"itemId": "safety", "text": "Checked", "required": True}]}},
            {"stepId": "wait", "title": "Wait", "nextStepId": "command", "wait": {"duration": "3s"}},
            {"stepId": "command", "title": "Unlock", "mandatory": True, "nextStepId": "notify", "command": {"deviceId": self.device, "capability": "unlock", "automatic": True}},
            {"stepId": "notify", "title": "Notify", "nextStepId": "auto", "notify": {"recipients": [{"role": "supervisor"}], "channels": ["NOTIFICATION_CHANNEL_EMAIL"], "template": "Response"}},
            {"stepId": "auto", "title": "Handler", "nextStepId": "escalate", "automated": {"webhookUrl": "https://response-handler:8443/ok", "timeout": "5s"}},
            {"stepId": "escalate", "title": "Escalate", "nextStepId": "end", "escalate": {"level": 2}},
            {"stepId": "end", "title": "End", "instruction": {}}
        ]
        plan = self.publish(self.plan(steps))
        self.replay_plan = plan
        incident = self.new_incident()
        run = self.wait(lambda: self.current_run(incident))
        self.action(run, "skip", {"reason": "skip mandatory"}, expected=409)
        key = uid()
        advanced = self.action(run, key=key)
        assert self.action(run, key=key) == advanced
        self.action(run, version=999, expected=412)
        self.action(advanced, body={"result": {"decisionOptionId": "invalid"}}, expected=409)
        run = self.action(advanced, body={"result": {"decisionOptionId": "real"}})
        self.action(run, body={"result": {"checkedItemIds": []}}, expected=409)
        run = self.action(run, body={"result": {"checkedItemIds": ["safety"]}})
        assert run["activeStepIds"] == ["wait"]
        self.kill_response("response-engine", "response-2")
        revised = json.loads(json.dumps(plan))
        revised["steps"][0]["instruction"]["text"] = "New version"
        self.compose("up", "-d", "response-engine", "response-2")
        self.response_ready()
        newer = self.publish(revised)
        assert newer["version"] == plan["version"] + 1
        run = self.at(incident, "command")
        pinned = self.rq("GET", "/v1/incidents/" + incident + "/run")["plan"]
        assert pinned["version"] == plan["version"] and pinned["steps"][0]["instruction"]["text"] == "Read instructions"
        command = self.wait(lambda: next((item for item in self.cq("GET", "?incidentId=" + incident)["commands"] if item.get("runId") == run["runId"]), None), 60)
        assert command["state"] == "COMMAND_STATE_AWAITING_APPROVAL"
        assert not any(item["id"] == command["commandId"] for item in self.executions())
        self.action(run, expected=409)
        self.cq("POST", "/" + command["commandId"] + ":approve", {}, role="chief", version=command["version"])
        run = self.at(incident, "end")
        current = self.action(run)
        assert current["state"] == "RUN_STATE_COMPLETED"
        self.wait(lambda: self.incident(incident).get("escalationLevel") == 2)
        self.wait(lambda: not self.incident(incident).get("blockingSteps"))
        assert len([call for call in self.calls() if call["incidentId"] == incident]) == 1
        assert int(self.sql("SELECT count(*) FROM response.psim_outbox WHERE topic='psim.response.events.v1'")) > 5
        assert len([item for item in self.executions() if item["id"] == command["commandId"]]) == 1
        branch = self.new_incident()
        run = self.action(self.wait(lambda: self.current_run(branch)))
        run = self.action(run, body={"result": {"decisionOptionId": "false"}})
        assert run["activeStepIds"] == ["end"] and not run.get("blockingStepIds")
        self.action(run)
        self.archive(newer)
        self.mark("checklist, branching, mandatory steps, pinned version, SIGKILL recovery, critical command approval, notification intent and one HTTPS effect")

    def manual_command(self):
        plan = self.publish(self.plan([
            {"stepId": "cmd", "title": "Arm zone", "mandatory": True, "nextStepId": "end", "command": {"targetExpression": "zone.devices", "capability": "arm"}},
            {"stepId": "end", "title": "Done", "instruction": {}}]))
        incident = self.new_incident()
        run = self.wait(lambda: self.current_run(incident))
        assert not next(step for step in run["steps"] if step["stepId"] == "cmd")["result"]["commandIds"]
        disabled = self.device_body | {"state": "RESOURCE_STATE_DISABLED", "version": "2"}
        self.emit_records([{"topic": "psim.catalog.v1", "key": "device:" + self.device, "value": {"envelope": {"tenantId": self.tenant}, "device": disabled}}])
        self.wait(lambda: self.sql("SELECT version FROM command.resources WHERE tenant='" + self.tenant + "' AND kind='device' AND id='" + self.device + "'") == "2")
        requested = self.action(run)
        command = next(step for step in requested["steps"] if step["stepId"] == "cmd")["result"]["commandIds"][0]
        self.final(command, "FAILED")
        def failed():
            value = self.current_run(incident)
            return value if value["steps"][0]["state"] == "STEP_STATE_FAILED" else None
        run = self.wait(failed)
        self.device_body["version"] = "3"
        self.emit_records([{"topic": "psim.catalog.v1", "key": "device:" + self.device, "value": {"envelope": {"tenantId": self.tenant}, "device": self.device_body}}])
        self.wait(lambda: self.sql("SELECT version FROM command.resources WHERE tenant='" + self.tenant + "' AND kind='device' AND id='" + self.device + "'") == "3")
        run = self.action(run, "retry")
        self.action(run)
        self.action(self.at(incident, "end"))
        self.archive(plan)
        self.mark("manual zone target waits for confirmation; changed catalog rejects request durably, restored device executes a new attempt")

    def manual_approval(self):
        plan = self.publish(self.plan([
            {"stepId": "cmd", "title": "Unlock", "mandatory": True, "nextStepId": "end", "command": {"deviceId": self.device, "capability": "unlock"}},
            {"stepId": "end", "title": "Done", "instruction": {}}]))
        incident = self.new_incident()
        run = self.wait(lambda: self.current_run(incident))
        requested = self.action(run, role="chief")
        command_id = requested["steps"][0]["result"]["commandIds"][0]
        command = self.wait(lambda: self.current(command_id))
        assert command["requestedBy"]["id"] == self.subjects["chief"]
        assert command["requestedBy"]["kind"] == "ACTOR_KIND_USER"
        assert command["state"] == "COMMAND_STATE_AWAITING_APPROVAL"
        self.cq("POST", "/" + command_id + ":approve", {}, role="chief", version=command["version"], expected=403)
        self.cq("POST", "/" + command_id + ":approve", {}, role="bob", version=command["version"])
        self.action(self.at(incident, "end"))
        self.archive(plan)
        self.mark("manual critical step preserves the initiating supervisor: self-approval denied, another supervisor confirms")

    def failures(self):
        plan = self.publish(self.plan([
            {"stepId": "cmd", "title": "Reject", "mandatory": True, "nextStepId": "auto", "command": {"deviceId": self.device, "capability": "reject", "automatic": True}},
            {"stepId": "auto", "title": "Fail", "mandatory": True, "nextStepId": "end", "automated": {"webhookUrl": "https://response-handler:8443/fail", "timeout": "2s"}},
            {"stepId": "end", "title": "Done", "instruction": {}}]))
        incident = self.new_incident()
        def failed():
            value = self.current_run(incident)
            return value if any(step["state"] == "STEP_STATE_FAILED" for step in value["steps"]) else None
        run = self.wait(failed, 60)
        self.action(run, "override", {"reason": "verified"}, role="operator", expected=403)
        run = self.action(run, "retry")
        run = self.wait(failed, 60)
        assert next(step for step in run["steps"] if step["stepId"] == "cmd")["attempt"] == 2
        run = self.action(run, "override", {"reason": "manual recovery"}, role="chief")
        run = self.wait(lambda: value if (value := failed()) and value["activeStepIds"] == ["auto"] else None)
        self.action(run, "override", {"reason": "accepted failure"}, role="chief")
        self.action(self.at(incident, "end"))
        self.archive(plan)
        self.mark("failed command results, explicit retry with a new attempt, failed handler and supervisor override")

    def recovery(self):
        plan = self.publish(self.plan([
            {"stepId": "cmd", "title": "Arm", "nextStepId": "end", "command": {"deviceId": self.device, "capability": "arm", "automatic": True}},
            {"stepId": "end", "title": "Done", "instruction": {}}]))
        lock = subprocess.Popen(self.compose_cmd + ["exec", "-T", "-e", "PGAPPNAME=psim-response-lock", "postgres", "psql", "-U", "psim", "-d", "psim", "-At", "-c", "SELECT pg_advisory_lock(hashtextextended('response:outbox',0)); SELECT pg_sleep(30)"], cwd=ROOT, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        try:
            self.wait(lambda: self.sql("SELECT count(*) FROM pg_locks WHERE locktype='advisory' AND granted AND pid IN (SELECT pid FROM pg_stat_activity WHERE application_name='psim-response-lock')") == "1")
            incident = self.new_incident()
            run = self.wait(lambda: self.current_run(incident))
            command = next(step for step in run["steps"] if step["stepId"] == "cmd")["result"]["commandIds"][0]
            assert not any(item["id"] == command for item in self.executions())
            self.kill_response("response-engine")
        finally:
            self.sql("SELECT pg_terminate_backend(pid) FROM pg_stat_activity WHERE application_name='psim-response-lock'")
            lock.communicate(timeout=10)
        self.final(command)
        self.compose("up", "-d", "response-engine")
        self.action(self.at(incident, "end"))
        assert len([item for item in self.executions() if item["id"] == command]) == 1
        self.archive(plan)
        plan = self.publish(self.plan([{"stepId": "hold", "title": "Unknown handler", "mandatory": True,
            "automated": {"webhookUrl": "https://response-handler:8443/hold", "timeout": "2s"}}]))
        incident = self.new_incident()
        self.wait(lambda: any(call["incidentId"] == incident for call in self.calls()))
        self.kill_response("response-engine", "response-2")
        self.compose("up", "-d", "response-engine", "response-2")
        def failed():
            value = self.current_run(incident)
            return value if value["steps"][0]["state"] == "STEP_STATE_FAILED" else None
        run = self.wait(failed, 60)
        assert len([call for call in self.calls() if call["incidentId"] == incident]) == 1
        self.action(run, "override", {"reason": "external outcome checked"}, role="chief")
        self.archive(plan)
        self.mark("SIGKILL after outbox commit: second process delivers once; interrupted HTTPS effect fails by durable deadline without resending")

    def cancellation(self):
        plan = self.publish(self.plan([{"stepId": "wait", "title": "Wait", "mandatory": True, "wait": {"duration": "20s"}}]))
        incident = self.new_incident()
        self.wait(lambda: self.current_run(incident))
        # A close event cancels the run even when it arrives before an older repeated create.
        self.emit_records([{"topic": "psim.incidents.events.v1", "key": incident, "repeat": 2, "value": {
            "envelope": {"tenantId": self.tenant}, "incidentClosed": {"incident": self.incident(incident) | {"state": "INCIDENT_STATE_CLOSED", "version": "100000"}}}}])
        run = self.wait(lambda: value if (value := self.current_run(incident))["state"] == "RUN_STATE_CANCELLED" else None)
        assert not run.get("blockingStepIds")
        assert self.sql("SELECT count(*) FROM response.timers WHERE owner='" + run["runId"] + "' AND NOT cancelled AND fired_at IS NULL") == "0"
        self.archive(plan)
        self.mark("incident closure cancels persisted pending steps and timers")

    def timers(self):
        plan = self.publish(self.plan([{"stepId": "wait", "title": "Wait", "wait": {"duration": "2s"}}]))
        incidents = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
            incidents = list(executor.map(lambda _: self.new_incident(), range(self.count)))
        self.wait(lambda: self.sql("SELECT count(*) FROM response.runs WHERE plan->>'planId'='" + plan["planId"] + "' AND body->>'state'='RUN_STATE_COMPLETED'") == str(self.count), 90)
        delay = self.sql("SELECT max(extract(epoch FROM fired_at)*1000-due_ms) FROM response.timers WHERE owner IN (SELECT id FROM response.runs WHERE plan->>'planId'='" + plan["planId"] + "')")
        self.report["timer_max_error_ms"] = float(delay)
        assert float(delay) <= 1000, delay
        assert self.sql("SELECT count(*) FROM response.runs WHERE plan->>'planId'='" + plan["planId"] + "'") == str(self.count)
        self.archive(plan)
        self.mark(f"two-instance timer firing: {self.count} runs, maximum error {float(delay):.2f} ms")

    def replay(self):
        before = (self.sql("SELECT count(*) FROM command.commands"), self.sql("SELECT count(*) FROM response.psim_outbox"), len(self.calls()))
        binary = ROOT / f"build/debian13/{self.preset}/services/response-engine/psim-response-replay"
        # Only the replay executable and stdin are available; no credentials, sockets or network.
        args = ["docker", "run", "--rm", "-i", "--network", "none", "--read-only", "--cap-drop", "ALL", "--security-opt", "no-new-privileges", "--security-opt", "seccomp=unconfined", "-u", f"{os.getuid()}:{os.getgid()}", "-v", str(binary) + ":/replay:ro", "debian:trixie@sha256:913f6706df59a68922d1dd08f78c2476560a8d367897200a6005b00e5f67c2d5", "/replay"]
        replayed = json.loads(self.run(args, input=json.dumps({"plan": self.replay_plan, "history": [
            {"stepId": "read"}, {"stepId": "decision", "decisionOptionId": "real"}, {"stepId": "check", "checkedItemIds": ["safety"]}, {"stepId": "end"}]})))
        assert replayed["state"] == 2 and replayed["externalCalls"] == 0 and len(replayed["effects"]) == 5
        after = (self.sql("SELECT count(*) FROM command.commands"), self.sql("SELECT count(*) FROM response.psim_outbox"), len(self.calls()))
        assert before == after
        self.mark("replay with commands, notification, handler, escalation and wait: isolated executable, no credentials, no network, zero durable or external side effects")

    def cleanup(self):
        try:
            states = {}
            for service in ("response-engine", "response-2"):
                identity = self.compose("ps", "-aq", service).strip()
                if identity:
                    states[service] = json.loads(self.run(["docker", "inspect", identity]))[0]["State"]
            (self.output / "process-state.json").write_text(json.dumps(states, indent=2))
            self.compose("stop", "-t", "35", "response-engine", "response-2", "response-handler")
            logs = self.compose("logs", "--no-color", "response-engine", "response-2", "response-handler")
            (self.output / "response-servers.log").write_text(logs)
            assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs), logs[-2000:]
        finally:
            super().cleanup()


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.missing_plan()
        check.definitions()
        check.coverage_checkpoint()
        check.lifecycle()
        check.manual_command()
        check.manual_approval()
        check.failures()
        check.coverage_checkpoint()
        check.recovery()
        check.cancellation()
        check.timers()
        check.replay()
        check.report["passed"] = True
    finally:
        check.cleanup()


if __name__ == "__main__":
    main()
