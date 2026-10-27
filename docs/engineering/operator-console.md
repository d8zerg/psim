# Operator Console

<!-- language-links -->
[English](operator-console.md) | [Русский](../ru/engineering/operator-console.md)
<!-- /language-links -->

Solution: [ADR-071](../adr/0071-operator-console.md). Server operations: [operational interface](operational-interface.md). Session and connection: [application shell](application-shell.md).

## Functionality

The `/` route is accessible to the operator and shift supervisor. The feed supports search, priority, status, site, and responsible person. The DOM contains a limited number of rows; the active row with keyboard focus remains visible during scrolling. When viewing the start of the list, new entries appear immediately; when viewing old entries, their positions are retained.

Alt+F switches focus to search, Alt+A accepts a selected new incident, Alt+W starts working with the accepted incident, Alt+R updates data. Arrow keys, Home, and End select rows in the feed. Combined actions are not intercepted in input fields.

The card displays alarm status separately from the incident phase, responsible person, risk, SLA deadline, escalation, presence, scenario, commands, timeline, and signals from original events. The timeline, signals, commands, and notifications have explicit pagination. Required checklists and resolutions are checked before sending the step result; the server rechecks conditions. Incident resolution sends the current known version of the scenario.

Handover first creates an offer. The current responsible person is retained. The recipient reviews the incident and scenario, marks the current version, and confirms acceptance. Changing data requires a new review; a briefing invalidated by the server requires a new handover offer. Designating a duty holder is performed by the shift supervisor; a backup contact is required.

Sound is enabled in the sound notification panel. Volume and speaker test are available. Limitations on repetitions and browser restrictions do not acknowledge alarms or remove visual indicators. In a hidden tab, the title shows the number of unconfirmed alarms.

## Commands and Errors

Commands are taken from the accessible device type without providing the operator access to all type directories. Select the device, capability, and parameters or active command step in the scenario. Specify the basis and request a preview. Before sending, check the object, consequences, and need for a second shift supervisor. Changing the proposal requires a new preview. Unknown consequences are displayed clearly.

After sending, commandId is retained. If the response is lost, use result checking; the client reads the existing command and does not create a new one. Execution status, failure, timeout, and unknown outcome are displayed separately. Confirming a critical command is available to another shift supervisor after its own preview.

In case of version conflict, the client rereads the status and reports the need for verification. When connection is lost, last data is retained with markings; actions that change are disabled. Reconnecting the channel and periodic checks restore details without restarting. Old responses do not replace data with new identity. In training and video wall modes, production actions are disabled.

After the final result, the “New Command” action clears the previous preview and reason. The new preview creates a new identifier. Unknown outcome retains the previous identifier and prohibits this action until a final state from the command list or explicit result checking is received. Sending does not repeat automatically.

Command steps are confirmed through the Response Engine with a known execution version. The engine creates commands and links their results to its step. The interface shows the preview of each device when targeting “all devices in the zone”; the number of targets is limited to 128. After an uncertain response, the interface reads the scenario execution and checks received server identifiers without re-sending.

## Objects

Select an object in the tree. For a floor or zone, there’s a protected plan with scaling, markers, and device text labels. Connection, quality, and confirmation time are not replaced by the last event. Unknown and stale data, maintenance, and disconnected states are not displayed as healthy. When changing the plan and closing an object, the temporary image URL is released.

## Checks

`task web:check` performs checks on the client, accessibility, languages, and sizes. `task operator:benchmark` is performed separately from the parallel visual matrix; the result is `build/operator/performance.json`. It has a scrolling frame rate for 10,000 rows and raw realtime -> DOM delays, without replacing the full event-to-browser SLO.

`task operator:test PRESET=release` creates a separate Docker Compose project. `OPERATOR_PROJECT` sets its name. The browser works with a real OIDC session and server processes; the result and snapshot are in `build/operator/release`. `PSIM_OPERATOR_KEEP_ENV=1` leaves an isolated test environment for diagnosis; a normal run releases it. The full input pipeline is checked separately by `task flow:test` in the prepared environment.

When changes occur in the web client or operator checks, `task ci` runs a separate benchmark before starting the environment and the native browser scenario after web checks.

Initial loading, total graph, and total JS composition continue to be checked with previous limits. Optimizing browser protobuf descriptors does not change server validation and original contracts. The interface retains its separate showcase dictionaries and components outside the production application dependency graph.
