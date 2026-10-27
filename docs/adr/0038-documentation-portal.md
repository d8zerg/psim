# ADR-038. Documentation portal built from the repository

<!-- language-links -->
[English](0038-documentation-portal.md) | [Русский](../ru/adr/0038-documentation-portal.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related:** ADR-026, ADR-030, ADR-034, method FF-09 ([verification-matrix.md](../quality/verification-matrix.md))

## Context

One repository-built portal contains architecture, ADRs, C4 models, OpenAPI, AsyncAPI and Protobuf references, and manuals, published through CI. Generated references prevent contract changes from leaving reference documentation stale.

Sources are Markdown under `docs/` with relative links and GitHub-style anchors checked by `docs:links`; UML and C4 use PlantUML. `task contracts:check` generates and verifies OpenAPI, AsyncAPI and the Markdown Protobuf reference, alongside topic and error registries. The portal is part of the distribution and must work without internet access. Pipelines are local; Git hosting stores code only (ADR-034).

## Options considered

### Generator

| Option | Advantages | Disadvantages |
|---|---|---|
| Docusaurus | React and MDX | MDX breaks ordinary Markdown containing `<` and `{}`; heavy build |
| VitePress | Fast; Node is already available | Markdown becomes Vue templates; Task and Go-template `{{ }}` require escaping |
| Zensical | Material successor; reads `mkdocs.yml` | Version 0.0.x |
| **MkDocs 1.6 + Material 9.7** | Ordinary Markdown; strict link and anchor checks; EN/RU search; mature | MkDocs 1.x is no longer developed; Material is maintained; Zensical offers a migration path using the same `mkdocs.yml` |

### References

| Reference | Source | Generation |
|---|---|---|
| REST | `contracts/openapi/psim-api-v1.yaml` | Local OpenAPI JSON and Redoc HTML; the portal image bundles the Redoc script and logo |
| Kafka topics | `contracts/asyncapi/psim-topics.yaml` | AsyncAPI generates topic, Protobuf-linked message, key, partitions, retention, semantics, producers and consumers |
| Protobuf | `contracts/docs/proto-reference.md` | Generated and verified by `contracts:check` |
| Error codes | `contracts/errors/errors.yaml` | Generated from the catalog |
| C4 | `docs/architecture/c4/L2-Containers.puml` | Seven L1, L2, L3 and deployment views stored as PlantUML with the local C4 standard library |

### Diagrams

A digest-pinned, network-disabled image renders PlantUML to white-background SVG during the build. The portal loads no renderer, works offline and keeps diagrams readable in light and dark themes. Clicking an image opens it at full size.

## Decision

1. **`task docs:portal`** builds `build/portal/site`: copy `docs/` and repository READMEs while preserving layout and relative links, generate references, create Redoc REST documentation, render PlantUML and run **strict** `mkdocs build`. Broken links or anchors fail the build. Links to files outside the portal, such as proto, YAML and code, point to the repository at the build commit. Navigation derives from `docs/README.md` sections, ADR registry, references and maintained guides, avoiding a second manually maintained contents list.
2. **No external resources:** no downloaded fonts or Material `offline` plugin, which would load a CDN script. Redoc's script and logo are local SHA-256-pinned files. The repository link does not fetch GitHub statistics. Redoc renders a local contract in the browser; it does not hydrate output from another React instance.
3. **Portal image:** `tools/docs/portal/` contains Python 3.13, MkDocs 1.6.1, Material 9.7.7, pymdown-extensions and SHA-256-verified Redoc. It builds locally with a tag derived from the Dockerfile and requirements hash.
4. **Publication:** every release includes `psim-platform-docs_<version>.tar` with an SBOM for MIT-licensed Material and Redoc in signed `dist/` (ADR-034). Locally, `task docs:serve` serves `http://localhost:8000`. No separate hosting is used.
5. **Self-contained documentation:** a separate registry holds permanent quality goals; security requirements link to components rather than development sequence. Maintained documents contain release criteria and limitations. Development-only materials are excluded from the portal; `docs:release:check` verifies the repository copy and portal without them.
6. **Gate:** `docs:portal` is part of `docs:check` (FF-09); documentation or contract changes activate the pipeline's `docs` stage.

7. **Languages:** English is the default; Russian has complete paired pages, diagrams, references and operational instructions. The registry `docs/localization.yaml` preserves page identities and reviewed source hashes. Terminology is shared through the [glossary](../domain/glossary.md). Language navigation retains the corresponding page and section; search stays within the selected language. The [contribution workflow](../../CONTRIBUTING.md) defines paired review and regeneration.

## Consequences

- Contract changes appear in the next portal build. `contracts:check` catches drift between proto and generated OpenAPI, AsyncAPI and Protobuf references; strict mode catches broken links and anchors.
- Portal generation includes UML/C4 rendering, reference generation, Redoc and strict MkDocs validation. Duration depends on page and diagram counts.
- Migrate to Zensical after it leaves 0.x; the configuration is compatible.

## Verification

Run `task docs:portal` and `task docs:check`; a negative broken-link test must fail strict mode. `task release:dist` and `task release:verify` verify the signed portal archive and its SBOM.
