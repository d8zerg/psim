import { RouterProvider } from "@tanstack/react-router";
import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { createAppRouter } from "./router";
import { ShellController, type AuthSession } from "./shell/controller";
import type { SessionState } from "./auth";
import { setPreferences } from "./ui/preferences";
import { FeedController } from "./feed/controller";
import { createApiClient } from "@psim/api-client";
import { RouteError } from "./shell/pages";
function fixture(role?: string) {
  let state: SessionState = role
    ? {
        status: "authenticated",
        error: false,
        profile: { subject: "actor", name: "Operator", roles: [role], context: role },
      }
    : { status: "anonymous", error: false };
  const listeners = new Set<() => void>();
  const update = (next: SessionState) => {
    state = next;
    listeners.forEach((listener) => {
      listener();
    });
  };
  const auth: AuthSession = {
    getSnapshot: () => state,
    subscribe: (listener) => {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    token: () => Promise.resolve(role ? "token" : undefined),
    login: vi.fn(),
    logout: () => {
      update({ status: "anonymous", error: false });
      return Promise.resolve();
    },
    invalidate: () => {
      update({ status: "anonymous", error: true });
    },
  };
  const factory = vi.fn<() => FeedController>(() => {
    throw new Error("No incident subscription expected");
  });
  return { auth, update, shell: new ShellController(auth, factory), factory };
}
beforeEach(() => {
  setPreferences({ language: "en" });
  window.history.replaceState(null, "", "/");
});
afterEach(cleanup);
it("renders anonymous sign-in without opening a channel and handles login failure", async () => {
  const f = fixture();
  vi.mocked(f.auth.login).mockRejectedValueOnce(new Error("secret details"));
  render(<RouterProvider router={createAppRouter(f.shell)} />);
  expect(await screen.findByRole("heading", { name: "Open-Source PSIM Platform" })).toBeDefined();
  expect(screen.getByTestId("connection-status").getAttribute("data-status")).toBe("offline");
  fireEvent.click(screen.getByRole("button", { name: "Sign in" }));
  expect((await screen.findByRole("alert")).textContent).toBe("Sign-in failed");
  expect(f.factory).not.toHaveBeenCalled();
});
it("loads an allowed deep link, gates manual navigation and clears the session on logout", async () => {
  const f = fixture("manager");
  window.history.replaceState(null, "", "/reports");
  const router = createAppRouter(f.shell);
  render(<RouterProvider router={router} />);
  expect(await screen.findByRole("heading", { name: "Incident reports" })).toBeDefined();
  expect(screen.queryByRole("link", { name: "Administration" })).toBeNull();
  await act(async () => {
    await router.navigate({ to: "/admin" });
  });
  expect(await screen.findByRole("heading", { name: "Access denied" })).toBeDefined();
  fireEvent.click(screen.getByRole("button", { name: "Sign out" }));
  expect(await screen.findByRole("button", { name: "Sign in" })).toBeDefined();
  expect(f.factory).not.toHaveBeenCalled();
});
it.each([
  ["admin", "Administration"],
  ["integrator", "Integrations"],
  ["auditor", "Audit log"],
])("chooses the default module for %s", async (role, title) => {
  const f = fixture(role);
  render(<RouterProvider router={createAppRouter(f.shell)} />);
  expect(await screen.findByRole("heading", { name: title })).toBeDefined();
  expect(f.factory).not.toHaveBeenCalled();
});
it("handles an unknown location inside the authenticated layout", async () => {
  const f = fixture("manager");
  window.history.replaceState(null, "", "/missing");
  render(<RouterProvider router={createAppRouter(f.shell)} />);
  expect(await screen.findByRole("heading", { name: "Page not found" })).toBeDefined();
});
it("renders a generic route failure without internal error details", () => {
  render(<RouteError reset={vi.fn()} />);
  expect(screen.getByRole("heading", { name: "Could not open this page" })).toBeDefined();
});

it("recovers an unavailable operator snapshot without presenting a false empty feed", async () => {
  const f = fixture("operator");
  let available = false;
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockImplementation(() =>
      Promise.resolve(
        available
          ? new Response(JSON.stringify({ incidents: [], resumeToken: "snapshot" }))
          : new Response("{}", { status: 503 }),
      ),
    );
  f.factory.mockReturnValue(
    new FeedController(
      createApiClient({ baseUrl: "https://test", getAccessToken: f.auth.token, fetch }),
      (handlers) => ({
        start: () => {
          handlers.onStatus?.("open");
        },
        stop: vi.fn(),
        subscribe: vi.fn(),
      }),
    ),
  );
  render(<RouterProvider router={createAppRouter(f.shell)} />);
  expect(await screen.findByText("Unavailable")).toBeDefined();
  expect(screen.queryByText("No active incidents")).toBeNull();
  available = true;
  fireEvent.click(screen.getByRole("button", { name: "Refresh data" }));
  expect(await screen.findByText("No active incidents")).toBeDefined();
  expect(f.factory).toHaveBeenCalledOnce();
  expect(screen.getByTestId("connection-status").dataset.status).toBe("open");
});
