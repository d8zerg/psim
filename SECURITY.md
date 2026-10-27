# Reporting security issues

<!-- language-links -->
[English](SECURITY.md) | [Русский](docs/ru/repository/SECURITY.md)
<!-- /language-links -->

Open-Source PSIM Platform is under MVP development. Implemented capabilities and limitations are listed in [release readiness](docs/product/release-readiness.md); release support rules are defined in the [compatibility policy](docs/engineering/compatibility-and-support.md).

## Supported versions

Security fixes target the main branch and the latest published release. Older versions must be upgraded. Separate LTS branches and guaranteed response times have not been established.

## How to report

If the [Security](https://github.com/d8zerg/psim/security) page provides **Report a vulnerability**, use it to submit a private report. If a private channel is unavailable, open an issue requesting confidential contact only; do not publish exploitation details, secrets or site data. Once a channel is agreed, send the description to the project owner.

Include:

- Affected version or commit and configuration.
- Impact and required conditions.
- Minimal reproduction steps using synthetic data.
- A proposed fix, if known.

Coordinate disclosure with the project owner after a fix is prepared. Perform testing only in your own environment or with its owner's permission.

## Project requirements

The [threat model](docs/security/threat-model.md) and [requirements register](docs/security/threat-register.md) define protective controls. The project checks dependencies, container images and SBOM; these checks complement behavioral tests and change review.
