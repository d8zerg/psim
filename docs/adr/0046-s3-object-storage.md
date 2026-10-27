# ADR-046. S3-compatible file storage

<!-- language-links -->
[English](0046-s3-object-storage.md) | [Русский](../ru/adr/0046-s3-object-storage.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** ADR-016, ADR-042, ADR-043

## Context

Floor plans and subsequent file functions require a private object storage with memory limits, integrity checks, and defined behavior after an unknown result of a write. The resource catalog manages plan versions separately from S3 versioning.

## Proposed Options

| Option | Properties | Solution |
|---|---|---|
| AWS SDK for C++, S3 module | Apache-2.0, SigV4, conditional requests, streaming bodies, and cancellation handler | Selected |
| MinIO C++ SDK | Apache-2.0, compact client; custom features for multipart buffering and transport settings | Not selected for this adapter |
| SeaweedFS | Apache-2.0, S3 gateway, filer, distributed master/volume; single binary file | Selected for MVP |
| Garage | AGPL-3.0, separate S3 service with distributed storage model | Not selected; static binding limitations do not automatically apply to separate servers |

## Solution

1. Register AWS SDK for C++ `1.11.692` through Conan with the S3 module; disable other services. The libcurl transport uses OpenSSL. Allow SPDX `curl` for libcurl: this is a permissive license requiring retention of its notice; include it in the license package. Calls to the SDK, file system, and SHA-256 should be done with `BlockingPool`; limit concurrent operations with a semaphore on the shard.
2. Select SeaweedFS `4.48`, and lock the digest. Locally run master, volume, filer, and S3 gateway with `weed server`. On Debian 13 x86_64, use the binary file from GitHub Releases; installation does not require closed software. Configuration, data directory, and unit files are defined by ADR-077.
3. One machine uses a local volume, with backup [under the recovery procedure](../engineering/administration-and-recovery.md). The cluster uses three masters with Raft, several volume servers with replicas in different failure domains, and several filer/S3 gateways behind a balancer. Filer metadata should be stored in a supported, shared highly available storage; its configuration and integration with the PostgreSQL platform are defined in ADR-077. Testing individual components does not confirm the overall cluster’s fault tolerance in physical failure domains.
4. Do not create an anonymous identity. The installer has a separate administrative identity, the application only has Read/Write access to its bucket `psim-plans`, and the reader only has Read access. Do not allow the application to create buckets, manage IAM, list other buckets, or set public ACLs. Domain authorization metadata remains in the Resource Catalog.
5. Use explicit access/secret keys through `security::read_secret`, TLS with a CA, name verification, and common profile `TlsOptions`, including CRL and client certificates when configured. Disable environment proxy settings, redirections, ambient credentials, and internal SDK retries. URLs, keys, secrets, and server diagnostic bodies should not be included in errors and metrics.
6. Object keys are immutable. Uploading creates a bounded snapshot of the regular file, calculates SHA-256, and sends a conditional PUT `If-None-Match: *`, along with SHA-256 checksum and metadata digest. RAM is limited by transfer blocks; temporary disk limits objects to 20 MiB per operation. The snapshot prevents changes to the sent bytes between digest calculation and repetition.
7. Check for unknown results of PUT with HEAD. The same size and SHA-256 provide successful recovery of the result; other objects cause conflicts. Only confirmed absence allows another conditional write. If the operation time expires, return an error with an unknown result; the next call with the same keys and bytes can perform verification. Operation retries are limited.
8. Downloading receives an independently expected size and SHA-256, checks HEAD, and reads from `If-Match` using an opaque ETag. The stream is written to a temporary file, hashed, and verified before atomic creation of the destination. Errors, cancellations, size exceedances, and digest mismatches remove the temporary result. The existing destination is not overwritten. ETag is not considered a digest or version of the plan.
9. HEAD and GET allow bounded retries within one operation time. DELETE is sent once: after a transport error, the result may be unknown. Cancellation marks the flag in the SDK call; native calls are drained until completion to avoid freeing active streams and files.
10. Public operations of the adapter include put/get/head/remove; bucket creation and IAM are part of installation. Plan versions, PNG/JPEG validation, user permissions, and cleanup of unbound objects are defined in ADR-051. Object backup and recovery, links, and trust are defined in ADR-081.

## Verification

AWS CRT requires GCC libatomic. The adapter statically binds the system `libatomic.a` from Debian 13 to work in the basic Debian image without additional shared libraries. License `GPL-3.0-or-later WITH GCC-exception-3.1` is already allowed by policy; a separate component `gcc-libatomic` with GCC version is included in the SBOM.

Objects up to 20 MiB, binary content and SHA-256, conditional writes and conflicts, metadata/delete, access denials, incorrect credentials, TLS with incorrect CA and name. Loss of response after PUT should leave the checked object and allow safe verification. Size limits, cancellations, deadlines, and network breaks do not publish partial files. Component checks are performed on the current SeaweedFS; the library is tested under ASan/UBSan and TSan on Debian 13 x86_64.

## Sources

- [AWS SDK for C++](https://github.com/aws/aws-sdk-cpp/tree/1.11.692).
- [curl License](https://curl.se/docs/copyright.html).
- [S3 conditional requests](https://docs.aws.amazon.com/AmazonS3/latest/userguide/conditional-writes.html).
- [SeaweedFS and license](https://github.com/seaweedfs/seaweedfs/tree/4.48).
- [SeaweedFS deployment diagram](https://github.com/seaweedfs/seaweedfs/wiki/Production-Setup).
