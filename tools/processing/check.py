"""Run normalization and history checks against an isolated native Docker Compose project."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import re
import subprocess
import sys
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
TASK = shutil.which("task") or str(Path.home() / ".local/bin/task")
sys.path.insert(0, str(ROOT / "tools/env"))
import envctl


class Check:
    def __init__(self, project, preset, count):
        self.project, self.preset, self.count = project, preset, count
        self.output = ROOT / "build/processing" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.env = os.environ | {"PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid()), "PSIM_PRESET": preset}
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", "deploy/local/compose.yaml"]
        if preset == "tsan":
            self.compose_cmd += ["-f", "deploy/local/compose.sanitizer.yaml"]
        override = self.output / "compose.yaml"
        overrides = {"normalizer": {"environment": {"PSIM__TELEMETRY__SAMPLING_RATIO": "0.001"}}}
        self.normalizer_shards = int(os.environ.get("PSIM_PROCESSING_SHARDS", "1"))
        if not 1 <= self.normalizer_shards <= 4:
            raise ValueError("invalid processing shard count")
        for name in ("normalizer", "normalizer-2", "normalizer-3"):
            overrides.setdefault(name, {}).setdefault("environment", {})["PSIM__RUNTIME__SHARDS"] = str(self.normalizer_shards)
        gap = os.environ.get("PSIM_NORMALIZER_TRANSACTION_GAP_MS")
        if gap is not None:
            for name in ("normalizer", "normalizer-2", "normalizer-3"):
                overrides.setdefault(name, {}).setdefault("environment", {})["PSIM__SETTINGS__TRANSACTION_GAP_MS"] = gap
        override.write_text(json.dumps({"services": overrides}))
        self.compose_cmd += ["-f", str(override)]
        self.toolchain = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
        self.owned = subprocess.run(["docker", "network", "inspect", project + "_default"], capture_output=True).returncode != 0
        self.probe = envctl.Env(project)
        self.apps = ["normalizer", "normalizer-2", "normalizer-3", "event-history", "history-2"]
        self.report = {"passed": False, "events": count, "normalizer_shards_per_instance": self.normalizer_shards, "checks": []}

    def run(self, args, log_output=True):
        result = subprocess.run(args, cwd=ROOT, env=self.env, capture_output=True, text=True)
        self.log.write(" ".join(args) + "\n" + (result.stdout if log_output else "<row identifiers collected>\n") + result.stderr + "\n")
        self.log.flush()
        if result.returncode:
            raise RuntimeError(result.stderr[-4000:] + result.stdout[-2000:])
        return result.stdout

    def compose(self, *args):
        return self.run(self.compose_cmd + list(args))

    def docker(self, *command):
        return ["docker", "run", "--rm", "--security-opt", "seccomp=unconfined", "-u", f"{os.getuid()}:{os.getgid()}", "--network", self.project + "_default",
                "-e", f"LLVM_PROFILE_FILE=/repo/build/debian13/{self.preset}/profiles/%p-%m.profraw",
                "-v", str(ROOT) + ":/repo", "-w", "/repo", self.toolchain, *command]

    def command(self, mode, count=100, seed=""):
        return self.docker(f"build/debian13/{self.preset}/tools/processing/psim-processing-probe", mode, str(count), *([seed] if seed else []))

    def wait(self, predicate, timeout=120):
        deadline = time.monotonic() + timeout
        last_error = ""
        while time.monotonic() < deadline:
            try:
                if predicate():
                    return
            except (RuntimeError, OSError) as error:
                last_error = str(error)
            time.sleep(0.2)
        raise RuntimeError("processing check timed out: " + last_error)

    def ready(self, services):
        for service in services:
            self.wait(lambda: self.probe.http("GET", "http://" + service + ":9100/health/ready")[0] == 200)

    def balanced(self, normalizers=3):
        for group, members in (("psim-normalizer", normalizers * self.normalizer_shards), ("psim-event-history", 2)):
            def stable():
                state = self.compose("exec", "-T", "kafka-1", "/opt/kafka/bin/kafka-consumer-groups.sh", "--bootstrap-server", "kafka-1:19092", "--describe", "--group", group, "--state")
                return any(re.search(r"\bStable\s+" + str(members) + r"\s*$", row) for row in state.splitlines())
            self.wait(stable)

    def mark(self, text):
        self.report["checks"].append(text)
        print("ok: " + text, flush=True)

    def history(self, seed):
        query = "SELECT count(), uniqExact(event_id) FROM events FINAL WHERE tenant_id='11111111-1111-4111-8111-111111111111' AND position(payload, '" + seed + "') > 0"
        result = self.compose("exec", "-T", "clickhouse", "clickhouse-client", "--user", "psim", "--password", "psim-dev-clickhouse", "-q", query).strip()
        return [int(x) for x in result.split("\t")]

    def reconcile_history(self, seed, count):
        query = "SELECT event_id FROM events FINAL WHERE tenant_id='11111111-1111-4111-8111-111111111111' AND position(payload, '" + seed + "') > 0 FORMAT TSV"
        rows = self.run(self.compose_cmd + ["exec", "-T", "clickhouse", "clickhouse-client", "--user", "psim", "--password", "psim-dev-clickhouse", "-q", query], log_output=False).splitlines()
        namespace = uuid.UUID(seed)
        expected = {str(uuid.uuid5(namespace, chr(len(str(index))) + str(index))) for index in range(count)}
        if len(rows) != count or set(rows) != expected:
            raise RuntimeError("ClickHouse identifiers differ from the generated input set")

    def prepare(self):
        (ROOT / f"build/debian13/{self.preset}/profiles").mkdir(parents=True, exist_ok=True)
        secrets = ROOT / ".cache/processing"
        secrets.mkdir(parents=True, exist_ok=True)
        password = secrets / "clickhouse.password"
        if password.exists():
            password.chmod(0o600)
        password.write_text("psim-dev-clickhouse")
        password.chmod(0o400)
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keeper", "clickhouse", "otel-collector")
        self.run(self.docker("sh", "tools/env/event_flow_tls.sh"))
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.run(self.command("seed"))
        self.compose("up", "-d", "--force-recreate", *self.apps)
        envctl.PROBE_IMAGE = self.toolchain
        self.probe.start_probe()
        self.ready(self.apps)
        self.balanced()
        self.mark("three normalizers and two history loaders ready with restored replicas")
        warmup = str(uuid.uuid4())
        warmup_count = 50000 if self.preset == "release" and self.count >= 100000 else 1000
        self.report["warmup_events"] = warmup_count
        self.run(self.command("emit", warmup_count, warmup))
        self.run(self.command("collect", warmup_count, warmup))
        self.wait(lambda: self.history(warmup) == [warmup_count, warmup_count])

    def check(self):
        seed = str(uuid.uuid4())
        collector = subprocess.Popen(self.command("observe", self.count, seed), cwd=ROOT, env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            if collector.stdout.readline().strip() != "ready":
                raise RuntimeError("normalization observer failed to join")
            emitted = json.loads(self.run(self.command("emit", self.count, seed)).splitlines()[-1])
            emission_finished = time.monotonic()
            output, errors = collector.communicate(timeout=120)
            if collector.returncode:
                raise RuntimeError(errors)
            normalized = json.loads(output.splitlines()[-1])
        finally:
            if collector.poll() is None:
                collector.terminate()
                collector.wait(timeout=10)
        self.wait(lambda: self.history(seed) == [self.count, self.count])
        history_lag = time.monotonic() - emission_finished
        self.run(self.command("emit", self.count, seed))
        self.run(self.command("collect", self.count, seed))
        self.wait(lambda: self.history(seed) == [self.count, self.count])
        self.report["history_lag_upper_bound_seconds"] = history_lag
        self.reconcile_history(seed, self.count)
        self.report["emission"] = emitted
        self.report["normalization"] = normalized
        print("normalization: " + json.dumps(normalized), flush=True)
        self.mark("duplicate input reconciles to exactly one normalized event and history row per identifier")
        self.compose("kill", "-s", "SIGKILL", "normalizer", "event-history")
        self.compose("start", "normalizer", "event-history")
        self.ready(["normalizer", "event-history"])
        self.run(self.command("emit", self.count, seed))
        self.run(self.command("collect", self.count, seed))
        self.wait(lambda: self.history(seed) == [self.count, self.count])
        self.mark("SIGKILL recovery and partition transfer preserve committed source cursors and history identity")
        interrupted = str(uuid.uuid4())
        self.compose("stop", "clickhouse")
        self.run(self.command("emit", 2000, interrupted))
        self.wait(lambda: int(self.compose("exec", "-T", "postgres", "psql", "-U", "psim", "-d", "psim", "-Atc", "SELECT count(*) FROM psim_clickhouse.pending_batches").strip()) > 0)
        self.compose("kill", "-s", "SIGKILL", "event-history", "history-2")
        self.compose("start", "clickhouse", "event-history", "history-2")
        self.ready(["event-history", "history-2"])
        self.wait(lambda: self.history(interrupted) == [2000, 2000])
        self.reconcile_history(interrupted, 2000)
        self.mark("prepared blocks survive unavailable ClickHouse and SIGKILL of both history loaders")
        invalid = str(uuid.uuid4())
        self.run(self.command("invalid", 10, invalid))
        self.run(self.command("dlq", 10, invalid))
        self.mark("unmapped events retain their original payload in the normalization DLQ")
        bad_key = str(uuid.uuid4())
        self.run(self.command("badkey", 10, bad_key))
        self.run(self.command("dlq", 10, bad_key))
        self.mark("a raw partition key inconsistent with its source is rejected before cursor mutation")
        large = str(uuid.uuid4())
        self.run(self.command("large", 200, large))
        self.run(self.command("collect", 200, large))
        self.wait(lambda: self.history(large) == [200, 200], timeout=300)
        self.reconcile_history(large, 200)
        self.mark("large escaped attributes fit bounded journal blocks and survive exact history reconciliation")
        following = str(uuid.uuid4())
        self.run(self.command("emit", 100, following))
        self.run(self.command("collect", 100, following))
        self.wait(lambda: self.history(following) == [100, 100])
        self.mark("normalization and history continue after restarts and permanent input failures")
        if self.preset == "release" and self.count >= 100000:
            self.report["performance_passed"] = normalized["p99_stage_ms"] <= 30 and history_lag <= 10 and emitted["seconds"] <= self.count / 47500 and normalized["visible_seconds"] <= self.count / 47500
            if not self.report["performance_passed"]:
                raise RuntimeError("processing performance budget failed: " + json.dumps(self.report))
        self.report["passed"] = True

    def close(self):
        self.probe.stop_probe()
        self.compose("stop", *self.apps)
        logs = self.compose("logs", "--no-color", *self.apps)
        (self.output / "services.log").write_text(logs)
        if self.owned:
            self.compose("--profile", "processing-test", "--profile", "event-flow", "down", "-v", "--remove-orphans")
        if "ERROR: AddressSanitizer" in logs or "WARNING: ThreadSanitizer" in logs or "runtime error:" in logs:
            self.report["passed"] = False
            raise RuntimeError("sanitizer reported a processing failure")


def main():
    check = Check(sys.argv[1], sys.argv[2], int(sys.argv[3]))
    try:
        check.prepare()
        check.check()
    finally:
        try:
            check.close()
        finally:
            (check.output / "report.json").write_text(json.dumps(check.report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
