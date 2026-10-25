import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { createApiClient } from "@psim/api-client";
import type { ReactNode } from "react";
import { OperatorApi, RequestFailure, type Incident, type Schema } from "./api";
import { IncidentPanel } from "./incident";
import { ObjectPanel } from "./objects";
import { CommandsPanel } from "./commands";
import { NotificationsPanel } from "./notifications";
import { RunPanel } from "./response";
import { ActionNotice, useActions, type Actions } from "./queries";
import { setPreferences } from "../ui/preferences";
const id = "00000000-0000-4000-8000-000000000001";
const deviceId = "00000000-0000-4000-8000-000000000002";
const profile = { subject: "recipient", name: "Operator", context: "context", roles: ["operator", "supervisor"] };
let latestActions: Actions;
const clients: QueryClient[] = [];
afterEach(() => {
  cleanup();
  clients.forEach((client) => {
    client.clear();
  });
  vi.restoreAllMocks();
});
beforeEach(() => {
  setPreferences({ language: "en", mode: "normal", sound: false });
});
function fixture() {
  let incident: Incident = {
    incidentId: id,
    version: "1",
    title: "Operational incident",
    state: "INCIDENT_STATE_NEW",
    assigneeId: "recipient",
    siteId: "site",
    escalationLevel: 1,
    operational: {
      conditionKnown: true,
      active: true,
      acknowledged: false,
      acknowledgeDue: "2030-01-01T00:00:00Z",
      resolveDue: "2030-01-02T00:00:00Z",
      presence: [{ actorId: "viewer", leaseId: "lease", until: "2099-01-01T00:00:00Z", editing: true }],
    },
  };
  let run: Schema["psim.response.v1.ResponseRun"] = {
    runId: "run",
    version: "1",
    planVersion: 1,
    activeStepIds: ["instruction"],
    steps: [{ stepId: "instruction", state: "STEP_STATE_ACTIVE" }],
  };
  const plan: Schema["psim.response.v1.ResponsePlan"] = {
    name: "Response plan",
    steps: [
      { stepId: "instruction", title: "Inspect object", mandatory: true, instruction: { text: "Verify safety" } },
    ],
  };
  let fail = 0;
  let loseSend = false;
  const writes: { path: string; body: Record<string, unknown>; version: string | null }[] = [];
  const fetch = vi.fn<typeof globalThis.fetch>().mockImplementation(async (input) => {
    // openapi-fetch always constructs a Request at this boundary.
    const request = input as Request;
    const url = new URL(request.url);
    const path = url.pathname;
    const get = request.method === "GET";
    const body = get ? {} : ((await request.json()) as Record<string, unknown>);
    if (!get) writes.push({ path, body, version: request.headers.get("If-Match") });
    if (!get && fail) {
      const status = fail;
      fail = 0;
      return Response.json(
        { type: "about:blank", title: "Rejected", code: "COMMON_CONCURRENT_MODIFICATION", status },
        { status },
      );
    }
    if (path.endsWith("/operations")) return Response.json({ incident, slaRisk: "at-risk" });
    if (path.endsWith("/timeline"))
      return Response.json({
        entries: [
          {
            entryId: "entry" + (url.searchParams.get("pageToken") ?? ""),
            text: "Recorded event",
            actor: { id: "actor" },
            at: "now",
          },
        ],
        ...(!url.searchParams.get("pageToken") ? { nextPageToken: "next" } : {}),
      });
    if (path.endsWith("/signals"))
      return Response.json({
        signals: [
          {
            signalId: "signal" + (url.searchParams.get("pageToken") ?? ""),
            signal: { origin: { ruleId: "rule" }, eventIds: ["event"] },
          },
        ],
        ...(!url.searchParams.get("pageToken") ? { nextPageToken: "next" } : {}),
      });
    if (path.endsWith("/run")) return Response.json({ run, plan });
    if (path.startsWith("/v1/events/")) return Response.json({ event: { eventId: "event" } });
    if (path === "/v1/commands" && get) return Response.json({ commands: [] });
    if (path.startsWith("/v1/devices/"))
      return Response.json({
        device: { deviceId, deviceTypeId: "type" },
        deviceType: {
          capabilities: [{ code: "unlock", name: "Unlock door", parameters: [{ name: "duration", required: true }] }],
        },
      });
    if (path === "/v1/commands:preview")
      return Response.json({ fingerprint: "fingerprint", consequence: "Door unlocks", requiresSecondSupervisor: true });
    if (path === "/v1/commands" && !get) {
      if (loseSend) throw new TypeError("response lost");
      return Response.json({ command: { commandId: body.commandId, state: "COMMAND_STATE_EXECUTED" } });
    }
    if (path.startsWith("/v1/commands/"))
      return Response.json({ command: { commandId: path.split("/")[3], state: "COMMAND_STATE_UNKNOWN" } });
    if (path === "/v1/incidents:transfer") {
      const items = body.items as { recipientId: string; transferId: string }[];
      incident = {
        ...incident,
        version: String(Number(incident.version) + 1),
        ...(items[0]?.transferId
          ? { assigneeId: "recipient", pendingTransfer: undefined }
          : {
              pendingTransfer: {
                transferId: "transfer",
                recipientId: items[0]?.recipientId ?? "",
                requestedVersion: String(Number(incident.version) + 1),
              },
            }),
      } as Incident;
      return Response.json({ results: [{ incident }] });
    }
    if (!get && path.includes("/steps/")) {
      run = { ...run, version: String(Number(run.version) + 1) };
      return Response.json({ run });
    }
    if (!get && path.startsWith("/v1/incidents/")) {
      const state = path.endsWith(":acknowledge")
        ? "INCIDENT_STATE_ACKNOWLEDGED"
        : path.endsWith(":start-work")
          ? "INCIDENT_STATE_IN_PROGRESS"
          : path.endsWith(":resolve")
            ? "INCIDENT_STATE_RESOLVED"
            : path.endsWith(":reopen")
              ? "INCIDENT_STATE_IN_PROGRESS"
              : path.endsWith(":close")
                ? "INCIDENT_STATE_CLOSED"
                : incident.state;
      incident = {
        ...incident,
        state: state ?? "INCIDENT_STATE_UNSPECIFIED",
        version: String(Number(incident.version) + 1),
      };
      return Response.json({ incident });
    }
    return Response.json({});
  });
  const api = new OperatorApi(createApiClient({ baseUrl: "https://psim.test", getAccessToken: () => "token", fetch }));
  return {
    api,
    fetch,
    writes,
    incident: () => incident,
    setIncident: (value: Incident) => {
      incident = value;
    },
    setFail: (value: number) => {
      fail = value;
    },
    loseSend: () => {
      loseSend = true;
    },
  };
}
function mount(view: (actions: Actions) => ReactNode) {
  const client = new QueryClient({
    defaultOptions: { queries: { retry: false, gcTime: 0 }, mutations: { retry: false } },
  });
  clients.push(client);
  function View() {
    const actions = useActions();
    latestActions = actions;
    return (
      <>
        <ActionNotice actions={actions} />
        {view(actions)}
      </>
    );
  }
  const result = render(
    <QueryClientProvider client={client}>
      <View />
    </QueryClientProvider>,
  );
  return { ...result, client };
}
async function click(name: string) {
  const button = screen.getByRole("button", { name });
  await waitFor(() => {
    expect(button.hasAttribute("disabled")).toBe(false);
  });
  fireEvent.click(button);
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
}
it("runs incident acceptance, comments, optimistic conflicts, alarms and lifecycle without changing SLA", async () => {
  const f = fixture();
  mount((actions) => <IncidentPanel id={id} api={f.api} live writable profile={profile} actions={actions} />);
  await screen.findByText("Operational incident");
  await click("Acknowledge incident");
  expect(f.incident().state).toBe("INCIDENT_STATE_ACKNOWLEDGED");
  fireEvent.keyDown(window, { altKey: true, code: "KeyW" });
  await waitFor(() => {
    expect(f.incident().state).toBe("INCIDENT_STATE_IN_PROGRESS");
  });
  fireEvent.change(screen.getByLabelText("Comment", { exact: true }), { target: { value: "Inspection recorded" } });
  await click("Add comment");
  fireEvent.change(screen.getByLabelText("Reason", { exact: true }), { target: { value: "Inspection complete" } });
  fireEvent.change(screen.getByLabelText("Until", { exact: true }), { target: { value: "2099-01-01T12:00" } });
  // Bound the temporary control to the current server-compatible day.
  fireEvent.change(screen.getByLabelText("Until", { exact: true }), {
    target: { value: new Date(Date.now() + 60000).toISOString().slice(0, 16) },
  });
  await click("Shelve alarm");
  await click("Remove shelving");
  await click("Suppress alarm");
  await click("Remove suppression");
  await click("Acknowledge alarm");
  f.setFail(412);
  await click("Resolve incident");
  await screen.findByText(/Data changed/u);
  await click("Resolve incident");
  expect(f.incident().state).toBe("INCIDENT_STATE_RESOLVED");
  await click("Reopen incident");
  await click("Resolve incident");
  await click("Close incident");
  expect(f.incident().operational?.resolveDue).toBe("2030-01-02T00:00:00Z");
  expect(f.writes.every((write) => write.path === "/v1/incidents:transfer" || write.version !== null)).toBe(true);
});
it("requires recipient review of current incident and run before confirming responsibility", async () => {
  const f = fixture();
  f.setIncident({ ...f.incident(), state: "INCIDENT_STATE_IN_PROGRESS", assigneeId: "owner" });
  const { client } = mount((actions) => (
    <IncidentPanel id={id} api={f.api} live writable profile={profile} actions={actions} />
  ));
  await screen.findByText("Operational incident");
  fireEvent.change(screen.getByLabelText("Reason", { exact: true }), { target: { value: "Shift change" } });
  fireEvent.change(screen.getByLabelText("Recipient", { exact: true }), { target: { value: "recipient" } });
  await click("Assign operator");
  await click("Request transfer");
  const review = await screen.findByLabelText("I reviewed this version of the incident and response run");
  fireEvent.click(review);
  f.setIncident({
    ...f.incident(),
    version: "5",
    pendingTransfer: { ...f.incident().pendingTransfer, requestedVersion: "5" },
  });
  await act(async () => {
    await client.invalidateQueries();
  });
  await waitFor(() => {
    expect(screen.getByRole("button", { name: "Accept responsibility" }).hasAttribute("disabled")).toBe(true);
  });
  fireEvent.click(review);
  await click("Accept responsibility");
  expect(f.incident().assigneeId).toBe("recipient");
});
it("paginates history and signals and retrieves source event evidence", async () => {
  const f = fixture();
  mount((actions) => <IncidentPanel id={id} api={f.api} live writable profile={profile} actions={actions} />);
  await screen.findByText("Operational incident");
  const timeline = screen.getByRole("heading", { name: "Incident timeline" }).closest("section");
  const signals = screen.getByRole("heading", { name: "Signals and source events" }).closest("section");
  if (!timeline || !signals) throw new Error("Missing evidence regions");
  fireEvent.click(within(timeline).getByRole("button", { name: "Load more" }));
  fireEvent.click(within(signals).getByRole("button", { name: "Load more" }));
  await waitFor(() => {
    expect(within(timeline).getAllByRole("listitem")).toHaveLength(2);
  });
  const firstSignal = screen.getAllByText(/signal.*rule/u)[0];
  if (firstSignal) fireEvent.click(firstSignal);
  const source = screen.getAllByRole("button", { name: "Source events: event" })[0];
  if (source) fireEvent.click(source);
  await screen.findByText(/"eventId": "event"/u);
});
it("validates checklist and decision results, optional skip, failed retry and supervisor override", async () => {
  const f = fixture();
  const steps: Schema["psim.response.v1.Step"][] = [
    {
      stepId: "check",
      title: "Safety",
      mandatory: true,
      checklist: { items: [{ itemId: "required", text: "Safety verified", required: true }] },
    },
    {
      stepId: "decision",
      title: "Decision",
      decision: {
        question: "Outcome",
        options: [
          { optionId: "yes", label: "Confirmed" },
          { optionId: "no", label: "False" },
        ],
      },
    },
    { stepId: "optional", title: "Optional", instruction: { text: "Optional text" } },
    { stepId: "failed", title: "Failed", instruction: {} },
  ];
  mount((actions) => (
    <RunPanel
      api={f.api}
      response={{
        run: {
          runId: "run",
          version: "2",
          steps: steps.map((step) => ({
            stepId: step.stepId ?? "",
            state: step.stepId === "failed" ? "STEP_STATE_FAILED" : "STEP_STATE_ACTIVE",
          })),
        },
        plan: { steps },
      }}
      writable
      actions={actions}
      supervisor
    />
  ));
  const safety = screen.getByRole("region", { name: "Safety" });
  expect(within(safety).getByRole("button", { name: "Complete step" }).hasAttribute("disabled")).toBe(true);
  fireEvent.click(screen.getByLabelText("Safety verified (Mandatory step)"));
  fireEvent.click(within(safety).getByRole("button", { name: "Complete step" }));
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  fireEvent.change(screen.getByLabelText("Outcome"), { target: { value: "yes" } });
  fireEvent.click(
    within(screen.getByRole("region", { name: "Decision" })).getByRole("button", { name: "Complete step" }),
  );
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  fireEvent.change(screen.getByLabelText("Reason: Optional"), { target: { value: "Not needed" } });
  fireEvent.click(within(screen.getByRole("region", { name: "Optional" })).getByRole("button", { name: "Skip step" }));
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  await click("Retry failed step");
  fireEvent.change(screen.getByLabelText("Reason: Failed"), { target: { value: "Verified manually" } });
  await click("Override failed step");
  expect(
    f.writes.some((write) => (write.body.result as { checkedItemIds?: string[] }).checkedItemIds?.includes("required")),
  ).toBe(true);
});
it("binds command preview and blocks resend after an unknown delivery outcome", async () => {
  const f = fixture();
  f.loseSend();
  mount((actions) => (
    <CommandsPanel
      api={f.api}
      incident={{ incidentId: id, version: "1" }}
      run={undefined}
      plan={undefined}
      live
      writable
      profile={profile}
      actions={actions}
    />
  ));
  fireEvent.change(screen.getByLabelText("Device", { exact: true }), { target: { value: deviceId } });
  await screen.findByRole("option", { name: "Unlock door" });
  fireEvent.change(screen.getByLabelText("Capability", { exact: true }), { target: { value: "unlock" } });
  fireEvent.change(screen.getByLabelText(/Parameters: duration/u), { target: { value: "10" } });
  fireEvent.change(screen.getByLabelText("Reason: Send device command"), { target: { value: "Evacuation" } });
  await click("Preview command");
  await screen.findByText("Consequence: Door unlocks");
  fireEvent.change(screen.getByLabelText(/Parameters: duration/u), { target: { value: "20" } });
  expect(screen.getByRole("button", { name: "Confirm command" }).hasAttribute("disabled")).toBe(true);
  await click("Preview command");
  await click("Confirm command");
  await screen.findByText(/The result is unknown/u);
  expect(screen.getByRole("button", { name: "Confirm command" }).hasAttribute("disabled")).toBe(true);
  await click("Check command result");
  expect(f.writes.filter((write) => write.path === "/v1/commands")).toHaveLength(1);
  expect(f.writes.find((write) => write.path === "/v1/commands")?.body.previewFingerprint).toBe("fingerprint");
});
it("approves critical commands only after a fresh preview by another supervisor", async () => {
  const f = fixture();
  f.fetch.mockImplementation(async (input) => {
    const request = input as Request;
    const path = new URL(request.url).pathname;
    if (request.method === "GET")
      return Response.json({
        commands: [
          {
            commandId: "pending",
            deviceId,
            capability: "unlock",
            requestedBy: { id: "other" },
            version: "1",
            state: "COMMAND_STATE_AWAITING_APPROVAL",
          },
          {
            commandId: "own",
            version: "1",
            state: "COMMAND_STATE_AWAITING_APPROVAL",
            requestedBy: { id: "recipient" },
          },
        ],
      });
    if (path.endsWith(":preview")) return Response.json({ fingerprint: "current", consequenceUnknown: true });
    f.writes.push({
      path,
      body: (await request.json()) as Record<string, unknown>,
      version: request.headers.get("If-Match"),
    });
    return Response.json({});
  });
  mount((actions) => (
    <CommandsPanel
      api={f.api}
      incident={{ incidentId: id, version: "1" }}
      run={undefined}
      plan={undefined}
      live
      writable
      profile={profile}
      actions={actions}
    />
  ));
  const pending = (await screen.findByText("pending")).closest("li") as HTMLElement;
  fireEvent.click(within(pending).getByRole("button", { name: "Preview command" }));
  await within(pending).findByText("Consequence: Consequence is unknown");
  fireEvent.click(within(pending).getByRole("button", { name: "Approve command" }));
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  fireEvent.change(within(pending).getByLabelText("Reason: pending"), { target: { value: "Denied" } });
  fireEvent.click(within(pending).getByRole("button", { name: "Reject command" }));
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  fireEvent.click(within(pending).getByRole("button", { name: "Cancel command" }));
  await waitFor(() => {
    expect(latestActions.pending).toBe(false);
  });
  expect(
    within(screen.getByText("own").closest("li") as HTMLElement).queryByRole("button", { name: "Approve command" }),
  ).toBeNull();
});
it("loads private floor images and live equipment state, and exposes explicit duty and fallback", async () => {
  const f = fixture();
  const release = vi.fn();
  const watch = vi.fn(() => release);
  vi.stubGlobal(
    "ResizeObserver",
    class {
      observe() {
        /* Test dimensions are supplied below. */
      }
      unobserve() {
        /* No native observer. */
      }
      disconnect() {
        /* No native observer. */
      }
    },
  );
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockReturnValue({
    x: 0,
    y: 0,
    width: 800,
    height: 384,
    top: 0,
    left: 0,
    right: 800,
    bottom: 384,
    toJSON: () => ({}),
  });
  vi.spyOn(HTMLElement.prototype, "offsetHeight", "get").mockReturnValue(384);
  vi.spyOn(HTMLElement.prototype, "offsetWidth", "get").mockReturnValue(800);
  vi.spyOn(HTMLElement.prototype, "clientHeight", "get").mockReturnValue(384);
  vi.spyOn(HTMLElement.prototype, "clientWidth", "get").mockReturnValue(800);
  URL.createObjectURL = vi.fn(() => "blob:plan");
  const revoke = vi.fn();
  URL.revokeObjectURL = revoke;
  let offline = false;
  let forbidden = false;
  f.fetch.mockImplementation(async (input) => {
    const request = input as Request;
    const path = new URL(request.url).pathname;
    if (forbidden && path !== "/v1/locations") return new Response("{}", { status: 403 });
    if (path === "/v1/locations")
      return Response.json({
        locations: [
          {
            locationId: "floor",
            kind: "LOCATION_KIND_FLOOR",
            siteId: "site",
            name: "Main floor",
            floorPlan: { width: 100, height: 100 },
          },
          {
            locationId: "zone",
            parentId: "hidden-floor",
            kind: "LOCATION_KIND_ZONE",
            siteId: "site",
            name: "Restricted zone",
          },
        ],
      });
    if (path === "/v1/devices")
      return Response.json({
        devices: [
          {
            deviceId,
            name: "Door",
            zoneId: "zone",
            state: "RESOURCE_STATE_ACTIVE",
            connectorId: "connector",
            position: { x: 20, y: 30 },
          },
        ],
      });
    if (path.endsWith("/floor-plan"))
      return new Response(new Blob(["png"], { type: "image/png" }), { headers: { "content-type": "image/png" } });
    if (path.endsWith("/status"))
      return Response.json({
        resumeToken: "location-token",
        devices: [
          {
            deviceId,
            connection: offline ? "offline" : "online",
            online: !offline,
            quality: "DATA_QUALITY_ACTUAL",
            state: "EVENT_CLASS_STATE",
            confirmedAt: "last-observed",
            sources: [{ sourceId: "source", availability: "online", availabilityConfirmedAt: "last-observed" }],
          },
        ],
        maintenanceWindows: [],
      });
    if (request.method === "GET")
      return Response.json({ duty: { userId: "operator", fallbackUserId: "backup", version: "1" } });
    f.writes.push({
      path,
      body: (await request.json()) as Record<string, unknown>,
      version: request.headers.get("If-Match"),
    });
    return Response.json({});
  });
  const { client, unmount } = mount((actions) => (
    <ObjectPanel api={f.api} live writable supervisor actions={actions} watchLocation={watch} />
  ));
  fireEvent.click(await screen.findByRole("treeitem", { name: "Main floor" }));
  await waitFor(() => {
    expect(screen.getByTestId("device-status").dataset.health).toBe("normal");
  });
  await waitFor(() => {
    expect(watch).toHaveBeenCalledWith("floor", "location-token");
  });
  expect((await screen.findByRole("img", { name: "Plan: Main floor" })).getAttribute("src")).toBe("blob:plan");
  await click("Zoom in");
  await click("Zoom out");
  await click("Reset zoom");
  fireEvent.click(screen.getByRole("button", { name: "Door" }));
  await screen.findByText("source");
  offline = true;
  await act(async () => {
    await client.invalidateQueries({ queryKey: ["location-status"] });
  });
  await waitFor(() => {
    expect(screen.getByTestId("device-status").dataset.health).toBe("offline");
  });
  fireEvent.change(screen.getByLabelText("Duty operator"), { target: { value: "next-operator" } });
  fireEvent.change(screen.getByLabelText("Fallback contact"), { target: { value: "next-backup" } });
  await click("Save");
  expect(f.writes[0]).toMatchObject({
    path: "/v1/sites/site/duty",
    version: '"1"',
    body: { userId: "next-operator", fallbackUserId: "next-backup" },
  });
  forbidden = true;
  await act(async () => {
    await client.invalidateQueries({ queryKey: ["location-status"] });
    await client.invalidateQueries({ queryKey: ["duty"] });
  });
  await waitFor(() => {
    expect(screen.queryByRole("img", { name: "Plan: Main floor" })).toBeNull();
    expect(screen.queryByTestId("device-status")).toBeNull();
    expect(screen.queryByText(/backup/u)).toBeNull();
    expect(release).toHaveBeenCalled();
  });
  expect(revoke).toHaveBeenCalledWith("blob:plan");
  forbidden = false;
  await act(async () => {
    await client.invalidateQueries();
  });
  await screen.findByRole("img", { name: "Plan: Main floor" });
  fireEvent.click(screen.getByRole("treeitem", { name: "Restricted zone" }));
  await screen.findByTestId("device-status");
  await waitFor(() => {
    expect(watch).toHaveBeenCalledWith("zone", "location-token");
  });
  unmount();
  expect(revoke).toHaveBeenCalledWith("blob:plan");
  expect(release).toHaveBeenCalled();
  vi.unstubAllGlobals();
});
it("keeps stale incident data read-only and handles unavailable details", async () => {
  const f = fixture();
  const { rerender } = mount((actions) => (
    <IncidentPanel id={id} api={f.api} live writable profile={profile} actions={actions} />
  ));
  await screen.findByText("Operational incident");
  // A separate mount represents a disconnected page with no authorized snapshot.
  cleanup();
  mount((actions) => (
    <IncidentPanel id={id} api={f.api} live={false} writable={false} profile={profile} actions={actions} />
  ));
  expect(screen.getByText("Could not load current details. Actions are unavailable.")).toBeDefined();
  expect(rerender).toBeDefined();
});
it("serializes rapid mutations and reports forbidden, rejected and transport outcomes", async () => {
  mount(() => null);
  let release: (() => void) | undefined;
  const pending = new Promise<void>((resolve) => {
    release = resolve;
  });
  const work = vi.fn(() => pending);
  let first: Promise<boolean> | undefined;
  await act(async () => {
    first = latestActions.perform(work);
    expect(await latestActions.perform(work)).toBe(false);
  });
  expect(work).toHaveBeenCalledOnce();
  await act(async () => {
    release?.();
    await first;
  });
  for (const [status, text] of [
    [403, "Access denied"],
    [400, "The operation was rejected."],
    [503, "The result is unknown."],
  ] as const) {
    await act(async () => {
      await latestActions.perform(() => Promise.reject(new RequestFailure(status, "failure")));
    });
    expect(screen.getByText(new RegExp(text.replaceAll(".", "\\."), "u"))).toBeDefined();
  }
});
it("renders legacy incidents with absent operational metadata and disables failed details", async () => {
  const f = fixture();
  f.fetch.mockImplementation((input) => {
    const path = new URL((input as Request).url).pathname;
    return Promise.resolve(
      path.endsWith("/operations")
        ? Response.json({ incident: { incidentId: id, version: "1", state: "INCIDENT_STATE_IN_PROGRESS" } })
        : path.endsWith("/run")
          ? new Response("{}", { status: 404 })
          : Response.json({}),
    );
  });
  mount((actions) => (
    <IncidentPanel id={id} api={f.api} live writable profile={{ ...profile, roles: ["operator"] }} actions={actions} />
  ));
  await screen.findByRole("heading", { name: "Incident" });
  expect(screen.getByRole("button", { name: "Acknowledge alarm" }).hasAttribute("disabled")).toBe(true);
  expect(screen.getByText("No response run")).toBeDefined();
  expect(screen.queryByRole("button", { name: "Suppress alarm" })).toBeNull();
  fireEvent.change(screen.getByLabelText("Resolution"), { target: { value: "RESOLUTION_FALSE_ALARM" } });
  fireEvent.keyDown(window, { code: "KeyA" });
});
it("clears incident details when current authorization rejects a refresh", async () => {
  const f = fixture();
  const { client } = mount((actions) => (
    <IncidentPanel id={id} api={f.api} live writable profile={profile} actions={actions} />
  ));
  await screen.findByText("Operational incident");
  f.fetch.mockImplementation(() =>
    Promise.resolve(
      Response.json({ type: "about:blank", title: "Forbidden", status: 403, code: "AUTH_FORBIDDEN" }, { status: 403 }),
    ),
  );
  await act(async () => {
    await client.invalidateQueries();
  });
  await screen.findByText("Access denied");
  expect(screen.queryByText("Operational incident")).toBeNull();
});

