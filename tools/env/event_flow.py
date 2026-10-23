#!/usr/bin/env python3
"""End-to-end event flow: real processes, browser rendering, durable effects and causal traces."""
import hashlib
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
PRESET = os.environ.get("PSIM_PRESET", "debug")
REPORT = ROOT / "build/event-flow-test"
SERVICES = ["connector-gateway", "normalizer", "correlation-engine", "incident-service", "projection-service", "api-gateway"]
PLAYWRIGHT = "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27"


def compose(project, *args, **kwargs):
    overlay = ["-f", "deploy/local/compose.sanitizer.yaml"] if PRESET == "tsan" else []
    return subprocess.run(["docker", "compose", "-p", project, "-f", "deploy/local/compose.yaml", *overlay, *args],
                          cwd=ROOT, check=True, text=True, capture_output=True,
                          env={**os.environ, "PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid())}, **kwargs)


def wait(predicate, seconds=90):
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        if predicate():
            return
        time.sleep(0.1)
    raise RuntimeError("timed out waiting for event flow test checkpoint")


def emit(project, name, count, repeat=1, *extra):
    result = compose(project, "run", "--rm", "simulator", "--count", str(count), "--interval-ms", "200", "--repeat", str(repeat), *extra)
    events = [json.loads(line) for line in result.stdout.splitlines() if line.startswith("{\"event_id\"")]
    assert len(events) == count, result.stderr
    (REPORT / f"{name}.json").write_text(json.dumps(events))
    return events


def sql(project, statement):
    return compose(project, "exec", "-T", "postgres", "psql", "-U", "psim", "-d", "psim", "-Atc", statement).stdout.strip()


def percentile(values, fraction):
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * fraction) - 1)]


def ready(project):
    sys.path.insert(0, str(ROOT / "tools/env"))
    import envctl
    envctl.PROBE_IMAGE = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
    env = envctl.Env(project)
    env.start_probe()
    try:
        for service in SERVICES:
            wait(lambda: env.http("GET", f"http://{service}:9100/health/ready")[0] == 200)
        wait(lambda: env.http("GET", "http://console:4173/")[0] == 200)
    finally:
        env.stop_probe()
    print("event flow: six services and console ready")


