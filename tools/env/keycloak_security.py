"""Reconcile the resource audience of the isolated development realm (SR-03)."""
import json
from pathlib import Path
import sys
import urllib.parse
import urllib.request


def request(base, path, token=None, data=None, form=False, method=None):
    headers = {}
    if token:
        headers["Authorization"] = "Bearer " + token
    if data is not None:
        headers["Content-Type"] = "application/x-www-form-urlencoded" if form else "application/json"
        data = urllib.parse.urlencode(data).encode() if form else json.dumps(data).encode()
    with urllib.request.urlopen(urllib.request.Request(base + path, data=data, headers=headers, method=method), timeout=10) as response:
        body = response.read(1024 * 1024)
        return json.loads(body) if body else None


def main(base, issuer=None):
    auth = request(base, "/realms/master/protocol/openid-connect/token", data={
        "grant_type": "password", "client_id": "admin-cli", "username": "admin",
        "password": "psim-dev-keycloak-admin"}, form=True)["access_token"]
    realm = request(base, "/admin/realms/psim", auth)
    realm["revokeRefreshToken"] = True
    realm["refreshTokenMaxReuse"] = 0
    realm["attributes"] = realm.get("attributes", {}) | {"frontendUrl": (issuer or base).split("/realms/")[0]}
    request(base, "/admin/realms/psim", auth, realm, method="PUT")
    desired = json.loads(Path("deploy/local/config/keycloak/psim-realm.json").read_text())
    for model in desired["clients"]:
        client_id = model["clientId"]
        client = request(base, "/admin/realms/psim/clients?clientId=" + client_id, auth)[0]
        if client_id == "psim":
            client.update({key: model[key] for key in ("bearerOnly", "secret", "serviceAccountsEnabled", "standardFlowEnabled", "directAccessGrantsEnabled")})
            request(base, "/admin/realms/psim/clients/" + client["id"], auth, client, method="PUT")
            continue
        path = "/admin/realms/psim/clients/" + client["id"] + "/protocol-mappers/models"
        mappers = request(base, path, auth)
        for obsolete in mappers:
            if obsolete["name"] in ("psim-fixture-tenant", "psim-fixture-scopes"):
                request(base, path + "/" + obsolete["id"], auth, method="DELETE")
        for mapper in model["protocolMappers"]:
            current = next((item for item in mappers if item["name"] == mapper["name"]), None)
            if current is None:
                request(base, path, auth, mapper)
            elif any(current.get(key) != value for key, value in mapper.items()):
                request(base, path + "/" + current["id"], auth, mapper | {"id": current["id"]}, method="PUT")
        if client_id == "psim-console":
            client["redirectUris"] = sorted(set(client.get("redirectUris", [])) | set(model["redirectUris"]))
            client["webOrigins"] = sorted(set(client.get("webOrigins", [])) | set(model["webOrigins"]))
            request(base, "/admin/realms/psim/clients/" + client["id"], auth,
                    {key: client[key] for key in ("redirectUris", "webOrigins")}, method="PUT")
    profile = request(base, "/admin/realms/psim/users/profile", auth)
    managed = {"psim_tenant", "psim_scopes"}
    profile["attributes"] = [item for item in profile["attributes"] if item["name"] not in managed] + [
        {"name": name, "multivalued": name == "psim_scopes", "permissions": {"view": ["admin"], "edit": ["admin"]}}
        for name in sorted(managed)]
    request(base, "/admin/realms/psim/users/profile", auth, profile, method="PUT")
    for fixture in desired["users"]:
        user = request(base, "/admin/realms/psim/users?exact=true&username=" + fixture["username"], auth)[0]
        attrs = user.get("attributes", {})
        user["attributes"] = fixture["attributes"] | attrs
        request(base, "/admin/realms/psim/users/" + user["id"], auth, user, method="PUT")
    print("Keycloak: confidential verification, audience, protected scope attributes and reference claims configured")


if __name__ == "__main__":
    main(*sys.argv[1:])
