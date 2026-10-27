# ADR-076. Web Client Qualification

<!-- language-links -->
[English](0076-web-client-qualification.md) | [Русский](../ru/adr/0076-web-client-qualification.md)
<!-- /language-links -->

- **Status:** Accepted
- **Related to:** [ADR-065](0065-ui-loading-budgets.md), [ADR-069](0069-application-shell-and-session-recovery.md), [ADR-070](0070-lazy-loading-and-delivery-budgets.md), [ADR-075](0075-audit-and-reporting-delivery-budget.md)

## Solution

Qualification combines contract and type checks, module validation, visual regression and accessibility, loading and feed measurements, native administrator, operator, auditor, and manager scenarios, as well as reconnection after disconnection. Cluster deployment setup and failure scenarios retain their separate checks.

Comparison is performed on reproducible artifacts: source build, static module loading, deferred editors, selected module language, and individual application and showcase entry points. Both languages are measured, each route, cold and warmed HTTP caches, static and full graphs. Common files are not summed repeatedly. Dynamic imports are included in the full graph. Separate artifacts are measured both individually in full and together; excluding a showcase from the working deployment does not mean removing its dependencies.

In the working configuration, both languages are included in the code of the opened module. In the experimental variant, the selected JSON dictionary is loaded before display. Switching languages requires the product dictionary and dictionaries of already opened modules; an error retains the previous language and input. Dictionary caches contain only translatable strings. They do not replace session-based isolation of server data and access scope.

Load measurements use a static server with real cache headers and separate controlled identification and API responses. They do not replace processing with Keycloak, Kafka, and databases. Raw sizes, network events, times, artifact composition, and results are saved for verification. Unavailable chunks, build updates, closed routes, and reconnection are checked separately.

The limits of ADR-075 are maintained. The selected configuration and functional checks are included in CI; alternative runs are initiated by the `web:loading` task in the same pipeline. For test environment latency, p95 is set to 2000 ms with cold input and first transition, and 1500 ms with warmed cache. The native processing checks incident completion, required steps, scenario version and state preservation upon restart, commands, SLA, administration, auditing, and reports.

Method and results: [qualification](../engineering/web-client-quality.md), [load comparison](../engineering/web-client-loading.md).

The qualified catalog contains 50000 devices with sources, as well as a site, building, floors, and zones. Historical replay is limited to 60000 device and object records, while live control copies have 120000 device, source, and object records. Previous total limits were 50000 and 100000, which did not accommodate such a set. Limits remain finite; JS deployment budgets do not change. Import validation and application together are limited to 300000 ms.

The historical list of sources is only used for periodic checks to ensure no events occur without a trigger. Other types of rules use events, object hierarchy and maintenance without a full list of sources. For periodic checks, a limit of 10000 sources and a snapshot size of 8 MiB are maintained; qualifying a single rule on a catalog of 50000 devices does not remove these limits.
