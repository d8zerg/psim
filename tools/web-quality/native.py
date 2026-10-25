"""Run browser acceptance against isolated native service projects and remove every profile."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]


def fingerprint(name, preset):
    digest = hashlib.sha256(preset.encode())
    digest.update((ROOT / "web/apps/console/dist/index.html").read_bytes())
    paths = subprocess.check_output(["git", "ls-files", "-c", "-o", "--exclude-standard", "libs", "services", "contracts", "deploy", "tools", "CMakeLists.txt", "CMakePresets.json"], cwd=ROOT).decode().splitlines()
    paths += [str(path.relative_to(ROOT)) for path in (ROOT / "web/apps/console" / ("e2e-" + name)).glob("**/*.ts")]
    paths.append("web/apps/console/playwright." + name + ".config.ts")
    for relative in sorted(set(paths)):
        path = ROOT / relative
        if path.is_file():
            digest.update(relative.encode() + b"\0" + path.read_bytes())
    return digest.hexdigest()


def main(prefix, preset, resume=False):
    output = ROOT / "build/web-quality/native.json"
    output.parent.mkdir(parents=True, exist_ok=True)
    previous = json.loads(output.read_text()).get("checks", []) if resume and output.exists() else []
    report = {"passed": False, "checks": []}
    environment = os.environ | {"PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid())}
    try:
        for name in ("operator", "admin", "insights"):
            project = prefix + "-" + name
            fixture = ROOT / "build" / name / preset / "report.json"
            signature = fingerprint(name, preset)
            saved = next((item for item in previous if item["name"] == name and item.get("fingerprint") == signature), None)
            if saved and fixture.exists() and json.loads(fixture.read_text()).get("passed"):
                report["checks"].append(saved | {"reused": True})
                print("reuse: " + name, flush=True)
                continue
            started = time.monotonic()
            try:
                subprocess.run([sys.executable, "-I", "tools/" + name + "/check.py", project, preset], cwd=ROOT, check=True)
                fixture = ROOT / "build" / name / preset / "report.json"
                result = json.loads(fixture.read_text())
                if not result.get("passed"):
                    raise RuntimeError("Native browser acceptance failed: " + name)
                report["checks"].append({"name": name, "passed": True, "elapsedSeconds": round(time.monotonic() - started, 3), "report": str(fixture.relative_to(ROOT)), "fingerprint": signature})
            finally:
                subprocess.run(["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml"), "--profile", "*", "down", "--volumes", "--remove-orphans"], cwd=ROOT, env=environment, check=True)
        report["passed"] = True
    finally:
        output.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], len(sys.argv) > 3 and sys.argv[3] == "--resume")
