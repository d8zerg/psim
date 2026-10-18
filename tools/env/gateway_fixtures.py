"""Create bounded synthetic connector identities for gateway component and load checks."""
from pathlib import Path
import json
import os
import subprocess
import uuid


def main():
    source = Path(".cache/event-flow")
    target = Path(".cache/gateway-test")
    target.mkdir(parents=True, exist_ok=True)
    identities = []
    for index in range(20):
        identity = str(uuid.uuid4())
        prefix = target / str(index)
        for suffix in ("", "-replacement"):
            stem = Path(str(prefix) + suffix)
            key = Path(str(stem) + ".key")
            if key.exists():
                key.chmod(0o600)
            subprocess.run(["openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-noenc", "-subj", "/CN=psim-gateway-test", "-keyout", str(stem) + ".key", "-out", str(stem) + ".csr"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            extension = Path(str(stem) + ".ext")
            extension.write_text("subjectAltName=URI:psim://connector/" + identity + "\nextendedKeyUsage=clientAuth\n")
            subprocess.run(["openssl", "x509", "-req", "-days", "7", "-in", str(stem) + ".csr", "-CA", str(source / "ca.pem"), "-CAkey", str(source / "ca.key"), "-CAcreateserial", "-extfile", str(extension), "-out", str(stem) + ".pem"], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            Path(str(stem) + ".key").chmod(0o400)
        identities.append(identity)
    (target / "identities.json").write_text(json.dumps(identities))
    print("gateway: synthetic client and replacement certificates generated")


if __name__ == "__main__":
    main()
