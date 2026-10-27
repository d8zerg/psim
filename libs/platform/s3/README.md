# S3 Layer

<!-- language-links -->
[English](README.md) | [Русский](../../../docs/ru/repository/libs/platform/s3/README.md)
<!-- /language-links -->

[ADR-046](../../../docs/adr/0046-s3-object-storage.md) · `psim::platform::s3`

AWS SDK for C++ `1.11.692`, the S3 module, and SeaweedFS `4.48` for MVP. The adapter provides uploading, downloading, metadata, and deleting objects up to 20 MiB in size. Domain rights, plan versions, PNG/JPEG validation, and cleaning up unbound objects are handled by Resource Catalog.

## Usage and Execution

Create `Sdk` in the main thread before starting the shard. It should survive all clients and unfinished operations. After clients terminate, destroy it in the main thread: the SDK has process state and threads, and stopping them does not affect the event processing path.

`Client::create` receives `BlockingPool`, `Options`, and the optional `Metrics`. Creating a client, reading secrets, calling the SDK, handling file systems, and calculating SHA-256 are performed in BlockingPool. The client is tied to one Asio executor; individual operations can run parallel within `concurrency`. `close()` stops accepting new operations and wakes up pending calls. Active calls are completed before freeing files and threads.

Configure explicit endpoints, region, bucket, and access/secret keys. They are read through `security::read_secret`, with owner verification, regular file checks, and `0400` rules. Credential chains, environment proxy parameters, and redirections are not used. TLS verifies CA and DNS name through libcurl/OpenSSL and the general `TlsOptions`; client certificates and CRLs are available. `allow_plaintext` is an exception for isolated development.

Calls:

```cpp
auto client = co_await psim::platform::s3::Client::create(&blocking, options, &metrics);
auto uploaded = co_await (*client)->put("tenant/object-id", source_path);
auto metadata = co_await (*client)->head("tenant/object-id");
auto downloaded = co_await (*client)->get(*uploaded, destination_path);
auto removed = co_await (*client)->remove("tenant/object-id");
```

In real code, verify each `Result` before dereferencing. Errors contain catalog error codes and secure messages; keys, endpoints, paths, and secrets do not appear in messages or metric labels.

## Streaming, Integrity, and Publication

Uploading opens a regular file without following a symbolic link and creates a snapshot in the temporary file. During copying, size limits, cancellation, and operation duration are checked, and SHA-256 is calculated. Only bytes of the snapshot are sent; changes to the original path do not change the repetition. Memory is limited to 64 KiB blocks and SDK/HTTP buffers; the temporary disk size is the object size for each operation. The limit `max_object_bytes` can be reduced, with a maximum of 20 MiB.

PUT sends checksum SHA-256 and metadata `sha256`. ETag is stored only as an opaque token condition. It is not considered SHA-256 and not used as a plan version.

GET requires independently expected size and SHA-256 from the upload result or catalog metadata. First, metadata HEAD is checked; then GET uses `If-Match` to avoid reading another object after HEAD. Bytes are written to the temporary file with the destination; hash and length are checked before atomic creation of the target name. The destination must be absent. Errors, cancellations, exceeding limits, and mismatches in SHA-256 do not leave a successful or partial target file; the temporary file is deleted.

Atomic creation of a name is not a backup copy. Long-term storage, restoration, and matching with the catalog are checked through [backup and recovery procedures](../../../docs/engineering/administration-and-recovery.md).

## Retries and Unknown Outcomes

Object keys are immutable. PUT uses `If-None-Match: *`. If no response is received, the client performs HEAD: matching size and SHA-256 restores the result; another object causes a conflict. Only confirmed absence allows another conditional PUT. Expiration may leave an unknown result, which cannot be declared as a failed write; the calling code can repeat the call with the same key and bytes and get a check.

The `WriteMode::kWriteOnly` mode only performs conditional PUT and does not perform HEAD for checking unknown results. It is designed for separate account of backup copies; an unknown result is returned as an error. `list` performs ListObjectsV2 with continuation and page size up to 1000 keys; the right ListBucket is only required by reading administrative clients.

For HEAD/GET, there are no more than `read_attempts` internal repeats; SDK internal repeats are disabled. DELETE is sent once; in case of transport error, deletion may also have an unknown result. An object cannot be automatically replaced with the same key, especially after a network failure.

`timeout` sets the operation duration, `acquire_timeout` waits for permission, `connect_timeout` connects. Asio cancellation sets the atomic flag `ContinueRequestHandler`; synchronous SDK calls are drained until return. This is cooperative cancellation with the granularity of libcurl callbacks, not immediate interruption of system calls. File operations in BlockingPool are also not interrupted during system calls.

## Environment and Deployment

```bash
task env:up
task test:s3
task test:s3:sanitizers
task test:component
```

The local environment contains `s3` with a private bucket `psim-plans`, a permanent data volume, and TLS. The installation identity is separated from `filestore` (Read/Write only this bucket) and `reader` (Read only this bucket). There is no anonymous identity. Fixture keys and certificates are created by `tools/env/s3_test_tls.sh`; they are not used in production. Master, filer, and service ports are not published externally. Fault endpoint through Toxiproxy is only needed for component scenarios.

For Debian 13 x86_64, there is a single binary file `weed` from the pinned release. On one machine, `weed server` is used; a cluster requires multiple masters, volumes with replicas, a common fault-tolerant filer metadata store, and several S3 gateways. Placement, least privilege, TLS, and HA checks are described in [deployment topologies](../../../docs/engineering/deployment-topologies.md); backup procedures are in [administration and recovery](../../../docs/engineering/administration-and-recovery.md). The current component environment checks the client against one actual SeaweedFS, not the cluster’s fault tolerance.

Metrics `psim_s3_operation_seconds`, `psim_s3_transferred_bytes_total`, `psim_s3_errors_total`, `psim_s3_retries_total` do not contain tags for keys, tenants, URLs, or credentials. The coverage threshold for the infrastructure adapter is 75% across module and component checks.
