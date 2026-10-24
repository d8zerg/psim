import { useSyncExternalStore } from "react";
import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { HomePage } from "./home";
import { FeedController } from "../feed/controller";
import { createApiClient } from "@psim/api-client";
import { Priority } from "@psim/realtime-client";
import type { Delta } from "@psim/realtime-client";
function change(): Delta {
  return {
    change: {
      case: "incidentEvent",
      value: {
        payload: {
          case: "incidentCreated",
          value: { incident: { incidentId: "incident", version: 2n, title: "Alarm", priority: Priority.P2 } },
        },
      },
    },
  } as Delta;
}
import { setPreferences } from "../ui/preferences";
afterEach(cleanup);
it("keeps live rows visible during connection loss and offers explicit recovery", async () => {
  setPreferences({ language: "en" });
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockResolvedValue(new Response(JSON.stringify({ incidents: [], resumeToken: "snapshot" })));
  const feed = new FeedController(
    createApiClient({ baseUrl: "https://test", getAccessToken: () => undefined, fetch }),
    () => ({ start: vi.fn(), stop: vi.fn(), subscribe: vi.fn() }),
  );
  const retry = vi.fn();
  function View() {
    const state = useSyncExternalStore(feed.subscribe, feed.getSnapshot);
    return <HomePage state={state} retry={retry} />;
  }
  await feed.start();
  render(<View />);
  expect(screen.getByText("No active incidents")).toBeDefined();
  act(() => {
    feed.delta(change());
  });
  expect(screen.getByTestId("incident-row").dataset.version).toBe("2");
  act(() => {
    feed.fail(new Error("offline"));
  });
  expect(screen.getByTestId("incident-row")).toBeDefined();
  fireEvent.click(screen.getByRole("button", { name: "Refresh data" }));
  expect(retry).toHaveBeenCalledOnce();
  feed.stop();
});

it("does not present an unavailable initial snapshot as an empty live feed", () => {
  setPreferences({ language: "en" });
  render(
    <HomePage
      state={{ rows: [], loaded: false, loading: false, status: "offline", error: "network" }}
      retry={vi.fn()}
    />,
  );
  expect(screen.queryByText("No active incidents")).toBeNull();
  expect(screen.getByText("Unavailable")).toBeDefined();
});
