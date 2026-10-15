import type * as FeedModule from "../feed/controller";
import { Priority } from "@psim/realtime-client";
import { createApiClient } from "@psim/api-client";
import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { auth } from "../auth";
import { createFeed, FeedController } from "../feed/controller";
import type { Delta } from "@psim/realtime-client";
function change(): Delta {
  return {
    change: {
      case: "incidentEvent",
      value: {
        envelope: { correlationId: "event" },
        payload: {
          case: "incidentCreated",
          value: { incident: { incidentId: "incident", version: 2n, title: "Intrusion alarm", priority: Priority.P2 } },
        },
      },
    },
  } as Delta;
}
import { HomePage } from "./home";
vi.mock("../auth", () => ({ auth: { token: vi.fn(), login: vi.fn() } }));
vi.mock("../feed/controller", async (original) => ({
  ...(await original<typeof FeedModule>()),
  createFeed: vi.fn(),
}));
afterEach(() => {
  cleanup();
  vi.resetAllMocks();
});
function fixture() {
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockResolvedValue(new Response(JSON.stringify({ incidents: [], resumeToken: "empty" })));
  const controller = new FeedController(
    createApiClient({ baseUrl: "https://test", getAccessToken: () => undefined, fetch }),
    () => ({ start: vi.fn(), stop: vi.fn(), subscribe: vi.fn() }),
  );
  vi.mocked(createFeed).mockReturnValue(controller);
  return controller;
}
it("shows an empty authenticated feed then renders an incoming incident", async () => {
  const controller = fixture();
  vi.mocked(auth.token).mockResolvedValue("token");
  render(<HomePage />);
  expect(await screen.findByText(/No active incidents/)).toBeDefined();
  act(() => {
    controller.delta(change());
  });
  const row = screen.getByTestId("incident-row");
  expect(row.textContent).toContain("Intrusion alarm");
  expect(row.dataset.version).toBe("2");
  expect(Number(row.dataset.renderedAt)).toBeGreaterThan(0);
});
it("shows a sign-in error without exposing credentials", async () => {
  fixture();
  vi.mocked(auth.token).mockResolvedValue(undefined);
  vi.mocked(auth.login).mockRejectedValue(new Error("Sign-in failed"));
  render(<HomePage />);
  fireEvent.click(await screen.findByRole("button", { name: "Sign in" }));
  expect((await screen.findByRole("alert")).textContent).toBe("Sign-in failed");
});
