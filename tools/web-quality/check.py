"""Build reproducible loading variants and measure browser transfers with actual HTTP caching."""
import io
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[2]
OUTPUT = ROOT / "build/web-quality"
NODE = "node:24.21.0-trixie@sha256:1278a37eb510ec1606fba0e80f554bcc941ae0b44f35ae373c4822ae7717c64d"
PLAYWRIGHT = "mcr.microsoft.com/playwright:v1.63.0-noble@sha256:eff16c30e6f3f4af0a03fa4b706120d5e9b0891c344a27d64559aff5900a4a27"


def container(command, image=NODE, directory="/repo/web", environment=None):
    args = ["docker", "run", "--rm", "--ipc=host", "-u", f"{os.getuid()}:{os.getgid()}", "-e", "HOME=/cache/home", "-e", "COREPACK_HOME=/cache/corepack", "-e", "CI=true", "-e", "PLAYWRIGHT_BROWSERS_PATH=/ms-playwright", "-v", str(ROOT) + ":/repo", "-v", str(ROOT / ".cache/web") + ":/cache", "-w", directory]
    for key, value in (environment or {}).items(): args += ["-e", key + "=" + value]
    script = "mkdir -p /tmp/psim-web-bin; corepack enable --install-directory /tmp/psim-web-bin pnpm; export PATH=/tmp/psim-web-bin:$PATH; " + shlex.join(command)
    subprocess.run(args + [image, "bash", "-c", script], cwd=ROOT, check=True)


def main():
    OUTPUT.mkdir(parents=True, exist_ok=True)
    for name in ("comparison.json", "loading.json"):
        (OUTPUT / name).unlink(missing_ok=True)
    revision = json.loads((ROOT / "tools/web-quality/baseline.json").read_text())["revision"]
    archive = subprocess.check_output(["git", "archive", revision, "web"], cwd=ROOT)
    reference = OUTPUT / "reference"
    shutil.rmtree(reference, ignore_errors=True)
    reference.mkdir()
    with tarfile.open(fileobj=io.BytesIO(archive)) as source: source.extractall(reference, filter="data")
    for package in (reference / "web", *(reference / "web/apps").iterdir(), *(reference / "web/packages").iterdir()):
        relative = package.relative_to(reference)
        original = ROOT / relative
        if (original / "node_modules").is_dir(): (package / "node_modules").symlink_to(os.path.relpath(original / "node_modules", package), target_is_directory=True)
        if (original / "src/gen").is_dir():
            (package / "src/gen").symlink_to(os.path.relpath(original / "src/gen", package / "src"), target_is_directory=True)
    variants = []
    combinations = [("baseline", "inline", "combined"), ("inline", "inline", "combined"), ("eager", "eager", "combined"), ("widgets", "widgets", "combined"), ("selected", "selected", "combined"), ("bilingual", "bilingual", "combined"), ("application", "inline", "application"), ("design", "inline", "design")]
    for name, variant, entry in combinations:
        directory = OUTPUT / "variants" / name
        source = reference / "web/apps/console" if name == "baseline" else ROOT / "web/apps/console"
        print("build:", name, flush=True)
        container(["node", "/repo/web/apps/console/node_modules/vite/bin/vite.js", "build", "--outDir", "/repo/" + str(directory.relative_to(ROOT)), "--emptyOutDir"], directory="/repo/" + str(source.relative_to(ROOT)), environment={"PSIM_WEB_ENTRY": entry, **({"PSIM_WEB_VARIANT": variant} if name != "inline" else {})})
        variants.append({"name": name, "entry": entry, "directory": "/repo/" + str(directory.relative_to(ROOT)), "revision": revision if name == "baseline" else "workspace"})
    (OUTPUT / "variants.json").write_text(json.dumps(variants, indent=2) + "\n")
    container(["node", "tools/web-quality/loading.mjs"], image=PLAYWRIGHT, directory="/repo")
    container(["node", "tools/web-quality/assets.mjs"], directory="/repo")


if __name__ == "__main__": main()
