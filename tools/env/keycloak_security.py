"""Reconcile the resource audience of the isolated development realm (SR-03)."""
import json
import sys
import urllib.parse
import urllib.request


def request(base, path, token=None, data=None, form=False):
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    if data is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json"
        data = urllib.parse.urlencode(data).encode() if form else json.dumps(data).encode()
    with urllib.request.urlopen(urllib.request.Request(base + path, data=data, headers=headers), timeout=10) as response:
        body = response.read(1024 * 1024)
        return json.loads(body) if body else None


def main(base):
    auth = request(base, "/realms/master/protocol/openid-connect/token", data={
        "grant_type": "password", "client_id": "admin-cli", "username": "admin",
        "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
    for client_id in ("psim-console", "psim-services"):
        client = request(base, "/admin/realms/psim/clients?clientId=" + client_id, auth)[0]
        path = "/admin/realms/psim/clients/" + client["id"] + "/protocol-mappers/models"
        mappers = request(base, path, auth)
        desired = {"name": "psim-audience", "protocol": "openid-connect",
            "protocolMapper": "oidc-audience-mapper", "consentRequired": False,
            "config": {"included.client.audience": "psim", "id.token.claim": "false", "access.token.claim": "true"}}
        current = next((item for item in mappers if item["name"] == "psim-audience"), None)
        if current is None:
            request(base, path, auth, desired)
        elif any(current.get(key) != value for key, value in desired.items()):
            payload = json.dumps(desired | {"id": current["id"]}).encode()
            update = urllib.request.Request(base + path + "/" + current["id"], data=payload, method="PUT",
                headers={"Authorization": "Bearer " + auth, "Content-Type": "application/json"})
            with urllib.request.urlopen(update, timeout=10):
                pass
    print("Keycloak: PSIM access-token audience configured")


if __name__ == "__main__":
    main(sys.argv[1])
