"""Run real Catalog acceptance checks in an isolated local environment."""
import datetime as dt
import hashlib
import hmac
import json
import os
from pathlib import Path
import ssl
import shutil
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request
import uuid
import zlib

ROOT = Path(__file__).resolve().parents[2]
TENANT = "11111111-1111-4111-8111-111111111111"
OTHER_TENANT = "99999999-9999-4999-8999-999999999999"
TASK = os.environ.get("PSIM_TASK") or shutil.which("task") or str(Path.home() / ".local/bin/task")


def uid():
    return str(uuid.uuid4())


def utc(seconds=0):
    return (dt.datetime.now(dt.timezone.utc) + dt.timedelta(seconds=seconds)).isoformat().replace("+00:00", "Z")


def png(width=2, height=2, padding=0):
    def chunk(name, data):
        return struct.pack(">I", len(data)) + name + data + struct.pack(">I", zlib.crc32(name + data))
    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    if padding:
        data += chunk(b"npAd", b"Comment\0" + b"x" * padding)
    return data + chunk(b"IDAT", zlib.compress((b"\0" + b"\xff" * width * 4) * height)) + chunk(b"IEND", b"")


class Check:
    def __init__(self, project, preset, count):
        self.project, self.preset, self.count = project, preset, count
        self.output = ROOT / "build" / "resource-catalog" / preset
        self.output.mkdir(parents=True, exist_ok=True)
        self.log = (self.output / "commands.log").open("w")
        self.tokens, self.ports = {}, {}
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem") if (ROOT / ".cache/event-flow/ca.pem").exists() else None
        self.observer = None
        self.observer_name = project + "-catalog-observer"
        self.owned = subprocess.run(["docker", "network", "inspect", project + "_default"], capture_output=True).returncode != 0
        self.override = self.output / "ports.yaml"
        self.override.write_text("services:\n" + "".join(f"  {service}:\n    ports: ['127.0.0.1::{port}']\n" for service, port in [("resource-catalog", 8443), ("catalog-2", 8443), ("keycloak", 8080), ("registry", 8080), ("s3", 8443)]))
        self.env = os.environ | {"PSIM_PRESET": preset, "PSIM_UID": str(os.getuid()), "PSIM_GID": str(os.getgid()), "PSIM_GATEWAY_SAMPLING_RATIO": "0.001"}
        self.compose_cmd = ["docker", "compose", "-p", project, "-f", str(ROOT / "deploy/local/compose.yaml")]
        if preset == "tsan":
            self.compose_cmd += ["-f", str(ROOT / "deploy/local/compose.sanitizer.yaml")]
        self.compose_cmd += ["-f", str(self.override)]
        self.toolchain = "psim-platform/toolchain:debian13-" + hashlib.sha256((ROOT / "tools/toolchain/Dockerfile").read_bytes()).hexdigest()[:12]
        self.site_id, self.foreign_site = uid(), uid()
        self.fixtures = uuid.uuid4().hex[:10]
        self.report = {"passed": False, "devices": count, "checks": []}

    def run(self, args, **kwargs):
        result = subprocess.run(args, cwd=ROOT, env=self.env, text=True, capture_output=True, **kwargs)
        self.log.write(" ".join(args[:8]) + "\n" + result.stdout + result.stderr + "\n")
        self.log.flush()
        if result.returncode:
            raise RuntimeError(f"command failed: {args[:5]}\n{result.stdout[-1000:]}\n{result.stderr[-2000:]}")
        return result.stdout

    def compose(self, *args):
        result = self.run(self.compose_cmd + list(args))
        if args and args[0] in ("start", "up", "restart"):
            self.ports.clear()
        return result

    def env_command(self, command, network=True):
        args = ["docker", "run", "--rm", "--security-opt", "seccomp=unconfined", "-u", f"{os.getuid()}:{os.getgid()}"]
        if network:
            args += ["--network", self.project + "_default"]
        else:
            args += ["--network", "none"]
        return self.run(args + ["-v", str(ROOT) + ":/repo", "-w", "/repo", self.toolchain, "bash", "-c", command])

    def port(self, service, port):
        if service not in self.ports:
            self.ports[service] = self.compose("port", service, str(port)).strip().rsplit(":", 1)[1]
        return self.ports[service]

    def keycloak(self, method, path, token=None, body=None, form=False):
        headers = {"Host": "keycloak:8080"}
        if token:
            headers["Authorization"] = "Bearer " + token
        data = None
        if body is not None:
            data = urllib.parse.urlencode(body).encode() if form else json.dumps(body).encode()
            headers["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json"
        request = urllib.request.Request("http://localhost:" + self.port("keycloak", 8080) + path, data=data, method=method, headers=headers)
        with urllib.request.urlopen(request, timeout=15) as response:
            content = response.read()
            return json.loads(content) if content else None

    def principals(self):
        admin = self.keycloak("POST", "/realms/master/protocol/openid-connect/token", body={"grant_type": "password", "client_id": "admin-cli", "username": "admin", "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
        clients = "/admin/realms/psim/clients"
        resource = self.keycloak("GET", clients + "?clientId=psim", admin)[0]["id"]
        cases = {"admin": ("admin", ["*"], TENANT), "site": ("admin", [self.site_id], TENANT), "other": ("admin", [self.foreign_site], TENANT), "operator": ("operator", [self.site_id], TENANT), "empty": ("admin", [], TENANT), "tenant": ("admin", ["*"], OTHER_TENANT)}
        for name, (role, scopes, tenant) in cases.items():
            client_id = "psim-catalog-" + self.fixtures + "-" + name
            self.keycloak("POST", clients, admin, {"clientId": client_id, "enabled": True, "publicClient": False, "serviceAccountsEnabled": True, "standardFlowEnabled": False, "secret": "psim-dev-catalog", "protocol": "openid-connect", "protocolMappers": [
                {"name": "audience", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper", "config": {"included.client.audience": "psim", "access.token.claim": "true"}},
                {"name": "tenant", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_tenant", "claim.value": tenant, "jsonType.label": "String", "access.token.claim": "true"}},
                {"name": "scope", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper", "config": {"claim.name": "psim_scopes", "claim.value": json.dumps(scopes), "jsonType.label": "JSON", "access.token.claim": "true"}},
            ]})
            found = self.keycloak("GET", clients + "?clientId=" + client_id, admin)[0]["id"]
            account = self.keycloak("GET", clients + "/" + found + "/service-account-user", admin)["id"]
            role_body = self.keycloak("GET", clients + "/" + resource + "/roles/" + role, admin)
            self.keycloak("POST", "/admin/realms/psim/users/" + account + "/role-mappings/clients/" + resource, admin, [role_body])
            self.tokens[name] = {"client": client_id, "value": "", "expires": 0}

    def token(self, name):
        entry = self.tokens[name]
        if entry["expires"] < time.time() + 30:
            response = self.keycloak("POST", "/realms/psim/protocol/openid-connect/token", body={"grant_type": "client_credentials", "client_id": entry["client"], "client_secret": "psim-dev-catalog"}, form=True)
            entry["value"], entry["expires"] = response["access_token"], time.time() + response["expires_in"]
        return entry["value"]

    def request(self, method, path, body=None, role="admin", version=None, expected=200, key=None, content_type=None, service="resource-catalog"):
        data = body if isinstance(body, bytes) else json.dumps(body).encode() if body is not None else None
        headers = {}
        if role:
            headers["Authorization"] = "Bearer " + self.token(role)
        if data is not None:
            headers["Content-Type"] = content_type or "application/json"
        if method in ("POST", "PUT", "PATCH", "DELETE"):
            headers["Idempotency-Key"] = key or uid()
        if version is not None:
            headers["If-Match"] = '"' + str(version) + '"'
        url = "https://localhost:" + self.port(service, 8443) + path
        try:
            with urllib.request.urlopen(urllib.request.Request(url, method=method, data=data, headers=headers), context=self.ca, timeout=90) as response:
                status, raw = response.status, response.read()
                media = response.headers.get_content_type()
        except urllib.error.HTTPError as error:
            status, raw = error.code, error.read()
            media = error.headers.get_content_type()
        assert status == expected, f"{method} {path[:100]}: expected {expected}, got {status}, {raw[:500]!r}"
        return json.loads(raw) if media in ("application/json", "application/problem+json") and raw[:1] in (b"{", b"[") else raw

    def s3_request(self, method, key, authenticated=False):
        port = self.port("s3", 8443)
        host = "localhost:" + port
        path = "/psim-plans/" + urllib.parse.quote(key, safe="/")
        headers = {}
        if authenticated:
            now = dt.datetime.now(dt.timezone.utc)
            day, stamp = now.strftime("%Y%m%d"), now.strftime("%Y%m%dT%H%M%SZ")
            digest = hashlib.sha256(b"").hexdigest()
            signed = "host;x-amz-content-sha256;x-amz-date"
            canonical = f"{method}\n{path}\n\nhost:{host}\nx-amz-content-sha256:{digest}\nx-amz-date:{stamp}\n\n{signed}\n{digest}"
            scope = day + "/us-east-1/s3/aws4_request"
            value = "AWS4-HMAC-SHA256\n" + stamp + "\n" + scope + "\n" + hashlib.sha256(canonical.encode()).hexdigest()
            signing = b"AWS4" + (ROOT / ".cache/s3-tls/filestore.secret").read_bytes()
            for part in (day, "us-east-1", "s3", "aws4_request"):
                signing = hmac.new(signing, part.encode(), hashlib.sha256).digest()
            signature = hmac.new(signing, value.encode(), hashlib.sha256).hexdigest()
            access = (ROOT / ".cache/s3-tls/filestore.access").read_text()
            headers = {"x-amz-content-sha256": digest, "x-amz-date": stamp, "Authorization": f"AWS4-HMAC-SHA256 Credential={access}/{scope}, SignedHeaders={signed}, Signature={signature}"}
        context = ssl.create_default_context(cafile=ROOT / ".cache/s3-tls/ca.pem")
        try:
            with urllib.request.urlopen(urllib.request.Request("https://" + host + path, method=method, headers=headers), context=context, timeout=15) as response:
                return response.status
        except urllib.error.HTTPError as error:
            return error.code

    def sql(self, statement):
        return self.compose("exec", "-T", "postgres", "psql", "-U", "psim", "-d", "psim", "-At", "-v", "ON_ERROR_STOP=1", "-c", statement).strip()

    def wait(self, callback, timeout=30):
        end = time.monotonic() + timeout
        last = None
        while time.monotonic() < end:
            try:
                result = callback()
                if result:
                    return result
            except (OSError, ValueError, AssertionError) as error:
                last = error
            time.sleep(0.1)
        raise AssertionError(f"condition timed out: {last}")

    def observed(self):
        return json.loads((self.output / "observer.json").read_text())

    def mark(self, name):
        self.report["checks"].append(name)
        print("ok: " + name, flush=True)

    def prepare(self):
        authority = ROOT / ".cache/s3-tls/ca.pem"
        previous_ca = authority.read_bytes() if authority.exists() else b""
        self.env_command("sh tools/env/event_flow_tls.sh && sh tools/env/s3_test_tls.sh", network=False)
        changed_ca = previous_ca != authority.read_bytes()
        self.ca = ssl.create_default_context(cafile=ROOT / ".cache/event-flow/ca.pem")
        self.compose("up", "-d", "--wait", "--wait-timeout", "180", "postgres", "kafka-1", "registry", "keycloak", "s3", "valkey", "otel-collector")
        if changed_ca:
            self.compose("up", "-d", "--force-recreate", "--wait", "s3")
        def registry_ready():
            with urllib.request.urlopen("http://localhost:" + self.port("registry", 8080) + "/apis/ccompat/v7/subjects", timeout=5) as response:
                return response.status == 200
        self.wait(registry_ready, 120)
        self.compose("up", "-d", "--wait", "toxiproxy")
        def bucket_ready():
            try:
                existing = self.compose("exec", "-T", "s3", "sh", "-c", "printf '%s\\n' 's3.bucket.list' | weed shell -filer=s3:8888")
                if "psim-plans" not in existing:
                    self.compose("exec", "-T", "s3", "sh", "-c", "printf '%s\\n' 's3.bucket.create -name psim-plans' | weed shell -filer=s3:8888")
                return True
            except RuntimeError:
                return False
        self.wait(bucket_ready, 90)
        self.run([sys.executable, "-I", "tools/env/envctl.py", "topics", self.project, "single"])
        self.run([TASK, "contracts:registry:register", "PROJECT=" + self.project])
        self.env_command("python3 -I tools/env/keycloak_security.py http://keycloak:8080")
        self.compose("up", "-d", "--force-recreate", "connector-gateway")
        self.env_command(f"python3 -I tools/env/gateway_bootstrap.py http://keycloak:8080 connector-gateway:8080 build/debian13/{self.preset}/tools/gateway-admin/psim-gateway-admin .cache/event-flow")
        self.principals()
        (self.output / "observer.json").unlink(missing_ok=True)
        self.run(["docker", "rm", "-f", self.observer_name]) if subprocess.run(["docker", "inspect", self.observer_name], capture_output=True).returncode == 0 else None
        log = (self.output / "observer.log").open("w")
        self.observer = subprocess.Popen(["docker", "run", "--rm", "--name", self.observer_name, "--security-opt", "seccomp=unconfined", "-u", f"{os.getuid()}:{os.getgid()}", "--network", self.project + "_default", "-v", str(ROOT) + ":/repo", "-e", f"LLVM_PROFILE_FILE=/repo/build/debian13/{self.preset}/profiles/%p-%m.profraw", "-w", "/repo", self.toolchain, f"build/debian13/{self.preset}/tools/catalog/psim-catalog-observer", str(self.output.relative_to(ROOT) / "observer.json")], cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
        self.wait(lambda: self.observed()["ready"], 60)
        (ROOT / f"build/debian13/{self.preset}/profiles").mkdir(parents=True, exist_ok=True)
        self.compose("up", "-d", "--force-recreate", "resource-catalog", "catalog-2")
        self.wait(lambda: self.request("GET", "/v1/locations"), 60)
        self.mark("HTTPS service readiness and Kafka observer")

    def location(self, kind, parent=None, identity=None):
        body = {"locationId": identity or uid(), "kind": "LOCATION_KIND_" + kind, "name": kind, "code": uid()[:16]}
        if parent:
            body["parentId"] = parent["locationId"]
        if kind == "SITE":
            body["timezone"] = "UTC"
        return self.request("POST", "/v1/locations", body)["location"]

    def basic(self):
        self.site = self.location("SITE", identity=self.site_id)
        self.location("SITE", identity=self.foreign_site)
        self.building = self.location("BUILDING", self.site)
        self.floor = self.location("FLOOR", self.building)
        self.zones = [self.location("ZONE", self.floor) for _ in range(20)]
        self.type = self.request("POST", "/v1/device-types", {"name": "Alarm sensor", "code": self.fixtures, "sourceTemplates": [{"kind": "alarm", "name": "Alarm"}], "capabilities": [{"code": "arm", "name": "Arm"}]})["deviceType"]
        self.wait(lambda: self.request("GET", "/v1/connectors")["connectors"])
        self.connector = "22222222-2222-4222-8222-222222222222"
        self.request("GET", "/v1/locations", role=None, expected=401)
        self.request("GET", "/v1/locations/" + self.site_id, role="other", expected=403)
        self.request("GET", "/v1/locations/" + self.site_id, role="tenant", expected=404)
        assert self.request("GET", "/v1/locations", role="empty")["locations"] == []
        self.request("POST", "/v1/locations", {"name": "x"}, role="operator", expected=403)
        self.request("POST", "/v1/locations", {"name": "x", "code": "x", "kind": "LOCATION_KIND_ZONE", "parentId": self.zones[0]["locationId"]}, expected=400)
        self.request("POST", "/v1/locations/" + self.site_id + ":archive", {}, version=1, expected=409)
        bad = dict(self.site, name="Changed")
        self.request("PUT", "/v1/locations/" + self.site_id, bad, expected=428)
        self.request("PUT", "/v1/locations/" + self.site_id, bad, version=99, expected=412)
        self.site = self.request("PUT", "/v1/locations/" + self.site_id, bad, version=1)["location"]
        key = uid()
        body = {"locationId": uid(), "kind": "LOCATION_KIND_BUILDING", "parentId": self.site_id, "name": "Idempotent", "code": uid()}
        assert self.request("POST", "/v1/locations", body, key=key) == self.request("POST", "/v1/locations", body, key=key)
        self.request("POST", "/v1/locations", body | {"name": "Other"}, key=key, expected=422)
        empty = self.location("BUILDING", self.site)
        archived = self.request("POST", "/v1/locations/" + empty["locationId"] + ":archive", {}, version=1)["location"]
        assert archived["state"] == "RESOURCE_STATE_ARCHIVED"
        self.request("POST", "/v1/locations", {"kind": "LOCATION_KIND_SITE", "name": "Bad timezone", "code": uid(), "timezone": "Invalid/Timezone"}, expected=400)
        self.request("POST", "/v1/device-types", {"name": "Duplicate", "code": self.fixtures}, expected=409)
        self.request("PUT", "/v1/locations/" + self.floor["locationId"], self.floor | {"siteId": self.foreign_site}, version=1, expected=409)
        self.mark("hierarchy, versions, idempotency and tenant/site authorization")

    def plans(self):
        path = "/v1/locations/" + self.floor["locationId"] + "/floor-plan"
        data = png()
        self.floor = self.request("PUT", path + "?width=100&height=100", data, version=self.floor["version"], content_type="image/png")["location"]
        assert self.request("GET", path) == data
        self.request("GET", path, role="other", expected=403)
        self.request("PUT", path + "?width=100&height=100", b"<svg/>", version=self.floor["version"], content_type="image/svg+xml", expected=400)
        self.request("PUT", path + "?width=100&height=100", b"fake PNG", version=self.floor["version"], content_type="image/png", expected=400)
        self.request("PUT", path + "?width=100&height=100", png(5000, 4000), version=self.floor["version"], content_type="image/png", expected=400)
        exact = png(padding=20 * 1024 * 1024 - len(png()) - 20)
        assert len(exact) == 20 * 1024 * 1024
        self.floor = self.request("PUT", path + "?width=100&height=100", exact, version=self.floor["version"], content_type="image/png")["location"]
        assert self.request("GET", path) == exact
        assert self.request("GET", path + "?version=1") == data
        assert len(self.request("GET", path + "/versions")["files"]) == 2
        # Oversized bodies are rejected from headers before their bytes are transmitted.
        import http.client
        connection = http.client.HTTPSConnection("localhost", int(self.port("resource-catalog", 8443)), context=self.ca, timeout=10)
        connection.putrequest("PUT", path + "?width=100&height=100")
        connection.putheader("Authorization", "Bearer " + self.token("admin"))
        connection.putheader("Content-Type", "image/png")
        connection.putheader("If-Match", '"' + self.floor["version"] + '"')
        connection.putheader("Content-Length", str(len(exact) + 1))
        connection.endheaders()
        oversized = connection.getresponse()
        assert oversized.status == 400
        oversized.read()
        connection.close()
        self.compose("stop", "-t", "2", "s3")
        try:
            self.request("PUT", path + "?width=100&height=100", data, version=self.floor["version"], content_type="image/png", expected=503)
        finally:
            self.compose("start", "--wait", "s3")
        self.wait(lambda: self.request("GET", path) == exact, 60)
        self.sql("CREATE FUNCTION catalog.reject_image() RETURNS trigger LANGUAGE plpgsql AS $$ BEGIN IF NEW.linked AND NOT OLD.linked THEN RAISE EXCEPTION 'image commit fault'; END IF; RETURN NEW; END $$; CREATE TRIGGER reject_image BEFORE UPDATE ON catalog.files FOR EACH ROW EXECUTE FUNCTION catalog.reject_image();")
        try:
            self.request("PUT", path + "?width=100&height=100", data, version=self.floor["version"], content_type="image/png", expected=400)
        finally:
            self.sql("DROP TRIGGER reject_image ON catalog.files; DROP FUNCTION catalog.reject_image();")
        assert self.request("GET", path) == exact
        orphans = self.sql("SELECT key FROM catalog.files WHERE NOT linked").splitlines()
        self.sql("UPDATE catalog.files SET expires=clock_timestamp() WHERE NOT linked")
        self.wait(lambda: self.sql("SELECT count(*) FROM catalog.files WHERE NOT linked") == "0", 40)
        for key in orphans:
            assert self.s3_request("HEAD", key, True) == 404
        assert self.s3_request("GET", self.floor["floorPlan"]["file"]["objectKey"]) in (401, 403)
        self.mark("PNG validation, exact 20 MiB, scoped versions, failed S3/metadata writes and orphan cleanup")

    def device(self, index):
        return {"deviceId": uid(), "deviceTypeId": self.type["deviceTypeId"], "zoneId": self.zones[index % len(self.zones)]["locationId"], "siteId": self.site_id, "connectorId": self.connector, "externalId": self.fixtures + ":" + str(index), "name": "Sensor " + str(index), "sources": [{"externalRef": self.fixtures + ":alarm:" + str(index), "kind": "alarm", "name": "Alarm"}], "position": {"x": index % 100, "y": index % 100}}

    def import_job(self, rows, dry=False, key=None):
        started = time.monotonic()
        response = self.request("POST", "/v1/imports?format=json&dryRun=" + str(dry).lower(), rows, key=key)["importJob"]
        path = "/v1/imports/" + response["importId"]
        if len(rows) >= 50000 and not dry:
            self.compose("kill", "-s", "SIGKILL", "resource-catalog")
            self.wait(lambda: self.request("GET", path, service="catalog-2")["importJob"])
            self.compose("start", "resource-catalog")
            self.wait(lambda: self.request("GET", path)["importJob"])
        report = self.wait(lambda: (job if (job := self.request("GET", path)["importJob"])["state"] != "IMPORT_STATE_RUNNING" else None), 300)
        return report, time.monotonic() - started

    def imports(self):
        dry, _ = self.import_job([self.device(-1)], True)
        assert dry["rowsCreated"] == 1 and dry["rowsFailed"] == 0
        assert self.request("GET", "/v1/devices?locationId=" + self.site_id)["devices"] == []
        rows = [self.device(index) for index in range(self.count)]
        key = uid()
        job, elapsed = self.import_job(rows, key=key)
        assert job["rowsCreated"] == self.count and job["rowsFailed"] == 0, job
        assert elapsed <= 300, elapsed
        first = self.request("POST", "/v1/imports?format=json&dryRun=false", rows, key=key)["importJob"]
        assert first["importId"] == job["importId"]
        self.report["importSeconds"] = elapsed
        self.wait(lambda: self.observed()["deviceCount"] == self.count, 30)
        digest = hashlib.sha256("".join(row["deviceId"] + "\n" for row in sorted(rows, key=lambda row: row["deviceId"])).encode()).hexdigest()
        assert self.observed()["deviceDigest"] == digest
        # Exercise update-by-external-id with server identities omitted.
        repeated = [{k: v for k, v in row.items() if k != "deviceId"} | {"name": row["name"] + " updated"} for row in rows]
        again, second = self.import_job(repeated)
        assert again["rowsUpdated"] == self.count and again["rowsCreated"] == 0 and again["rowsFailed"] == 0, again
        self.report["repeatImportSeconds"] = second
        invalid = [self.device(self.count + 1), self.device(self.count + 2), self.device(self.count + 3)]
        invalid[0]["zoneId"] = uid()
        invalid[1]["sources"][0]["externalRef"] = rows[0]["sources"][0]["externalRef"]
        invalid.append(invalid[2])
        errors, _ = self.import_job(invalid)
        assert errors["rowsCreated"] == 1 and errors["rowsFailed"] == 3, errors
        self.extra_device = invalid[2]["deviceId"]
        exported = self.request("GET", "/v1/devices:export?format=json&locationId=" + self.site_id)
        assert {row["deviceId"] for row in exported} == {row["deviceId"] for row in rows} | {self.extra_device}
        self.request("PUT", "/v1/devices/" + rows[0]["deviceId"], rows[0] | {"position": {"x": 101, "y": 0}}, version=2, expected=400)
        self.request("PUT", "/v1/device-types/" + self.type["deviceTypeId"], self.type | {"sourceTemplates": []}, version=1, expected=409)
        self.request("POST", "/v1/locations/" + self.zones[0]["locationId"] + ":archive", {}, version=1, expected=409)
        self.rows = rows
        self.mark(f"import {self.count} devices in {elapsed:.3f}s, consumer identity reconciliation, repeat import and row errors")

    def maintenance(self):
        start, end = utc(0.4), utc(2.0)
        body = {"zoneId": self.zones[0]["locationId"], "startsAt": start, "endsAt": end, "reason": "Inspection", "responsible": "admin", "suppression": {"eventTypes": ["access.denied"]}}
        self.request("POST", "/v1/maintenance-windows", body | {"suppression": {"eventTypes": ["fire.alarm"]}}, expected=400)
        window = self.request("POST", "/v1/maintenance-windows", body)["maintenanceWindow"]
        path = "/v1/maintenance-windows/" + window["maintenanceId"]
        self.wait(lambda: self.request("GET", path)["maintenanceWindow"]["state"] == "MAINTENANCE_STATE_COMPLETED", 5)
        self.wait(lambda: any(item["state"] == "MAINTENANCE_STATE_COMPLETED" for item in self.observed()["maintenance"].get(window["maintenanceId"], [])), 5)
        ended_ms = dt.datetime.fromisoformat(end.replace("Z", "+00:00")).timestamp() * 1000
        observed = next(item for item in self.observed()["maintenance"][window["maintenanceId"]] if item["state"] == "MAINTENANCE_STATE_COMPLETED")
        drift = observed["observedMs"] - ended_ms
        assert drift <= 1000, drift
        self.report["maintenanceCompletionMs"] = drift
        # Stop both instances before the end, then restore durable transitions on startup.
        restart = self.request("POST", "/v1/maintenance-windows", body | {"deviceId": self.rows[0]["deviceId"], "zoneId": None, "startsAt": utc(-1), "endsAt": utc(5)})["maintenanceWindow"]
        self.wait(lambda: restart["maintenanceId"] in self.observed()["maintenance"])
        self.wait(lambda: self.sql("SELECT count(*) FROM catalog.psim_outbox WHERE published_at IS NULL") == "0")
        self.compose("stop", "-t", "30", "resource-catalog", "catalog-2")
        time.sleep(5)
        began = time.monotonic()
        self.compose("start", "resource-catalog", "catalog-2")
        restored = self.wait(lambda: self.request("GET", "/v1/maintenance-windows/" + restart["maintenanceId"])["maintenanceWindow"])
        assert restored["state"] == "MAINTENANCE_STATE_COMPLETED", restored
        self.report["restartReadinessSeconds"] = time.monotonic() - began
        cancel = self.request("POST", "/v1/maintenance-windows", body | {"startsAt": utc(5), "endsAt": utc(10)})["maintenanceWindow"]
        cancel = self.request("POST", "/v1/maintenance-windows/" + cancel["maintenanceId"] + ":cancel", {}, version=cancel["version"])["maintenanceWindow"]
        assert cancel["state"] == "MAINTENANCE_STATE_CANCELLED"
        self.request("GET", "/v1/maintenance-windows/" + cancel["maintenanceId"], role="other", expected=403)
        active = self.request("POST", "/v1/maintenance-windows", body | {"startsAt": utc(-1), "endsAt": utc(30)})["maintenanceWindow"]
        updated = self.request("PUT", "/v1/maintenance-windows/" + active["maintenanceId"], active | {"startsAt": utc(-60), "endsAt": utc(60), "reason": "Extended inspection"}, version=active["version"])["maintenanceWindow"]
        assert updated["startsAt"] == active["startsAt"] and updated["state"] == "MAINTENANCE_STATE_ACTIVE"
        self.request("PUT", "/v1/maintenance-windows/" + active["maintenanceId"], updated | {"zoneId": self.zones[1]["locationId"]}, version=updated["version"], expected=400)
        self.request("POST", "/v1/maintenance-windows/" + updated["maintenanceId"] + ":cancel", {}, version=updated["version"])
        assert self.request("GET", "/v1/maintenance-windows?siteId=" + self.site_id)["maintenanceWindows"]
        self.mark("maintenance receipt-time boundaries, automatic audit and recovery after both instances restart")

    def administration(self):
        connector_id = uid()
        def certificate(slot):
            directory = self.output / ("certificate-" + self.fixtures + "-" + slot)
            directory.mkdir(exist_ok=True)
            key, csr, pem, ext = (directory / name for name in ("key.pem", "request.pem", "certificate.pem", "extensions"))
            ext.write_text("basicConstraints=critical,CA:FALSE\nkeyUsage=critical,digitalSignature\nextendedKeyUsage=clientAuth\nsubjectAltName=URI:psim://connector/" + connector_id + "\n")
            self.run(["openssl", "req", "-new", "-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:P-256", "-noenc", "-subj", "/CN=psim-catalog-probe", "-keyout", str(key), "-out", str(csr)])
            key.chmod(0o400)
            self.run(["openssl", "x509", "-req", "-days", "1", "-in", str(csr), "-CA", str(ROOT / ".cache/event-flow/ca.pem"), "-CAkey", str(ROOT / ".cache/event-flow/ca.key"), "-CAcreateserial", "-extfile", str(ext), "-out", str(pem)])
            fingerprint = self.run(["openssl", "x509", "-in", str(pem), "-fingerprint", "-sha256", "-noout"]).strip().split("=")[1].replace(":", "").lower()
            return pem.read_text(), fingerprint
        first, original = certificate("first")
        path = "/v1/connectors?" + urllib.parse.urlencode({"certificatePem": first})
        body = {"connectorId": connector_id, "name": "Catalog HTTPS connector", "connectorType": "webhook"}
        request_id = uid()
        created = self.request("POST", path, body, key=request_id)["connector"]
        assert self.request("POST", path, body, key=request_id)["connector"] == created
        self.request("POST", path, body, role="site", expected=403)
        second, fingerprint = certificate("second")
        until = utc(60)
        rotated = self.request("POST", "/v1/connectors/" + connector_id + ":rotate-certificate", {"newCertificate": {"sha256": fingerprint}, "certificatePem": second, "previousNotAfter": until}, version=1)["connector"]
        assert len(rotated["certificates"]) == 2
        assert next(item for item in rotated["certificates"] if item["sha256"] == original)["notAfter"] == until
        revoked = self.request("POST", "/v1/connectors/" + connector_id + ":revoke", {"reason": "Retired certificate", "fingerprint": original}, version=2)["connector"]
        assert len(revoked["certificates"]) == 1
        disabled = self.request("POST", "/v1/connectors/" + connector_id + ":revoke", {"reason": "Integration check complete"}, version=3)["connector"]
        assert disabled["state"] == "RESOURCE_STATE_DISABLED"
        self.wait(lambda: self.request("GET", "/v1/connectors/" + connector_id)["connector"]["version"] == "4")
        self.mark("REST connector registration, exact retry, certificate rotation and enforced revocation")

    def variants(self):
        device = self.request("GET", "/v1/devices/" + self.rows[0]["deviceId"])["device"]
        device = self.request("PUT", "/v1/devices/" + device["deviceId"], device | {"state": "RESOURCE_STATE_DISABLED"}, version=device["version"])["device"]
        assert device["state"] == "RESOURCE_STATE_DISABLED"
        assert len(self.request("GET", "/v1/devices?state=RESOURCE_STATE_DISABLED&locationId=" + self.site_id)["devices"]) == 1
        extra = self.device(self.count + 100)
        extra = self.request("POST", "/v1/devices", extra)["device"]
        self.request("POST", "/v1/devices", extra | {"deviceId": uid()}, expected=409)
        self.request("POST", "/v1/devices", extra | {"deviceId": uid(), "externalId": "another"}, expected=409)
        floor_path = "/v1/locations/" + self.floor["locationId"] + "/floor-plan"
        self.request("PUT", floor_path + "?width=1&height=1", png(), version=self.floor["version"], content_type="image/png", expected=400)
        self.request("GET", floor_path + "?version=9999", expected=404)
        self.request("GET", floor_path + "/versions?pageSize=1&pageToken=bad", expected=400)
        self.request("GET", "/v1/devices?pageSize=1001", expected=400)
        self.request("GET", "/v1/unknown", expected=404)
        self.request("GET", "/v1/devices?unsupported=x", expected=400)
        import csv
        import io
        exported = self.request("GET", "/v1/devices:export?format=csv&locationId=" + self.site_id).decode()
        parsed = list(csv.DictReader(io.StringIO(exported)))
        assert len(parsed) == self.count + 2
        csv_bytes = io.StringIO()
        writer = csv.DictWriter(csv_bytes, fieldnames=list(parsed[0]))
        writer.writeheader()
        writer.writerow(parsed[0])
        bad = dict(parsed[0], externalId="invalid structured source", sources="[bad]")
        writer.writerow(bad)
        job = self.request("POST", "/v1/imports?format=csv&dryRun=true", csv_bytes.getvalue().encode(), content_type="text/csv")["importJob"]
        job = self.wait(lambda: (current if (current := self.request("GET", "/v1/imports/" + job["importId"])["importJob"])["state"] == "IMPORT_STATE_COMPLETED" else None))
        assert job["rowsUpdated"] == 1 and job["rowsFailed"] == 1, job
        self.mark("device mutations, CSV round trip, per-row CSV errors and request bounds")

    def finish(self):
        self.wait(lambda: self.sql("SELECT count(*) FROM catalog.psim_outbox WHERE published_at IS NULL") == "0", 30)
        self.wait(lambda: self.observed()["audit"].get("catalog.maintenance.complete", 0) >= 2, 15)
        observed = self.observed()
        assert observed["maxDelayMs"] <= 2000, observed["maxDelayMs"]
        self.report["maxConsumerDelayMs"] = observed["maxDelayMs"]
        assert observed["audit"].get("access.denied", 0) >= 3, observed["audit"]
        assert time.time() * 1000 - observed["writtenMs"] < 2000
        assert self.observer.poll() is None, (self.output / "observer.log").read_text()[-2000:]
        self.report["passed"] = True
        self.mark("durable outbox drained, audit received, all changes propagated within 2 seconds")

    def cleanup(self):
        if self.observer:
            subprocess.run(["docker", "stop", "-t", "15", self.observer_name], stdout=self.log, stderr=self.log)
            self.observer.wait(timeout=20)
            if self.report["passed"]:
                assert self.observer.returncode == 0, (self.output / "observer.log").read_text()[-2000:]
        self.compose("stop", "-t", "30", "resource-catalog", "catalog-2", "connector-gateway")
        logs = self.compose("logs", "--no-color", "resource-catalog", "catalog-2", "connector-gateway")
        (self.output / "servers.log").write_text(logs)
        if self.report["passed"]:
            import re
            assert not re.search(r"AddressSanitizer|LeakSanitizer|ThreadSanitizer|runtime error:", logs), "sanitizer findings in service logs"
            for service in ("resource-catalog", "catalog-2", "connector-gateway"):
                identity = self.compose("ps", "-aq", service).strip()
                if identity:
                    state = json.loads(self.run(["docker", "inspect", identity]))[0]["State"]
                    assert state["ExitCode"] == 0, f"{service} did not drain: {state['ExitCode']}"
        if self.owned and os.environ.get("PSIM_CATALOG_KEEP_ENV") != "1":
            self.compose("--profile", "catalog-test", "--profile", "event-flow", "--profile", "gateway-test", "down", "-v", "--remove-orphans", "--timeout", "10")
        else:
            self.compose("stop", "-t", "30", "resource-catalog", "catalog-2", "connector-gateway")
        self.log.close()


def main():
    project, preset, count = sys.argv[1:]
    check = Check(project, preset, int(count))
    try:
        check.prepare()
        check.basic()
        check.plans()
        check.imports()
        check.maintenance()
        check.variants()
        check.administration()
        check.finish()
    finally:
        (check.output / "result.json").write_text(json.dumps(check.report, indent=2) + "\n")
        check.cleanup()
    print(json.dumps(check.report, indent=2))


if __name__ == "__main__":
    main()
