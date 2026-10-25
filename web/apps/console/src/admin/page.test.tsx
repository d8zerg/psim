import { StrictMode } from "react";
import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { AdministrationPage } from "./page";
import { setPreferences } from "../ui/preferences";
const session = vi.hoisted(() => ({ profile: { context: "admin", roles: ["admin"] } }));
const shell = vi.hoisted(() => ({ auth: { token: () => Promise.resolve("token") } }));
vi.mock("../shell/context", () => ({ useShell: () => ({ shell, session }) }));
afterEach(() => {
  cleanup();
  vi.unstubAllGlobals();
  vi.restoreAllMocks();
});
beforeEach(() => {
  setPreferences({ language: "en", mode: "normal" });
  session.profile.context = "admin";
  window.psimConfig = { issuer: "https://identity.test/realms/psim" };
});
function fixture() {
  let roles = ["admin"];
  let sites = ["*"];
  let fail = 0;
  let name = "Original";
  const requests: { path: string; options: RequestInit }[] = [];
  const fetcher = vi.fn<typeof fetch>().mockImplementation((input, options = {}) => {
    const path = input instanceof Request ? input.url : input instanceof URL ? input.href : input;
    requests.push({ path, options });
    if (fail) {
      const status = fail;
      fail = 0;
      return Promise.resolve(
        Response.json({ type: "about:blank", title: "Denied", code: "AUTH_SCOPE_DENIED", status }, { status }),
      );
    }
    if (path === "/v1/me")
      return Promise.resolve(Response.json({ userId: "admin", roles, siteIds: sites, actions: ["catalog.write"] }));
    if (path === "/v1/settings")
      return Promise.resolve(
        Response.json({
          processing: { version: "1", sourceIdleTimeout: "60s" },
          retention: { version: "1", audit: "86400s" },
        }),
      );
    if (path.startsWith("/v1/locations")) {
      if (options.method)
        return Promise.resolve(Response.json({ location: { locationId: "created", name: "Created", version: "1" } }));
      return Promise.resolve(
        Response.json({
          locations: [{ locationId: "site", name, version: "1" }],
          nextPageToken: path.includes("pageToken=next") ? "" : "next",
        }),
      );
    }
    return Promise.resolve(Response.json({ connectors: [], mappingRuleSets: [], devices: [] }));
  });
  vi.stubGlobal("fetch", fetcher);
  return {
    fetcher,
    requests,
    fail: (code: number) => {
      fail = code;
    },
    role: (role: string) => {
      roles = [role];
    },
    scopes: (scopes: string[]) => {
      sites = scopes;
    },
    name: (next: string) => {
      name = next;
    },
  };
}
function nav(name: string) {
  fireEvent.click(within(screen.getByRole("navigation", { name: "Administration" })).getByRole("button", { name }));
}
it("loads actual catalog pages, keeps draft fields during refresh, reloads explicitly and creates new records", async () => {
  const f = fixture();
  render(<AdministrationPage />);
  fireEvent.click(await screen.findByRole("button", { name: "Original" }));
  fireEvent.change(screen.getByLabelText<HTMLInputElement>("Name"), { target: { value: "Local edit" } });
  f.name("Remote change");
  fireEvent.click(screen.getAllByRole("button", { name: "Reload saved data" })[0] ?? document.body);
  await screen.findByRole("button", { name: "Remote change" });
  expect(screen.getByLabelText<HTMLInputElement>("Name").value).toBe("Local edit");
  fireEvent.click(screen.getAllByRole("button", { name: "Reload saved data" }).at(-1) ?? document.body);
  fireEvent.click(within(await screen.findByRole("alertdialog")).getByRole("button", { name: "Confirm action" }));
  await waitFor(() => {
    expect(screen.getByLabelText<HTMLInputElement>("Name").value).toBe("Remote change");
  });
  fireEvent.click(screen.getByRole("button", { name: "Next page" }));
  await screen.findByRole("button", { name: "First page" });
  fireEvent.click(screen.getByRole("button", { name: "First page" }));
  fireEvent.change(screen.getByLabelText<HTMLInputElement>("Filter records"), { target: { value: "missing" } });
  expect(screen.queryByRole("button", { name: "Remote change" })).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Create" }));
  fireEvent.change(screen.getByLabelText("Name"), { target: { value: "Created" } });
  fireEvent.click(screen.getByRole("button", { name: "Save" }));
  await screen.findByRole("heading", { name: /Locations: Created/u });
  expect(f.requests.some((r) => r.path === "/v1/locations" && r.options.method === "POST")).toBe(true);
  nav("Processing settings");
  await waitFor(() => {
    expect(screen.getAllByRole("button", { name: "Processing settings" })).toHaveLength(2);
  });
  fireEvent.click(screen.getAllByRole("button", { name: "Processing settings" }).at(-1) ?? document.body);
  expect(await screen.findByLabelText("Source idle timeout")).toBeDefined();
});
it("restricts tenant settings for scoped administrators and exposes effective access with a safe IdP link", async () => {
  const f = fixture();
  f.scopes(["site"]);
  render(<AdministrationPage />);
  await screen.findByRole("button", { name: "Original" });
  nav("Retention");
  await screen.findByText(/entire tenant/u);
  expect(f.requests.some((r) => r.path === "/v1/settings")).toBe(false);
  nav("Roles and access scopes");
  expect(await screen.findByRole("link", { name: "Open Keycloak administration" })).toHaveProperty(
    "href",
    "https://identity.test/admin/psim/console/",
  );
  f.role("operator");
  fireEvent.click(screen.getByRole("button", { name: "Reload saved data" }));
  expect(await screen.findByText(/Previously loaded administrative data has been hidden/u)).toBeDefined();
  expect(screen.queryByRole("navigation", { name: "Administration" })).toBeNull();
});
it("shows only integration resources to integrators and no catalog data on a direct admin entry", async () => {
  const f = fixture();
  f.role("integrator");
  const view = render(<AdministrationPage integration />);
  await screen.findByRole("heading", { name: "Connectors" });
  expect(screen.queryByRole("button", { name: "Devices" })).toBeNull();
  nav("Normalization mappings");
  expect(await screen.findByRole("heading", { name: "Normalization mappings" })).toBeDefined();
  view.unmount();
  render(<AdministrationPage />);
  await screen.findByText(/Access denied/u);
});
it("disables writes on connection loss and in training mode, preserves fields, and clears them after denial", async () => {
  const f = fixture();
  render(<AdministrationPage />);
  fireEvent.click(await screen.findByRole("button", { name: "Original" }));
  fireEvent.change(screen.getByLabelText("Name"), { target: { value: "Local edit" } });
  Object.defineProperty(navigator, "onLine", { configurable: true, value: false });
  fireEvent(window, new Event("offline"));
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Save" }).disabled).toBe(true);
  expect(screen.getByText(/No connection/u)).toBeDefined();
  Object.defineProperty(navigator, "onLine", { configurable: true, value: true });
  fireEvent(window, new Event("online"));
  act(() => {
    setPreferences({ mode: "training" });
  });
  expect(screen.getByRole<HTMLButtonElement>("button", { name: "Save" }).disabled).toBe(true);
  act(() => {
    setPreferences({ mode: "normal" });
  });
  f.fail(503);
  fireEvent.click(screen.getAllByRole("button", { name: "Reload saved data" })[0] ?? document.body);
  await screen.findByText(/request was rejected/u);
  expect(screen.getByLabelText<HTMLInputElement>("Name").value).toBe("Local edit");
  f.fail(403);
  fireEvent.click(screen.getAllByRole("button", { name: "Reload saved data" })[0] ?? document.body);
  await screen.findByText(/Previously loaded administrative data has been hidden/u);
  expect(screen.queryByLabelText("Name")).toBeNull();
});
it("guards section changes and clears the entire administrative session on identity change", async () => {
  fixture();
  const view = render(<AdministrationPage />);
  fireEvent.click(await screen.findByRole("button", { name: "Original" }));
  fireEvent.change(screen.getByLabelText("Name"), { target: { value: "Local edit" } });
  nav("Device import");
  fireEvent.click(within(await screen.findByRole("alertdialog")).getByRole("button", { name: "Confirm action" }));
  expect(await screen.findByText(/Select a CSV file/u)).toBeDefined();
  nav("Floor plans");
  expect(await screen.findByLabelText(/Floor identifier/u)).toBeDefined();
  session.profile.context = "changed";
  view.rerender(<AdministrationPage />);
  expect(await screen.findByRole("button", { name: "Original" })).toBeDefined();
  expect(screen.queryByLabelText(/Floor identifier/u)).toBeNull();
});

