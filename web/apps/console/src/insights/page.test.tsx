import { StrictMode } from "react";
import { act, cleanup, fireEvent, render, screen, waitFor } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { focusManager } from "@tanstack/react-query";
import { InsightsPage } from "./page";
import { setPreferences } from "../ui/preferences";
const session = vi.hoisted(() => ({ profile: { context: "insights" } }));
const shell = vi.hoisted(() => ({ auth: { token: () => Promise.resolve("token") } }));
vi.mock("../shell/context", () => ({ useShell: () => ({ shell, session }) }));
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});
beforeEach(() => {
  setPreferences({ language: "en", mode: "normal" });
  session.profile.context = "insights";
  focusManager.setFocused(true);
});
const timestamp = new Date().toISOString();
function fixture(audit = true) {
  const state = {
    roles: audit ? ["auditor"] : ["manager"],
    sites: ["*"],
    fail: 0,
    target: "",
    empty: false,
    broken: false,
    checkpoint: true,
    groups: 1,
    huge: false,
    scopeVersion: 0,
    mime: true,
  };
  const calls: { path: string; options: RequestInit }[] = [];
  const row = (index = 0) => ({
    auditSeq: String(index + 1),
    occurredAt: timestamp,
    recordedAt: timestamp,
    sourceEventId: "source",
    tenantId: "tenant",
    hash: "hash",
    prevHash: "prev",
    entry: {
      actor: { id: "actor" },
      action: `incident.action-${String(state.scopeVersion)}`,
      objectId: "incident",
      objectType: "incident",
      siteId: "site",
      outcome: "AUDIT_OUTCOME_SUCCESS",
      details: { plain: "<script>escaped</script>" },
    },
  });
  const totals = {
    incidentsCreated: state.huge ? "18446744073709551615" : "10",
    incidentsResolved: "10",
    mtta: "60s",
    mttr: "120s",
    falseAlarmRate: 0.2,
    ackSlaCompliance: 0.5,
    resolveSlaCompliance: 1,
    ackSlaEvaluated: "10",
    resolveSlaEvaluated: "10",
    escalated: "1",
  };
  const fetcher = vi.fn<typeof fetch>().mockImplementation((input, options = {}) => {
    const path = input instanceof Request ? input.url : String(input);
    calls.push({ path, options });
    if (state.fail && (!state.target || path.includes(state.target))) {
      const status = state.fail;
      state.fail = 0;
      return Promise.resolve(
        Response.json({ type: "about:blank", title: "Failure", code: "COMMON_UNAVAILABLE", status }, { status }),
      );
    }
    if (path === "/v1/me")
      return Promise.resolve(
        Response.json({
          userId: "reader",
          tenantId: "tenant",
          roles: state.roles,
          siteIds: state.sites,
          actions: ["audit.search", "report.read"],
        }),
      );
    if (path.includes(":export"))
      return Promise.resolve(
        new Response("file", {
          headers: { "Content-Type": state.mime ? (audit ? "application/x-ndjson" : "text/csv") : "text/html" },
        }),
      );
    if (path.includes("audit:verify"))
      return Promise.resolve(
        Response.json({
          intact: !state.broken,
          recordsChecked: "9007199254740993",
          ...(state.broken ? { firstBrokenSeq: "9007199254740993", detail: "hash mismatch" } : {}),
          ...(state.checkpoint
            ? {
                lastCheckpoint: {
                  auditSeq: "10",
                  hash: "hash",
                  keyId: "signer",
                  signature: "signed",
                  tenantId: "tenant",
                  createdAt: timestamp,
                },
              }
            : {}),
        }),
      );
    if (path.includes("audit/records"))
      return Promise.resolve(
        Response.json({
          records: state.empty ? [] : [row(path.includes("pageToken") ? 1 : 0)],
          nextPageToken: path.includes("pageToken") ? "" : "next",
        }),
      );
    return Promise.resolve(
      Response.json({
        totals: state.empty ? {} : { ...totals, ...(state.huge ? { incidentsCreated: "18446744073709551615" } : {}) },
        rows: state.empty
          ? []
          : Array.from({ length: state.groups }, (_, index) => ({
              ...totals,
              group: index === 0 ? "" : String(index),
            })),
        generatedAt: timestamp,
      }),
    );
  });
  vi.stubGlobal("fetch", fetcher);
  const create = vi.fn(() => "blob:export"),
    revoke = vi.fn();
  vi.stubGlobal(
    "URL",
    class extends URL {
      static override createObjectURL = create;
      static override revokeObjectURL = revoke;
    },
  );
  vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => undefined);
  return { state, calls, fetcher, create, revoke };
}
async function start(audit = true) {
  await screen.findByRole("button", { name: audit ? "Search audit" : "Build report" });
  fireEvent.click(screen.getByRole("button", { name: audit ? "Search audit" : "Build report" }));
}
async function refreshAccess() {
  await act(async () => {
    focusManager.setFocused(false);
    focusManager.setFocused(true);
    await Promise.resolve();
  });
}
it("searches fixed filters, paginates, reveals escaped details and blocks export after draft changes", async () => {
  const f = fixture();
  render(
    <StrictMode>
      <InsightsPage audit />
    </StrictMode>,
  );
  await start();
  await screen.findAllByText("incident.action-0");
  fireEvent.click(screen.getByText("Audit record 1"));
  expect(screen.getByText("<script>escaped</script>")).toBeTruthy();
  expect(document.querySelector("script")).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Next page" }));
  await screen.findByText("Audit record 2");
  expect(screen.getByRole("button", { name: "Next page" }).hasAttribute("disabled")).toBe(true);
  fireEvent.click(screen.getByRole("button", { name: "Previous page" }));
  await screen.findByText("Audit record 1");
  fireEvent.change(screen.getByLabelText("Action prefix"), { target: { value: "access." } });
  expect(screen.getByRole("button", { name: "Export JSONL" }).hasAttribute("disabled")).toBe(true);
  fireEvent.change(screen.getByLabelText("Actor ID"), { target: { value: "user" } });
  fireEvent.change(screen.getByLabelText("Site ID"), { target: { value: "site" } });
  fireEvent.change(screen.getByLabelText("Object type"), { target: { value: "incident" } });
  fireEvent.change(screen.getByLabelText("Object ID"), { target: { value: "id" } });
  fireEvent.change(screen.getByLabelText("Outcome"), { target: { value: "AUDIT_OUTCOME_DENIED" } });
  await start();
  await waitFor(() => {
    expect(
      f.calls.some(
        (call) => call.path.includes("query.actorId=user") && call.path.includes("query.outcome=AUDIT_OUTCOME_DENIED"),
      ),
    ).toBe(true);
  });
  fireEvent.click(screen.getByRole("button", { name: "Export JSONL" }));
  await screen.findByText("Export downloaded");
  expect(f.create).toHaveBeenCalledTimes(1);
  fireEvent.click(screen.getByRole("button", { name: "Export JSONL" }));
  await waitFor(() => {
    expect(f.create).toHaveBeenCalledTimes(2);
  });
  expect(f.revoke).toHaveBeenCalledTimes(1);
});
it("verifies the entire chain, reports a precise broken sequence and distinguishes absent checkpoints", async () => {
  const f = fixture();
  render(<InsightsPage audit />);
  fireEvent.click(await screen.findByRole("button", { name: "Verify integrity" }));
  await screen.findByText("Chain intact");
  expect(screen.getByText("Signed checkpoint")).toBeTruthy();
  expect(screen.getByText("9,007,199,254,740,993")).toBeTruthy();
  fireEvent.change(screen.getByLabelText("First sequence (0 = first)"), { target: { value: "9" } });
  fireEvent.change(screen.getByLabelText("Last sequence (0 = last)"), { target: { value: "8" } });
  expect(screen.getByRole("button", { name: "Verify integrity" }).hasAttribute("disabled")).toBe(true);
  fireEvent.change(screen.getByLabelText("Last sequence (0 = last)"), { target: { value: "0" } });
  f.state.broken = true;
  f.state.checkpoint = false;
  fireEvent.click(screen.getByRole("button", { name: "Verify integrity" }));
  await screen.findByText("Chain integrity broken");
  expect(screen.getByText("9007199254740993")).toBeTruthy();
  expect(screen.getByText("No signed checkpoint")).toBeTruthy();
  f.state.fail = 503;
  f.state.target = "verify";
  fireEvent.click(screen.getByRole("button", { name: "Verify integrity" }));
  await screen.findByText("Request failed. Check the connection and submit again.");
});
it("limits scoped auditors to search and hides all results after a forbidden read", async () => {
  const f = fixture();
  f.state.sites = ["site"];
  render(<InsightsPage audit />);
  await start();
  await screen.findByText("Audit record 1");
  expect(screen.queryByRole("button", { name: "Verify integrity" })).toBeNull();
  expect(screen.getByRole("button", { name: "Export JSONL" }).hasAttribute("disabled")).toBe(true);
  f.state.fail = 403;
  f.state.target = "audit/records";
  await start();
  await screen.findByText("Access is unavailable. Previous data has been hidden.");
  expect(screen.queryAllByText("incident.action-0")).toHaveLength(0);
  f.state.target = "";
  fireEvent.click(screen.getByRole("button", { name: "Check access again" }));
  await screen.findByRole("button", { name: "Search audit" });
  expect(screen.queryByText("Audit record 1")).toBeNull();
});
it("destroys data and drafts when effective scopes change without a token change", async () => {
  const f = fixture();
  render(<InsightsPage audit />);
  await start();
  await screen.findAllByText("incident.action-0");
  f.state.sites = ["site"];
  f.state.scopeVersion = 1;
  await refreshAccess();
  await waitFor(() => {
    expect(screen.queryAllByText("incident.action-0")).toHaveLength(0);
  });
  expect(screen.queryByRole("button", { name: "Verify integrity" })).toBeNull();
  await start();
  await screen.findAllByText("incident.action-1");
  f.state.roles = ["operator"];
  await refreshAccess();
  await screen.findByText("Access is unavailable. Previous data has been hidden.");
});
it("renders server totals exactly, paginates group tables and exports only applied filters", async () => {
  const f = fixture(false);
  f.state.groups = 51;
  f.state.huge = true;
  render(<InsightsPage audit={false} />);
  await start(false);
  await waitFor(() => {
    expect(screen.getByTestId("total-incidentsCreated").textContent).toBe("18,446,744,073,709,551,615");
  });
  expect(screen.getByTestId("total-mtta").textContent).toBe("60");
  expect(screen.getByTestId("total-falseAlarmRate").textContent).toBe("20%");
  expect(screen.getAllByRole("img")).toHaveLength(2);
  expect(document.querySelectorAll("svg g")).toHaveLength(40);
  fireEvent.click(screen.getByRole("button", { name: "Next page" }));
  expect(screen.getByText("Groups on this page: 1 / 51")).toBeTruthy();
  fireEvent.click(screen.getByRole("button", { name: "Previous page" }));
  fireEvent.change(screen.getByLabelText("Site IDs"), { target: { value: "site-a,site-b" } });
  fireEvent.change(screen.getByLabelText("Incident type IDs"), { target: { value: "type-a" } });
  fireEvent.change(screen.getByLabelText("Group by"), { target: { value: "PRIORITY" } });
  fireEvent.change(screen.getByLabelText("Priorities"), { target: { value: "PRIORITY_P2" } });
  expect(screen.getByRole("button", { name: "Export CSV" }).hasAttribute("disabled")).toBe(true);
  await start(false);
  await screen.findByText("P1");
  fireEvent.click(screen.getByRole("button", { name: "Export CSV" }));
  await screen.findByText("Export downloaded");
  expect(f.calls.at(-1)?.path).toContain("filter.siteIds=site-a&filter.siteIds=site-b");
  expect(f.calls.at(-1)?.path).toContain("grouping=REPORT_GROUPING_PRIORITY");
});
it("handles empty metrics, total grouping, invalid dates and unavailable networks", async () => {
  const f = fixture(false);
  f.state.empty = true;
  render(<InsightsPage audit={false} />);
  await start(false);
  await screen.findByText("No matching records");
  expect(screen.getByTestId("total-ackSlaCompliance").textContent).toBe("No observations");
  fireEvent.change(screen.getByLabelText("Group by"), { target: { value: "NONE" } });
  await start(false);
  await waitFor(() => {
    expect(screen.getAllByRole("img")).toHaveLength(2);
  });
  fireEvent.change(screen.getByLabelText("From (UTC, inclusive)"), { target: { value: "" } });
  expect(screen.getByRole("button", { name: "Build report" }).hasAttribute("disabled")).toBe(true);
  Object.defineProperty(navigator, "onLine", { configurable: true, get: () => false });
  fireEvent(window, new Event("offline"));
  expect(screen.getByText("No network connection. Displayed data may be out of date.")).toBeTruthy();
  Object.defineProperty(navigator, "onLine", { configurable: true, get: () => true });
  fireEvent(window, new Event("online"));
});
it("offers explicit retries for initial and result failures without automatic repetition", async () => {
  const f = fixture();
  f.state.fail = 503;
  render(<InsightsPage audit />);
  await screen.findByText("Request failed. Check the connection and submit again.");
  fireEvent.click(screen.getByRole("button", { name: "Check access again" }));
  await screen.findByRole("button", { name: "Search audit" });
  f.state.fail = 503;
  f.state.target = "audit/records";
  await start();
  await screen.findByText("Request failed. Check the connection and submit again.");
  await start();
  await screen.findByText("Audit record 1");
  f.state.empty = true;
  await start();
  await screen.findByText("No matching records");
});
it("rejects incorrect export media and isolates authentication failures from download errors", async () => {
  const f = fixture();
  render(<InsightsPage audit />);
  await start();
  await screen.findByText("Audit record 1");
  f.state.mime = false;
  fireEvent.click(screen.getByRole("button", { name: "Export JSONL" }));
  await screen.findByText("Export failed. No file was downloaded.");
  expect(f.create).not.toHaveBeenCalled();
  f.state.fail = 401;
  f.state.target = ":export";
  fireEvent.click(screen.getByRole("button", { name: "Export JSONL" }));
  await screen.findByText("Access is unavailable. Previous data has been hidden.");
});
it("aborts verification when the session changes and prevents late results", async () => {
  const f = fixture();
  let finish: ((value: Response) => void) | undefined;
  const fetcher = f.fetcher.getMockImplementation();
  f.fetcher.mockImplementation((input, options) =>
    (input instanceof Request ? input.url : String(input)).includes("audit:verify")
      ? new Promise((resolve) => {
          finish = resolve;
        })
      : (fetcher?.(input, options) ?? Promise.resolve(Response.json({}))),
  );
  const view = render(<InsightsPage audit />);
  fireEvent.click(await screen.findByRole("button", { name: "Verify integrity" }));
  await waitFor(() => {
    expect(finish).toBeDefined();
  });
  session.profile.context = "new";
  view.rerender(<InsightsPage audit />);
  await act(async () => {
    finish?.(Response.json({ intact: true }));
    await Promise.resolve();
  });
  expect(screen.queryByText("Chain intact")).toBeNull();
});
it("renders the Russian interface and denies an obsolete role before searching", async () => {
  const f = fixture();
  setPreferences({ language: "ru" });
  render(<InsightsPage audit />);
  await screen.findByRole("button", { name: "Найти записи" });
  fireEvent.click(screen.getByRole("button", { name: "Найти записи" }));
  await screen.findByText("Запись аудита 1");
  f.state.roles = ["manager"];
  await refreshAccess();
  await screen.findByText("Доступ недоступен. Прежние данные скрыты.");
});
