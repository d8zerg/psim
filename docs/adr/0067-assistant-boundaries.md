# ADR-067. Assistant with sources and Without Command Execution

<!-- language-links -->
[English](0067-assistant-boundaries.md) | [Русский](../ru/adr/0067-assistant-boundaries.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-063](0063-interface-principles.md), [ADR-061](0061-live-access-validation.md)

## Solution

The assistant explains values, prepares summaries, and suggests the next step based on authorized sources. Each conclusion identifies the source, time of receipt, version, and assumption. Incomplete data and outdated sources are visible to the operator. Instructions within documents and events are considered data.

The assistant has no tools for executing commands, confirming changes, changing permissions, or assigning responsibility. The proposal may open up a regular application form; filling in reasons and confirmation is done by a person, while the server rechecks permissions and versions. The context is limited to the current user, tenant, and access scope; changing the context removes history and cache.

Working without an external network is supported by a local model provider. The provider is set by the administrator; user text does not select the server address. If the model fails, original documents and normal operations remain. The model does not receive bearer tokens, connection secrets, or data outside the access scope.

Audits record user and request identifiers, selected provider, model and source versions, responses, and open actions by users according to storage policies. Access failures and source errors are preserved without content leakage. The showcase uses deterministic responses and clearly shows the demonstration mode.

## Verification

There is no path from “model response -> command execution”. Missing sources, outdated versions, malicious instructions in sources, changed access scopes, unavailable models, and explicit markings of demonstration responses are checked. The real provider and audits are connected to server-side support.