def main(project):
    REPORT.mkdir(parents=True, exist_ok=True)
    for path in REPORT.iterdir():
        if path.is_file():
            path.unlink()
    before = int(sql(project, "SELECT count(*) FROM incident.psim_inbox"))
    cache = ROOT / ".cache/web"
    command = ["docker", "run", "--rm", "--name", project + "-browser", "--network", "container:" + project + "-console-1", "--ipc=host",
               "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/cache/home", "-e", "COREPACK_HOME=/cache/corepack",
               "-e", "PLAYWRIGHT_BROWSERS_PATH=/ms-playwright", "-v", f"{ROOT}:/repo", "-v", f"{cache}:/cache",
               "-w", "/repo/web", PLAYWRIGHT, "bash", "-c",
               "node apps/console/node_modules/@playwright/test/cli.js test -c apps/console/playwright.event-flow.config.ts"]
    with (REPORT / "playwright.log").open("w") as output:
        browser = subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT, cwd=ROOT)
        def checkpoint(name):
            def ready():
                if browser.poll() is not None:
                    raise RuntimeError((REPORT / "playwright.log").read_text())
                return (REPORT / name).exists()
            wait(ready)
        try:
            checkpoint("warmup-ready")
            emit(project, "warmup", 3)
            checkpoint("measure-ready")
            measured = emit(project, "input", 30, 2)
            checkpoint("offline-ready")
            emit(project, "offline", 2)
            checkpoint("restart-ready")
            compose(project, "kill", "-s", "SIGKILL", "api-gateway")
            wait(lambda: compose(project, "ps", "--all", "--format", "{{.State}}", "api-gateway").stdout.strip() == "exited")
            compose(project, "start", "api-gateway")
            checkpoint("after-restart-ready")
            emit(project, "after-restart", 1)
            checkpoint("browser-done")
            assert browser.wait(timeout=30) == 0, (REPORT / "playwright.log").read_text()
        finally:
            if browser.poll() is None:
                subprocess.run(["docker", "rm", "-f", project + "-browser"], capture_output=True)
                browser.wait(timeout=20)
    expected = len(measured) + 6
    wait(lambda: int(sql(project, "SELECT count(*) FROM incident.psim_inbox")) == before + expected)
    wait(lambda: sql(project, "SELECT count(*) FROM incident.psim_outbox WHERE published_at IS NULL") == "0")
    # A restore is normalized but does not satisfy the alarm rule.
    emit(project, "restore", 1, 1, "--code", "RESTORE")
    time.sleep(1)
    assert int(sql(project, "SELECT count(*) FROM incident.psim_inbox")) == before + expected
    toolchain = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
    security = ["--security-opt", "seccomp=unconfined"] if PRESET == "tsan" else []
    probe = subprocess.run(["docker", "run", "--rm", *security, "--network", project + "_default", "-u", f"{os.getuid()}:{os.getgid()}",
        "-v", f"{ROOT}:/repo", "-w", "/repo", "-e", f"LLVM_PROFILE_FILE=/repo/build/debian13/{PRESET}/profiles/%p-%m.profraw", toolchain,
        f"build/debian13/{PRESET}/tools/event-flow/psim-event-flow-probe", "/repo/build/event-flow-test/input.json"], capture_output=True, text=True, check=True)
    accounting = json.loads(probe.stdout)
    sys.path.insert(0, str(ROOT / "tools/env"))
    import envctl
    envctl.PROBE_IMAGE = toolchain
    env = envctl.Env(project)
    env.start_probe()
    trace = None
    def causal_trace():
        nonlocal trace
        status, trace = env.http_json("GET", "http://tempo:3200/api/traces/" + measured[0]["trace_id"])
        if status != 200 or not trace:
            return False
        batches = trace.get("batches", [])
        services = {a["value"].get("stringValue") for batch in batches for a in batch["resource"]["attributes"] if a["key"] == "service.name"}
        spans = [span for batch in batches for scope in batch.get("scopeSpans", []) for span in scope.get("spans", [])]
        ids = {span["spanId"] for span in spans}
        return set(SERVICES + ["simulator"]) <= services and sum(not span.get("parentSpanId") for span in spans) == 1 and all(not span.get("parentSpanId") or span["parentSpanId"] in ids for span in spans)
    try:
        wait(causal_trace)
        (REPORT / "trace.json").write_text(json.dumps(trace, indent=2) + "\n")
    finally:
        env.stop_probe()
    browser_rows = json.loads((REPORT / "browser.json").read_text())
    signal = list(accounting["signal_ms"].values())
    rendered = [row["browser_ms"] for row in browser_rows]
    assert all(value >= 0 for value in signal + rendered)
    late = max(event["late_ms"] for event in measured)
    report = {"preset": PRESET, "samples": len(measured), "warmup": 3, "interval_ms": 200, "repeat": 2,
        "generator_late_ms": late, "baseline_valid": late <= 10, "clock": "single host, shared kernel clock",
        "accounting": accounting["counts"], "durable_effects": expected, "trace_services": SERVICES + ["simulator"],
        "event_to_signal_ms": {"p50": percentile(signal, .5), "p99": percentile(signal, .99), "max": max(signal)},
        "event_to_browser_ms": {"p50": percentile(rendered, .5), "p99": percentile(rendered, .99), "max": max(rendered)}}
    (REPORT / "baseline.json").write_text(json.dumps(report, indent=2) + "\n")
    compose(project, "--profile", "event-flow", "stop", *SERVICES, "console")
    for service in SERVICES:
        container = compose(project, "ps", "-a", "-q", service).stdout.strip()
        state = subprocess.run(["docker", "inspect", "-f", "{{.State.ExitCode}}", container], capture_output=True, text=True, check=True).stdout.strip()
        logs = compose(project, "logs", "--no-color", service).stdout
        assert state == "0", f"{service} failed its drain: exit {state}"
        assert "Sanitizer" not in logs and "runtime error:" not in logs, f"{service} sanitizer findings"
    print(json.dumps(report, indent=2))
    return 0


if __name__ == "__main__":
    sys.exit(ready(sys.argv[2]) if sys.argv[1] == "ready" else main(sys.argv[2]))
