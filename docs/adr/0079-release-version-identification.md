# ADR-079. Defining Version and Identity of a Delivery

<!-- language-links -->
[English](0079-release-version-identification.md) | [Русский](../ru/adr/0079-release-version-identification.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-024, ADR-034

## Context

The constant version `0.1.0` does not distinguish between builds and allows replacement of content within a single image tag. Updates and rollback require product versions, source identity, and verified identities for all artifacts. CMake, Debian, and Docker use different acceptable version representations.

## Solution

Use SemVer 2.0.0 and Git tags `vMAJOR.MINOR.PATCH`. The base version is taken from the nearest reachable SemVer tag in Git; version files and implicit base versions are not used. For the base, annotated and lightweight tags are allowed, with the prefix `v` or without it. Official releases require an annotated tag. The script `tools/version.py` defines the version once for CMake, packages, images, and web client metadata.

A clean tag sets the exact version. After a stable tag, the next patch is added with the suffix `-dev.<distance>+g<revision>`. After a prerelease tag, `.dev.<distance>` is added. Modified working trees are marked with `.dirty`; such builds are intended for development and qualification. Official releases are verified with `tools/version.py --release` and require a clean annotated tag. If no reachable tags exist, the normal build ends with an understandable error; for individual test artifacts, an explicit `PSIM_BUILD_VERSION` is allowed, which does not confirm an official release. Tag creation and publication are not performed by the versioning tool.

The complete SemVer is stored in binary files and `build-info.json` of the web client. The numerical part is used in `project(VERSION)` CMake. For Debian, the prerelease separator is replaced with `~`; for Docker tags, `+` is replaced with `_`. The inventory stores the product version; the transformation is performed by the delivery model.

The version does not replace the content’s identity. The identifier of the installed delivery is the SHA-256 digest of signed `SHA256SUMS`. It links images, DEBs, SBOMs, contract snapshots, and SQL plans. A separate trusted Ed25519 key verifies the signature; the key within the archive serves as reference information and does not become a source of trust.

For an image, the OCI descriptors of Linux amd64 and configuration are preserved. Docker Engine can show the manifest or configuration in `Image.Id` digest; only a digest from the signed chain is allowed. The mutable tag is not used as proof of identity.

The infrastructure is separately defined in `images.json` and compatibility policies. Updating the application release does not automatically change its version.

## Alternatives

| Option | Rating |
|---|---|
| Manual version in CMake and package.json | Multiple sources, builds are indistinguishable between releases |
| Only Git SHA | Identifies the source, but does not set the order of compatible releases |
| SemVer from Git and signed artifact identities | Chosen; separates compatibility, origin, and content |

## Verification

The clean tag, modified tree, SemVer compliance, Debian prerelease order, separate trusted key, changes to artifacts and contract snapshots, completeness of signed inventory, and preservation of old configuration are verified.

References: [SemVer](https://semver.org/), [git describe](https://git-scm.com/docs/git-describe), [Debian package versions](https://www.debian.org/doc/debian-policy/ch-controlfields.html#version), [CPack DEB](https://cmake.org/cmake/help/latest/cpack_gen/deb.html).
