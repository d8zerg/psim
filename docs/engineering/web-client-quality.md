# Web Client Qualification

<!-- language-links -->
[English](web-client-quality.md) | [Русский](../ru/engineering/web-client-quality.md)
<!-- /language-links -->

Status: Accepted. Solution - [ADR-076](../adr/0076-web-client-qualification.md), load comparison - [report](web-client-loading.md).

## Launch

```bash
task web:qualify
```

The task sequentially checks types, formatting, contracts, module tests with coverage, visual baselines, and accessibility; compares load; measures the feed; executes business scenarios with real services and a five-minute network outage. Linux, native Docker Engine, and Docker Compose are required. Node.js, pnpm, and browsers run in fixed images. Measurements are performed before launching the service environment.

| Task | Verification | Artifacts |
|---|---|---|
| `task web:check` | Generation, ESLint, TypeScript, Vitest with coverage, build, size, Chromium, Axe, comparison with snapshots | `web/*/*/coverage/`, Playwright reports |
| `task web:loading` | Cold and warm HTTP cache, both languages, direct input and transitions, alternative artifacts, load failures | `build/web-quality/variants.json`, `loading.json`, `comparison.json` in `build/web-quality/` |
| `task web:perf` | 10,000 lines, FPS and p99 realtime -> DOM, separate Chromium worker | `build/operator/performance.json` |
| `task web:native` | Single build of client and release services, then automated operator, administrator, and analyst acceptance scenarios | `build/web-quality/native.json`, `build/operator/release/`, `build/admin/release/`, `build/insights/release/` |
| `task web:session` | Expired JWT, five minutes without network, replay and restart of Gateway | `build/event-flow-test/` |

For reruns after driver corrections, `task web:native NATIVE_RESUME=--resume` is available: the already successful group is reused only if the build fingerprint, service source code, contracts, and their tests match. The regular CI always runs all groups again.

The exact directories for individual artifacts are defined by corresponding tasks. `task web:e2e` remains a separate launch for all browser checks of the built application and component catalog. `task web:visual:update` updates benchmarks for change checking; the regular CI only compares them.

## Cross-Scenario Tests

Native browsers enter through the real Keycloak with PKCE and MFA. Business-API responses and WSS channels are not replaced. Each run creates a separate Compose project, its own resources and users, then deletes the environment along with its profiles and test volumes. The project prefix is set via `NATIVE_PROJECT`; a separate session check project via `SESSION_PROJECT`.

| Scenario | Verification through interface | Related server checks |
|---|---|---|
| S1 | Connector registration, public certificate from external PKI and renewal; saving mappings, checking, and activating rules. The registered connector sends events via mTLS until the current incident and scenario | SDK compliance and certificate failures have separate checks |
| S2 | Site, building, three floors, twenty zones, floor plans, importing file containing 50,000 devices with creation and updates, no more than five minutes; failure of non-supported files and other sites’ floor plans | Load tests on the directory and S3 recovery are not replaced by browser tests |
| S6 | Accepting an incident, starting operations, opening the tree and private plan, seeing the device status | Load and server access matrix are checked separately |
| S7 | Required checklist and decision, restarting both replicas of the Response Engine, publishing a new version, saving the old version and current step; proposal and acceptance of responsibility by another operator with the same SLA | Concurrent operations, repeated requests and other branches are checked by the Response Engine |
| S8 | Previewing and executing commands from the active step of the scenario; critical command requires another shift supervisor, the initiator is not offered self-approval; device failure and timeout get final statuses; new command requires final result of the previous one, new reason and preview | Delivery delay and lack of repeated executions are checked by the Command Service |
| S9 | Receiving the current SLA notification and marking it as read | Levels, email/webhook, deadlines and delivery repetitions are checked by the Notification Service |
| S10 | Searching and pages for audit, checking integrity, revoking role and scope without new JWT, absence of out-of-scope data in CSV | Cryptographic checkpoints and chain damage are checked by the Audit Service |
| S13 | Report based on pre-prepared data, filters, grouping, SVG graphs and CSV; original SLA, reporting time and failure after scope revocation | Load scenarios for reports are checked separately |

The operator scenario is checked until the final state: the recipient cannot resolve an incident with an unfinished required step; after completing the step, it resolves and closes the incident. The native driver additionally reads states `CLOSED` and `RUN_STATE_COMPLETED` from the server.

