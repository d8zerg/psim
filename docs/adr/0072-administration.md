# ADR-072. Administration Module

<!-- language-links -->
[English](0072-administration.md) | [Русский](../ru/adr/0072-administration.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-020](0020-authorization-model.md), [ADR-051](0051-resource-catalog.md), [ADR-058](0058-api-gateway-and-configuration.md), [ADR-061](0061-live-access-validation.md), [ADR-069](0069-application-shell-and-session-recovery.md)

## Solution

The `/admin` and `/integrations` routes lazily load administrative forms. They use the OIDC session of the shell and a public REST API Gateway. The integrator receives control over connectors and mappings; the administrator also gets a catalog, import, maintenance, floor plans, incident types, SLA, rules, scenarios, and settings.

The nested form structure is generated from OpenAPI along with REST types. In the interface, individual fields, lists, dictionaries, and rule or step variations are edited; manual JSON editing is not required. The oneof variations are mutually exclusive. Identifiers are created by the browser; server versions and metadata are not editable. The server remains the owner of semantic validation, access scope, and auditing.

The edited copy retains the original read version. Background checking does not replace entered fields. Conflicts require explicit rereading; changes do not automatically repeat. An unknown outcome allows only an exact repetition of the request with the previous Idempotency-Key. New content gets a new key. Failure of current authorization hides previously read information. Context change destroys requests and edited copies. Exercises, video walls, and network loss prohibit changes.

The rule is saved as a draft, then checked by the server and tested on the selected range of history. Activation retains other active rules and uses the version of the entire set. The scenario is checked before publication; publication refers to the saved version. Changing the form destroys the test result.

Files are transmitted binary through the Gateway. Import is first checked without writing; application applies to the same selected file. Floor plans accept PNG/JPEG and sizes, using the floor version; historical versions are read through the authorized API. Blob URLs are released when replaced and closed. Certificates come from an external PKI; the interface does not accept private keys.

Roles and scopes belong to Keycloak. The panel shows current effective rights from `/v1/me` and opens a management console for assigning client roles and scope attributes to the selected realm. This requires a separate administrative session in Keycloak. The application does not receive control tokens from IdP and does not expand the site administrator’s rights. Rereading rights and server introspection apply changes to active sessions.

## Alternatives

- Any JSON editor: Does not meet form scenario requirements and complicates setup without developers.
- Separately described nested models: Duplicate the public contract and increase the risk of discrepancies.
- Own user and role storage: Creates a second identity owner and conflicts with ADR-020.

## Verification

Forms, versions, and exact repetition are checked. Writing without connection is prohibited, rights are revoked, ru/en, keyboard, accessibility, import, and file rejection are checked. Browser acceptance with real services includes connecting connectors, normalization mappings, hierarchy, and import, rule with test run, and scenario publication.