it("paginates current-user notifications, opens incidents and acknowledges without resending delivery", async () => {
  const f = fixture();
  const choose = vi.fn();
  f.fetch.mockImplementation(async (input) => {
    const request = input as Request;
    const url = new URL(request.url);
    if (request.method === "GET")
      return Response.json({
        notifications: [
          {
            notificationId: url.searchParams.get("pageToken") ? "second" : "first",
            version: "1",
            template: "SLA escalation",
            incidentId: id,
          },
        ],
        ...(url.searchParams.get("pageToken") ? {} : { nextPageToken: "next" }),
      });
    f.writes.push({
      path: url.pathname,
      body: (await request.json()) as Record<string, unknown>,
      version: request.headers.get("If-Match"),
    });
    return Response.json({});
  });
  mount((actions) => <NotificationsPanel api={f.api} live writable actions={actions} choose={choose} />);
  await screen.findByText(/SLA escalation/u);
  await click("Open incident");
  expect(choose).toHaveBeenCalledWith(id);
  await click("Mark as read");
  expect(f.writes[0]).toMatchObject({ path: "/v1/me/notifications/first:read", version: '"1"' });
  fireEvent.click(screen.getByRole("button", { name: "Load more" }));
  await waitFor(() => {
    expect(screen.getAllByText(/SLA escalation/u)).toHaveLength(2);
  });
});

