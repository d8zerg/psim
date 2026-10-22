"""Run diagnostic response replay with only a binary and bounded stdin, without network or credentials."""
import os
from pathlib import Path
import subprocess
import sys

binary, source = (Path(value).resolve(strict=True) for value in sys.argv[1:3])
if source.stat().st_size > 1048576:
    raise SystemExit("replay input exceeds 1 MiB")
result = subprocess.run([
    "docker", "run", "--rm", "-i", "--network", "none", "--read-only", "--cap-drop", "ALL",
    "--security-opt", "no-new-privileges", "-u", f"{os.getuid()}:{os.getgid()}",
    "-v", str(binary) + ":/replay:ro", sys.argv[3], "/replay",
], input=source.read_bytes(), check=False)
raise SystemExit(result.returncode)