it("restores the API lifetime after StrictMode replays mount effects", async () => {
  fixture();
  render(
    <StrictMode>
      <AdministrationPage />
    </StrictMode>,
  );
  expect(await screen.findByRole("button", { name: "Original" })).toBeDefined();
  expect(screen.queryByText(/No connection/u)).toBeNull();
});

it("destroys resource caches when effective scopes change without a new JWT context", async () => {
  const f = fixture();
  render(<AdministrationPage />);
  fireEvent.click(await screen.findByRole("button", { name: "Original" }));
  nav("Roles and access scopes");
  f.scopes(["site"]);
  f.name("Scoped record");
  fireEvent.click(screen.getByRole("button", { name: "Reload saved data" }));
  await screen.findByText("site");
  nav("Locations");
  expect(screen.queryByRole("button", { name: "Original" })).toBeNull();
  expect(await screen.findByRole("button", { name: "Scoped record" })).toBeDefined();
  f.fail(403);
  fireEvent.click(screen.getByRole("button", { name: "Reload saved data" }));
  await screen.findByText(/Previously loaded administrative data has been hidden/u);
  f.fail(403);
  fireEvent.click(screen.getByRole("button", { name: "Reload saved data" }));
  await waitFor(() => {
    expect(f.requests.at(-1)?.path).toBe("/v1/me");
  });
  expect(screen.queryByRole("navigation", { name: "Administration" })).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Reload saved data" }));
  expect(await screen.findByRole("button", { name: "Scoped record" })).toBeDefined();
});
