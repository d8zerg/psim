# Connectors

<!-- language-links -->
[English](README.md) | [Русский](../docs/ru/repository/connectors/README.md)
<!-- /language-links -->

- [SDK and Integrator Guide](../docs/engineering/connector-sdk.md): the public target `psim::platform_connector`, example, and conformance suite.
- [Reference Connectors](../docs/engineering/reference-connectors.md): `simulator/` - virtual devices, load, commands, and snapshots; `webhook/` - HTTPS event reception with durable retry handling.

```bash
task connectors:conformance
task connectors:conformance OUTAGE_SECONDS=1
task connectors:bench
task connectors:coverage
```

The direct gRPC client for end-to-end verification is located in `tools/event-flow/psim-flow-simulator`; `libs/sim` is a separate manually run simulation environment.