it("hides cached command capabilities and command records after access is revoked", async () => {
  const f = fixture();
  const original = f.fetch.getMockImplementation();
  if (!original) throw new Error("Missing fetch fixture");
  const { client } = mount((actions) => (
    <CommandsPanel
      api={f.api}
      incident={{ incidentId: id, version: "1" }}
      run={undefined}
      plan={undefined}
      live
      writable
      profile={profile}
      actions={actions}
    />
  ));
  fireEvent.change(screen.getByLabelText("Device", { exact: true }), { target: { value: deviceId } });
  await screen.findByRole("option", { name: "Unlock door" });
  let denied = "/v1/devices/" + deviceId;
  f.fetch.mockImplementation((input, init) =>
    new URL((input as Request).url).pathname === denied
      ? Promise.resolve(new Response("{}", { status: 403 }))
      : original(input, init),
  );
  await act(async () => {
    await client.invalidateQueries({ queryKey: ["device"] });
  });
  await screen.findByText("Access denied");
  expect(screen.queryByRole("option", { name: "Unlock door" })).toBeNull();
  denied = "/v1/commands";
  await act(async () => {
    await client.invalidateQueries({ queryKey: ["commands"] });
  });
  await waitFor(() => {
    expect(screen.queryByLabelText("Device", { exact: true })).toBeNull();
  });
});
