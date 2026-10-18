"""Run gateway checks with isolated processes and preserve diagnostic artifacts."""
import hashlib
import json
import os
import re
from pathlib import Path
import subprocess
import sys
import time
import urllib.request

import yaml

ROOT = Path(__file__).resolve().parents[2]


def compose(project, preset, *args, check=True):
    overlay = ["-f", "deploy/local/compose.sanitizer.yaml"] if preset == "tsan" else []
    return subprocess.run(["docker", "compose", "-p", project, "-f", "deploy/local/compose.yaml", *overlay, "--profile", "gateway-test", *args], cwd=ROOT, env={**os.environ, "PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid()), "PSIM_PRESET": preset}, check=check, text=True, capture_output=True)


def ready(project):
    sys.path.insert(0, str(ROOT / "tools/env"))
    import envctl
    envctl.PROBE_IMAGE = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
    env = envctl.Env(project)
    env.start_probe()
    try:
        deadline = time.monotonic() + 90
        for server in ("connector-gateway", "gateway-2"):
            while time.monotonic() < deadline:
                try:
                    if env.http("GET", f"http://{server}:9100/health/ready")[0] == 200:
                        break
                except Exception:
                    pass
                time.sleep(0.1)
            else:
                raise RuntimeError("gateway readiness timed out: " + server)
    finally:
        env.stop_probe()
    print("gateway: both instances ready")


def command(project, preset, executable, *args):
    image = yaml.safe_load((ROOT / "Taskfile.yml").read_text())["vars"]["RUNTIME_IMAGES"]["map"]["debian13"]
    security = ["--security-opt", "seccomp=unconfined"] if preset == "tsan" else []
    return ["docker", "run", "--rm", *security, "--network", project + "_default", "-u", f"{os.getuid()}:{os.getgid()}",
            "-v", f"{ROOT}:/repo", "-w", "/repo", "-e", f"LLVM_PROFILE_FILE=/repo/build/debian13/{preset}/profiles/%p-%m.profraw",
            image, f"build/debian13/{preset}/services/connector-gateway/{executable}", *args]


def main(project, preset, mode):
    report = ROOT / "build/gateway-test" / preset
    report.mkdir(parents=True, exist_ok=True)
    if mode == "shutdown":
        compose(project, preset, "stop", "connector-gateway", "gateway-2")
        logs = compose(project, preset, "logs", "--no-color", "connector-gateway", "gateway-2").stdout
        (report / "servers.log").write_text(logs)
        for service in ("connector-gateway", "gateway-2"):
            instance = compose(project, preset, "ps", "-aq", service).stdout.strip()
            state = json.loads(subprocess.check_output(["docker", "inspect", instance], text=True))[0]["State"]
            if state["Running"] or state["ExitCode"] != 0:
                raise RuntimeError(f"{service} did not stop cleanly: {state['ExitCode']}")
        if re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs):
            raise RuntimeError("gateway sanitizer diagnostics found in server logs")
        print("gateway: both instances drained and stopped cleanly")
        return
    output = report / (mode + ".log")
    started = time.monotonic()
    try:
        for service in ("connector-gateway", "gateway-2"):
            instance = compose(project, preset, "ps", "-aq", service).stdout.strip()
            configuration = json.loads(subprocess.check_output(["docker", "inspect", instance], text=True))[0]
            expected = f"/repo/build/debian13/{preset}/services/connector-gateway/psim-connector-gateway"
            if configuration["Config"]["Cmd"][0] != expected:
                raise RuntimeError("gateway build preset does not match the test preset")
        ready(project)
        if mode == "network":
            result = subprocess.run(command(project, preset, "psim_gateway_network_component_test"), cwd=ROOT, text=True, capture_output=True, timeout=300)
            output.write_text(result.stdout + result.stderr)
            print(result.stdout)
            if result.returncode:
                raise RuntimeError(result.stderr or "gateway component checks failed")
        elif mode == "bench":
            result = subprocess.run(command(project, preset, "psim-gateway-probe", "bench"), cwd=ROOT, text=True, capture_output=True, timeout=180)
            output.write_text(result.stdout + result.stderr)
            print(result.stdout)
            if result.returncode:
                raise RuntimeError(result.stderr or "gateway benchmark failed")
        elif mode == "failover":
            sys.path.insert(0, str(ROOT / "tools/env"))
            import envctl
            env = envctl.Env(project)
            env.start_probe()
            process = None
            try:
                _, proxies = env.http_json("GET", "http://toxiproxy:8474/proxies")
                if "gateway-failover" in proxies:
                    env.http("DELETE", "http://toxiproxy:8474/proxies/gateway-failover")
                status, _ = env.http("POST", "http://toxiproxy:8474/proxies", {"name": "gateway-failover", "listen": "0.0.0.0:18080", "upstream": "connector-gateway:8080", "enabled": True})
                if status != 201:
                    raise RuntimeError("cannot create gateway fault proxy")
                with output.open("w") as log:
                    process = subprocess.Popen(command(project, preset, "psim-gateway-probe", "failover"), cwd=ROOT, stdout=log, stderr=subprocess.STDOUT, text=True)
                    deadline = time.monotonic() + 40
                    while "READY" not in output.read_text() and time.monotonic() < deadline and process.poll() is None:
                        time.sleep(0.1)
                    if "READY" not in output.read_text():
                        raise RuntimeError("SDK did not reach the initial gateway session")
                    time.sleep(0.1)
                    compose(project, preset, "kill", "-s", "SIGKILL", "connector-gateway")
                    status, detail = env.http("PATCH", "http://toxiproxy:8474/proxies/gateway-failover", {"name": "gateway-failover", "listen": "0.0.0.0:18080", "upstream": "gateway-2:8080", "enabled": True})
                    if status != 200:
                        raise RuntimeError(f"gateway fault proxy update failed: {status}: {detail}")
                    if process.wait(timeout=100) != 0:
                        raise RuntimeError("SDK failover or reconciliation failed")
                print(output.read_text())
            finally:
                if process is not None and process.poll() is None:
                    process.terminate()
                    process.wait(timeout=10)
                env.http("DELETE", "http://toxiproxy:8474/proxies/gateway-failover")
                env.stop_probe()
                compose(project, preset, "up", "-d", "connector-gateway")
        else:
            raise RuntimeError("unknown gateway check")
        print(f"gateway {mode}: passed in {time.monotonic() - started:.1f}s")
    finally:
        logs = compose(project, preset, "logs", "--no-color", "connector-gateway", "gateway-2", check=False)
        (report / "servers.log").write_text(logs.stdout + logs.stderr)


if __name__ == "__main__":
    main(*sys.argv[1:])
