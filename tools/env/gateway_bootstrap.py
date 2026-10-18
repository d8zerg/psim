"""Provision the isolated development management principal and connector registration."""
import hashlib
import json
import os
from pathlib import Path
import runpy
import subprocess
import sys
import uuid


def token(base, directory):
    request = runpy.run_path("tools/env/keycloak_security.py")["request"]
    admin = request(base, "/realms/master/protocol/openid-connect/token", data={
        "grant_type": "password", "client_id": "admin-cli", "username": "admin",
        "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
    clients = "/admin/realms/psim/clients"
    client_id = "psim-gateway-bootstrap"
    found = request(base, clients + "?clientId=" + client_id, admin)
    if not found:
        request(base, clients, admin, {
            "clientId": client_id, "enabled": True, "publicClient": False,
            "serviceAccountsEnabled": True, "standardFlowEnabled": False, "directAccessGrantsEnabled": False,
            "secret": "psim-dev-gateway-bootstrap", "protocol": "openid-connect",
            "protocolMappers": [
                {"name": "audience", "protocol": "openid-connect", "protocolMapper": "oidc-audience-mapper",
                 "config": {"included.client.audience": "psim", "access.token.claim": "true"}},
                {"name": "tenant", "protocol": "openid-connect", "protocolMapper": "oidc-hardcoded-claim-mapper",
                 "config": {"claim.name": "psim_tenant", "claim.value": "11111111-1111-4111-8111-111111111111",
                            "jsonType.label": "String", "access.token.claim": "true"}},
            ]})
        found = request(base, clients + "?clientId=" + client_id, admin)
    account = request(base, clients + "/" + found[0]["id"] + "/service-account-user", admin)
    resource = request(base, clients + "?clientId=psim", admin)[0]
    role = request(base, clients + "/" + resource["id"] + "/roles/integrator", admin)
    request(base, "/admin/realms/psim/users/" + account["id"] + "/role-mappings/clients/" + resource["id"], admin, [role])
    access = request(base, "/realms/psim/protocol/openid-connect/token", data={
        "grant_type": "client_credentials", "client_id": client_id, "client_secret": "psim-dev-gateway-bootstrap"}, form=True)["access_token"]
    path = directory / "operator.token"
    if path.exists():
        path.chmod(0o600)
    path.write_text(access)
    path.chmod(0o400)
    technical = request(base, "/realms/psim/protocol/openid-connect/token", data={
        "grant_type": "client_credentials", "client_id": "psim-services", "client_secret": "psim-dev-services-secret"}, form=True)["access_token"]
    path = directory / "technical.token"
    if path.exists():
        path.chmod(0o600)
    path.write_text(technical)
    path.chmod(0o400)
    return directory / "operator.token"


def manage(target, binary, directory, token_file, body, required=True):
    path = directory / "management-request.json"
    body = {"requestId": str(uuid.uuid4()), **body}
    path.write_text(json.dumps(body))
    result = subprocess.run([str(binary), target, str(directory), str(path), str(token_file)], text=True, capture_output=True, timeout=35)
    if result.returncode:
        if required:
            raise RuntimeError(result.stderr.strip())
        return None
    return json.loads(result.stdout)


def bootstrap(base, target, binary, directory):
    directory = Path(directory)
    operator = token(base, directory)
    identity = "22222222-2222-4222-8222-222222222222"
    existing = manage(target, binary, directory, operator, {"action": "REGISTRATION_ACTION_INSPECT", "connector": {"connectorId": identity}}, False)
    fingerprint = subprocess.check_output(["openssl", "x509", "-in", str(directory / "client.pem"), "-outform", "DER"])
    digest = hashlib.sha256(fingerprint).hexdigest()
    if existing and any(item["sha256"] == digest for item in existing["connector"].get("certificates", [])):
        print("gateway: development connector registration already current")
        return
    if existing:
        for cert in existing["connector"].get("certificates", []):
            existing = manage(target, binary, directory, operator, {"action": "REGISTRATION_ACTION_REVOKE", "expectedVersion": existing["connector"]["version"], "connector": {"connectorId": identity}, "fingerprint": cert["sha256"]})
    body = {"action": "REGISTRATION_ACTION_ROTATE" if existing else "REGISTRATION_ACTION_REGISTER", "connector": {"connectorId": identity, "name": "Development simulator", "connectorType": "simulator"}, "certificatePem": (directory / "client.pem").read_text()}
    if existing:
        body["expectedVersion"] = existing["connector"]["version"]
    manage(target, binary, directory, operator, body)
    print("gateway: authenticated development connector registered")


if __name__ == "__main__":
    bootstrap(*sys.argv[1:])