Import verification includes reading the file, full validation, and application. Validation retains the model, links, rights, and restrictions of the database within the rollback transaction, but does not create temporary publications for each line. The batch contains up to 64 lines. Historical replay can handle up to 60,000 devices and objects, live rule catalog - up to 120,000 devices, sources, and objects. Adding a new device does not scan previously added devices; replacing an existing device only removes its previous sources. These changes are needed for a complete set of 50,000 devices with sources and object hierarchy. The list of sources in historical snapshots is only needed for periodic checks to ensure absence-rule timers without new events; its limit is 10,000 sources and 8 MiB saved.

Connection interruption is checked by a real network failure of at least 300,000 ms: the Gateway is allowed up to 30 seconds for time differences, delayed tokens receive a 401, changes occur without the browser, token updates and replay restore the state; the Gateway is restarted. Compact browser checks additionally cover HTTP snapshot errors, RESYNC, saving mappingsed data, and prohibition of writing when connection is lost.

The browser parts S1/S2/S6-S10/S13 are automated. The server, simulation, load, and cluster parts of these scenarios remain in [the test matrix](../quality/verification-matrix.md). S3-S5 do not require a browser, full cluster stability S11 and installation S12 belong to operational qualification.

## CI and Limits

When changes are made to the web client or its drivers, the local CI runs a comparison of load and an isolated test of the feed before services. Then, it performs `web:check`, `web:native`, and `web:session`. Methods TS-11, TS-14, TS-15, and TS-38 are related to tasks and scenarios in the [registry](../quality/verification.yaml).

The limits of ADR-075 are maintained: initial load is 150,000 bytes gzip per language; the total application graph is 250,000; the showcase graph is 260,000; unique JS and JSON dictionaries are 300,000; CSS is 15,000. The calculation of the total graph includes dynamic imports and JSON dictionaries. Transferring data from JS to JSON does not reduce the measured delivery.

For bench testing, the latency limits are p95 2000 ms for cold start or first transition, and 1500 ms for warmed cache. Three repetitions are intended to detect large regressions on the isolated CI host. This is not an evaluation of the user’s p95 and does not replace the load-based SLO. Testing the feed separately ensures at least 50 FPS and p99 not exceeding 1000 ms.

## Results

| Test | Result |
|---|---|
| `web:check` | 19 tasks, 220 module and 282 browser tests: 76 consoles and 206 design systems; types, coverage, Axe, and visual baselines pass |
| `web:loading` | Eight artifacts; 70 route/language pairs with three repetitions for cold and warmed starts, 56 transitions with three repetitions, five boundary and recovery tests |
| `web:perf` | 10,000 records, 60.00 FPS, p99 realtime -> DOM 12.30 ms |
| `web:native` | Six browser scenarios passed on actual services: one operator, two administrative, and three audit and reporting; incident closed, scenario completed, hashes of all three client artifacts match measurements |
| `web:session` | Actual delay of 318834 ms with a limit of 300,000; expired JWT rejected with 401, new token received, replay and restart of Gateway passed |
| C++ and Architecture | 10 module tests for the directory and 38 rule unit tests pass in release, ASan/UBSan, and TSan; individual native directory tests and all five types of rules, clang-tidy, and architectural constraints pass |

The selected option: initial JS EN/RU - 128,466/129,859 bytes gzip, total application graph - 240,519, showcase graph - 183,963, unique JS/JSON - 297,812. All previous budgets pass. The maximum bench p95 for cold/warmed starts is 193/89 ms, for first/repetition transitions - 81/69 ms. Methods and alternatives are provided in [load comparison](web-client-loading.md).

After the final result, a new command with a new reason and preview is available. The active step command is created through the Response Engine and advances the scenario execution. Explicit testing preserves results of all commands in the group, including those from unloaded pages in the list. Lost responses do not lead to re-sending. The native full import of 50,000 lines took 275,339 ms with a limit of 300,000; the report confirmed 49,999 creations and one update. For the directory, temporary publications are removed during validation of imports, source scanning is done during initial live loading, and unnecessary sources are removed in historical filtering using the usual rule.

All qualification tasks pass. The operator scenario was tested on actual services through incident closure and scenario completion.
