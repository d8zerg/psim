import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { QueryClient, QueryClientProvider } from "@tanstack/react-query";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { useEffect, type ReactNode } from "react";
import { AdminApi, write } from "./api";
import { Editor, GuardButton, type Permissions } from "./editor";
import { useWrites, WriteNotice, type Writes } from "./actions";
import { FloorPlans, Imports } from "./files";
import { resources, type ResourceKey, type RecordValue } from "./model";
import { setPreferences } from "../ui/preferences";
const clients: QueryClient[] = [];
afterEach(() => {
  cleanup();
  clients.forEach((client) => {
    client.clear();
  });
  vi.restoreAllMocks();
});
beforeEach(() => {
  setPreferences({ language: "en", mode: "normal" });
});
function mount(children: ReactNode) {
  const client = new QueryClient({ defaultOptions: { queries: { retry: false } } });
  clients.push(client);
  return render(<QueryClientProvider client={client}>{children}</QueryClientProvider>);
}
function fixture(key: ResourceKey = "locations") {
  const requests: { path: string; options: RequestInit; body: RecordValue }[] = [];
  let failure = 0;
  let validation = true;
  let invalidResponse = false;
  const fetcher = vi.fn<typeof fetch>().mockImplementation((input, options = {}) => {
    const path = input instanceof Request ? input.url : input instanceof URL ? input.href : input;
    const body = typeof options.body === "string" ? (JSON.parse(options.body) as RecordValue) : {};
    requests.push({ path, options, body });
    if (failure) {
      const code = failure;
      failure = 0;
      return Promise.resolve(
        Response.json(
          {
            type: "about:blank",
            title: "Rejected",
            code: "COMMON_CONCURRENT_MODIFICATION",
            status: code,
            errors: [{ field: "name", message: "Invalid name" }],
          },
          { status: code },
        ),
      );
    }
    if (path.endsWith("/active"))
      return Promise.resolve(
        Response.json({
          ruleSet: {
            version: "8",
            rules: [
              { ruleId: "other", version: 2 },
              { ruleId: "id", version: 1 },
            ],
          },
        }),
      );
    if (path.includes(":validate")) return Promise.resolve(Response.json({ valid: validation, violations: [] }));
    if (path.includes(":dry-run") || path.includes("/dry-runs/"))
      return Promise.resolve(
        Response.json({
          dryRun: { dryRunId: "run", state: "DRY_RUN_STATE_COMPLETED", eventsProcessed: "5", signalsProduced: "1" },
        }),
      );
    if (path.includes(":activate")) return Promise.resolve(Response.json({ ruleSet: { version: "9" } }));
    return Promise.resolve(
      Response.json(
        invalidResponse
          ? {}
          : {
              [resources[key].item]: {
                ...body,
                [resources[key].id]: "id",
                name: "Saved record",
                version: key === "rules" || key === "plans" ? 2 : "2",
              },
            },
      ),
    );
  });
  const api = new AdminApi(() => Promise.resolve("token"), fetcher);
  const permissions: Permissions = { writable: true, global: true, denied: vi.fn() };
  return {
    api,
    permissions,
    requests,
    fetcher,
    fail: (code: number) => {
      failure = code;
    },
    invalid: () => {
      validation = false;
    },
    invalidResponse: () => {
      invalidResponse = true;
    },
  };
}
async function confirm(name: string) {
  fireEvent.click(screen.getByRole<HTMLButtonElement>("button", { name }));
  const dialog = await screen.findByRole("alertdialog");
  fireEvent.click(within(dialog).getByRole("button", { name: "Confirm action" }));
}
it("keeps a local edit after conflicts and supports explicit reload and discard", async () => {
  const f = fixture();
  const reload = vi.fn();
  const dirty = vi.fn();
  mount(
    <Editor
      resourceKey="locations"
      value={{ locationId: "id", name: "Original", version: "1" }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={dirty}
      reload={reload}
    />,
  );
  fireEvent.change(screen.getByLabelText<HTMLInputElement>("Name"), { target: { value: "Local edit" } });
  f.fail(412);
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  expect(await screen.findByText(/saved version changed/u)).toBeDefined();
  expect(screen.getByLabelText<HTMLInputElement>("Name").value).toBe("Local edit");
  expect(new Headers(f.requests[0]?.options.headers).get("if-match")).toBe('"1"');
  await confirm("Reload saved data");
  expect(reload).toHaveBeenCalledOnce();
  expect(dirty).toHaveBeenCalledWith(true);
});
it("retries an unknown result with precisely the original idempotency key and body", async () => {
  const f = fixture();
  f.fail(503);
  mount(
    <Editor
      resourceKey="locations"
      value={{ locationId: "id", name: "Original", version: "1" }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  await screen.findByText(/request result is unknown/u);
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Save" }).disabled).toBe(true);
  fireEvent.click(screen.getByRole<HTMLButtonElement>("button", { name: "Retry exact request" }));
  await screen.findByText(/Saved record/u);
  expect(f.requests[0]?.options.body).toBe(f.requests[1]?.options.body);
  expect(new Headers(f.requests[0]?.options.headers).get("idempotency-key")).toBe(
    new Headers(f.requests[1]?.options.headers).get("idempotency-key"),
  );
});
it("hides forbidden results and keeps unauthorized and offline forms read-only", async () => {
  const f = fixture();
  f.fail(403);
  const view = mount(
    <Editor
      resourceKey="locations"
      value={{ locationId: "id", name: "Original", version: "1" }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  await waitFor(() => {
    expect(f.permissions.denied).toHaveBeenCalledOnce();
  });
  view.unmount();
  mount(
    <Editor
      resourceKey="retention"
      value={{ version: "1" }}
      existing
      api={f.api}
      permissions={{ ...f.permissions, global: false }}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Save" }).disabled).toBe(true);
  expect(screen.getByText(/entire tenant/u)).toBeDefined();
});
it("validates and publishes the saved plan, and invalidates validation on local changes", async () => {
  const f = fixture("plans");
  mount(
    <Editor
      resourceKey="plans"
      value={{
        planId: "id",
        name: "Plan",
        version: 1,
        steps: [{ stepId: "start", title: "Start", instruction: { text: "Inspect" } }],
      }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  const publish = screen.getByRole<HTMLButtonElement>("button", { name: "Publish saved version" });
  expect(publish.disabled).toBe(true);
  fireEvent.click(screen.getByRole<HTMLButtonElement>("button", { name: "Validate saved version" }));
  await waitFor(() => {
    expect(publish.disabled).toBe(false);
  });
  fireEvent.change(screen.getByLabelText("Name"), { target: { value: "Changed" } });
  expect(publish.disabled).toBe(true);
  fireEvent.change(screen.getByLabelText("Name"), { target: { value: "Plan" } });
  fireEvent.click(screen.getByRole("button", { name: "Validate saved version" }));
  await waitFor(() => {
    expect(publish.disabled).toBe(false);
  });
  await confirm("Publish saved version");
  await screen.findByText(/Saved record/u);
  expect(f.requests.at(-1)?.body).toEqual({ version: 1 });
  expect(f.requests.at(-1)?.path).toBe("/v1/plans/id:publish");
});
it("runs a validated rule over history and preserves every other active rule during activation", async () => {
  const f = fixture("rules");
  mount(
    <Editor
      resourceKey="rules"
      value={{ ruleId: "id", name: "Rule", version: 1, single: {}, filter: {}, output: {} }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.click(screen.getByRole("button", { name: "Validate saved version" }));
  const activate = screen.getByRole<HTMLButtonElement>("button", { name: "Activate saved rule" });
  await waitFor(() => {
    expect(activate.disabled).toBe(false);
  });
  fireEvent.change(screen.getByLabelText("History start (UTC)"), { target: { value: "2030-01-01T00:00:00Z" } });
  fireEvent.change(screen.getByLabelText("History end (UTC)"), { target: { value: "2030-01-01T01:00:00Z" } });
  fireEvent.change(screen.getByLabelText("History source"), { target: { value: "DRY_RUN_SOURCE_CLICKHOUSE" } });
  fireEvent.click(screen.getByRole("button", { name: "Run against history" }));
  await waitFor(() => {
    expect(f.requests.some((r) => r.path === "/v1/dry-runs/run")).toBe(true);
  });
  await confirm("Activate saved rule");
  await waitFor(() => {
    expect(f.requests.at(-1)?.path).toBe("/v1/rule-sets:activate");
  });
  expect(f.requests.at(-1)?.body).toEqual({
    rules: [
      { ruleId: "other", version: 2 },
      { ruleId: "id", version: 1 },
    ],
    expectedRuleSetVersion: "8",
  });
});
it("rejects failed validation and saves new drafts with their server-assigned versions", async () => {
  const f = fixture("plans");
  f.invalid();
  mount(
    <Editor
      resourceKey="plans"
      value={{ planId: "id", name: "Plan", version: 1 }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.click(screen.getByRole("button", { name: "Validate saved version" }));
  await waitFor(() => {
    expect(f.requests).toHaveLength(1);
  });
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Publish saved version" }).disabled).toBe(true);
  fireEvent.click(screen.getByRole<HTMLButtonElement>("button", { name: "Save" }));
  await screen.findByText(/Saved record/u);
  expect(f.requests.at(-1)?.body.state).toBe("PLAN_STATE_DRAFT");
});
it.each(["locations", "rules", "plans"] as const)("requires confirmation before archiving %s", async (key) => {
  const f = fixture(key);
  mount(
    <Editor
      resourceKey={key}
      value={{ [resources[key].id]: "id", name: "Original", version: 1 }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  await confirm("Archive");
  await screen.findByText(/Saved record/u);
  expect(f.requests[0]?.path).toBe(`${resources[key].path}/id:archive`);
});
it("requires a reason for ending maintenance and confirms the exact target", async () => {
  const f = fixture("maintenance");
  mount(
    <Editor
      resourceKey="maintenance"
      value={{ maintenanceId: "id", name: "Window", version: "1" }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  const fields = screen.getAllByLabelText("Reason: End maintenance");
  fireEvent.change(fields.at(-1) ?? document.body, { target: { value: "Work completed" } });
  await confirm("End maintenance");
  await screen.findByText(/Saved record/u);
  expect(f.requests[0]?.body).toEqual({ reason: "Work completed" });
});
it("registers a connector with public PEM and supports renewal and reasoned revocation", async () => {
  const f = fixture("connectors");
  mount(
    <Editor
      resourceKey="connectors"
      value={{ connectorId: "id", name: "New connector" }}
      existing={false}
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.change(screen.getByLabelText("Public certificate PEM"), { target: { value: "PUBLIC CERT" } });
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  await screen.findByText(/Saved record/u);
  fireEvent.change(screen.getByLabelText("Public certificate PEM"), { target: { value: "RENEWED CERT" } });
  await confirm("Renew certificate");
  await waitFor(() => {
    expect(f.requests.at(-1)?.path).toBe("/v1/connectors/id:rotate-certificate");
  });
  fireEvent.change(screen.getByLabelText("Reason: Revoke connector"), { target: { value: "Retired" } });
  await confirm("Revoke connector");
  await waitFor(() => {
    expect(f.requests.at(-1)?.body).toEqual({ reason: "Retired" });
  });
});
let actions: Writes;
function WriteHarness({ api, denied }: { readonly api: AdminApi; readonly denied: () => void }) {
  const current = useWrites(api, denied);
  useEffect(() => {
    actions = current;
  }, [current]);
  return <WriteNotice actions={current} writable />;
}
it("prevents concurrent writes and new operations until an uncertain result is resolved", async () => {
  const f = fixture();
  let finish: ((response: Response) => void) | undefined;
  f.fetcher.mockImplementationOnce(
    () =>
      new Promise((resolve) => {
        finish = resolve;
      }),
  );
  mount(<WriteHarness api={f.api} denied={f.permissions.denied} />);
  let first: Promise<void> | undefined;
  act(() => {
    first = actions.perform(write("/v1/locations", {}));
  });
  await act(async () => {
    await actions.perform(write("/v1/locations", { name: "duplicate" }));
  });
  expect(f.fetcher).toHaveBeenCalledTimes(1);
  await act(async () => {
    finish?.(new Response("bad", { status: 500 }));
    await first;
  });
  await act(async () => {
    await actions.perform(write("/v1/locations", { name: "changed" }));
  });
  expect(f.fetcher).toHaveBeenCalledTimes(1);
  fireEvent.click(screen.getByRole("button", { name: "Retry exact request" }));
  await screen.findByText(/Saved record/u);
});
it("reports rejected fields and discards invalid write responses", async () => {
  const f = fixture();
  f.fail(400);
  mount(<WriteHarness api={f.api} denied={f.permissions.denied} />);
  await act(async () => {
    await actions.perform(write("/v1/locations", {}));
  });
  expect(screen.getByText("name: Invalid name")).toBeDefined();
  cleanup();
  f.invalidResponse();
  mount(
    <Editor
      resourceKey="locations"
      value={{ locationId: "id", name: "Original", version: "1" }}
      existing
      api={f.api}
      permissions={f.permissions}
      onDirty={vi.fn()}
      reload={vi.fn()}
    />,
  );
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  await screen.findByText(/request result is unknown/u);
});
it("checks an immutable import file before applying it and displays the persisted job", async () => {
  const f = fixture();
  f.fetcher.mockImplementation((input) =>
    Promise.resolve(
      Response.json(
        (input instanceof Request ? input.url : input instanceof URL ? input.href : input).includes("/imports?")
          ? { importJob: { importId: "job" } }
          : { importJob: { importId: "job", state: "IMPORT_STATE_COMPLETED", rowsFailed: 0, rowsCreated: 2 } },
      ),
    ),
  );
  mount(<Imports api={f.api} permissions={f.permissions} />);
  const input = screen.getByLabelText("File");
  fireEvent.change(input, { target: { files: [new File(["bad"], "bad.exe")] } });
  expect(screen.getByText(/Unsupported file/u)).toBeDefined();
  fireEvent.change(input, { target: { files: [new File(["[]"], "devices.json", { type: "application/json" })] } });
  fireEvent.click(screen.getByRole("button", { name: "Check import" }));
  await waitFor(() => {
    expect(screen.getByRole<HTMLButtonElement>("button", { name: "Apply checked import" }).disabled).toBe(false);
  });
  await confirm("Apply checked import");
  await waitFor(() => {
    expect(
      f.fetcher.mock.calls.some(([path]) =>
        (path instanceof Request ? path.url : path instanceof URL ? path.href : path).includes("dryRun=false"),
      ),
    ).toBe(true);
  });
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Apply checked import" }).disabled).toBe(true);
});
it("reads private plan versions, uploads against the floor version and releases image URLs", async () => {
  const f = fixture();
  const revoke = vi.fn();
  vi.stubGlobal("URL", Object.assign(URL, { createObjectURL: vi.fn(() => "blob:private"), revokeObjectURL: revoke }));
  vi.stubGlobal(
    "createImageBitmap",
    vi.fn(() => Promise.resolve({ width: 10, height: 10, close: vi.fn() })),
  );
  f.fetcher.mockImplementation((input, options) => {
    const path = input instanceof Request ? input.url : input instanceof URL ? input.href : input;
    if (path.includes("/floor-plan?version"))
      return Promise.resolve(new Response("png", { headers: { "Content-Type": "image/png" } }));
    if (path.includes("/versions"))
      return Promise.resolve(
        Response.json({
          files: [{ version: "1", sha256: "checksum" }],
          nextPageToken: path.includes("pageToken=next") ? "" : "next",
        }),
      );
    if (options?.method === "PUT") return Promise.resolve(Response.json({ file: { version: "2" } }));
    return Promise.resolve(
      Response.json({ location: { locationId: "floor", kind: "LOCATION_KIND_FLOOR", version: "3", name: "Floor" } }),
    );
  });
  mount(<FloorPlans api={f.api} permissions={f.permissions} />);
  fireEvent.change(screen.getByLabelText(/Floor identifier/u), { target: { value: "floor" } });
  fireEvent.click(screen.getByRole("button", { name: "Select record" }));
  await screen.findByText("Version: 1 / checksum");
  fireEvent.click(screen.getByRole("button", { name: "View version 1" }));
  await screen.findByRole("img");
  fireEvent.click(screen.getByRole("button", { name: "Close preview" }));
  await waitFor(() => {
    expect(revoke).toHaveBeenCalled();
  });
  fireEvent.change(screen.getByLabelText("File"), {
    target: { files: [new File(["png"], "plan.png", { type: "image/png" })] },
  });
  fireEvent.change(screen.getByLabelText("Plan width"), { target: { value: "200" } });
  fireEvent.change(screen.getByLabelText("Plan height"), { target: { value: "150" } });
  fireEvent.click(screen.getByRole("button", { name: "Upload plan" }));
  await waitFor(() => {
    expect(f.fetcher.mock.calls.some(([, options]) => options?.method === "PUT")).toBe(true);
  });
  const call = f.fetcher.mock.calls.find(([, options]) => options?.method === "PUT");
  expect(new Headers(call?.[1]?.headers).get("if-match")).toBe('"3"');
  fireEvent.click(screen.getByRole("button", { name: "Next page" }));
  await screen.findByRole("button", { name: "First page" });
  fireEvent.click(screen.getByRole("button", { name: "First page" }));
});
it("supports explicit cancellation of a dirty form without executing its action", async () => {
  const action = vi.fn();
  mount(<GuardButton dirty text="Navigate" action={action} />);
  fireEvent.click(screen.getByRole("button", { name: "Navigate" }));
  fireEvent.click(within(await screen.findByRole("alertdialog")).getByRole("button", { name: "Cancel editing" }));
  expect(action).not.toHaveBeenCalled();
});
