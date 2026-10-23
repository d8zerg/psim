import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import Operations from "./operations";
import { defaults, setPreferences, usePreferences } from "../ui/preferences";
import { playAlert } from "../ui/alert-sound";
vi.mock("../ui/alert-sound", () => ({ playAlert: vi.fn().mockResolvedValue(undefined) }));
beforeEach(() => {
  setPreferences({ ...defaults, language: "en" });
  vi.stubGlobal(
    "ResizeObserver",
    class {
      observe() {
        /* Fixed dimensions supplied below. */
      }
      unobserve() {
        /* No native observer. */
      }
      disconnect() {
        /* No native observer. */
      }
    },
  );
  vi.spyOn(HTMLElement.prototype, "clientHeight", "get").mockReturnValue(384);
  vi.spyOn(HTMLElement.prototype, "clientWidth", "get").mockReturnValue(800);
  vi.spyOn(HTMLElement.prototype, "offsetHeight", "get").mockReturnValue(64);
  vi.spyOn(HTMLElement.prototype, "offsetWidth", "get").mockReturnValue(800);
  HTMLElement.prototype.scrollTo = vi.fn();
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
  vi.useRealTimers();
  setPreferences(defaults);
});
function button(name: string) {
  return screen.getByRole("button", { name });
}
it("groups 500 events, retains a selected record, blocks historical actions and briefs recovery", async () => {
  render(<Operations />);
  const grid = screen.getByRole("grid");
  const old = within(grid).getAllByRole("row")[1];
  fireEvent.click(button("Generate 500 alarms"));
  expect(grid.getAttribute("aria-rowcount")).toBe("502");
  expect(within(grid).getAllByRole("row")[1]).toBe(old);
  fireEvent.click(screen.getAllByRole("button", { name: /^SENSOR-\d+: \d/u })[0] ?? document.body);
  fireEvent.click(button("All sources"));
  fireEvent.change(screen.getByLabelText("History position"), { target: { value: "1" } });
  expect(button("Acknowledge").hasAttribute("disabled")).toBe(true);
  expect(screen.getByText("Historical view. Actions are disabled.")).toBeTruthy();
  fireEvent.click(button("Return to live state"));
  fireEvent.click(button("Disconnect channel"));
  fireEvent.click(button("Next minute"));
  fireEvent.click(button("Recover channel"));
  expect(screen.getByText(/Recovery briefing: events received: 1/u)).toBeTruthy();
  fireEvent.click(button("Toggle latency"));
  expect(screen.getByText("! Delayed")).toBeTruthy();
  fireEvent.change(screen.getByLabelText("Screen layout"), { target: { value: "dual" } });
  fireEvent.change(screen.getByLabelText("Time zone"), { target: { value: "Europe/Moscow" } });
  fireEvent.click(button("Start training mode"));
  fireEvent.click(button("Acknowledge"));
  expect(screen.getByText("Correct acknowledgements: 1")).toBeTruthy();
  fireEvent.click(button("Enable crisis mode"));
  expect(usePreferences.getState().mode).toBe("crisis");
  fireEvent.click(button("Return to normal mode"));
  await waitFor(() => {
    expect(screen.getByTestId("operations-announcement").textContent).toContain("Unacknowledged");
  });
});
it("requires audited suppression, source evidence, a different reviewer and final confirmation", async () => {
  render(<Operations />);
  expect(button("Shelve for 10 minutes").hasAttribute("disabled")).toBe(true);
  fireEvent.change(screen.getByRole("textbox", { name: "Audit reason" }), { target: { value: "Check entrance" } });
  fireEvent.click(button("Shelve for 10 minutes"));
  expect(screen.getByText(/Shelved: Check entrance/u)).toBeTruthy();
  fireEvent.click(button("Restore visibility"));
  fireEvent.click(button("Suppress for 10 minutes"));
  fireEvent.click(button("Restore visibility"));
  fireEvent.click(button("Return condition to normal"));
  fireEvent.click(button("Reassign to backup"));
  expect(screen.getAllByText("Owner: operator-b").length).toBeGreaterThan(0);
  fireEvent.click(button("Request soft lease"));
  expect(screen.getByText("Soft lease requested: operator-a")).toBeTruthy();
  fireEvent.click(button("Release soft lease"));
  fireEvent.click(button("Why this value"));
  expect(screen.getByRole("dialog").textContent).toContain("rule:v1");
  fireEvent.keyDown(screen.getByRole("dialog"), { key: "Escape" });
  fireEvent.change(screen.getByLabelText("Filter source evidence"), { target: { value: "missing evidence" } });
  expect(screen.getByText("No matching sources. No answer generated.")).toBeTruthy();
  fireEvent.click(button("Request command approval"));
  fireEvent.click(button("Approve as second supervisor"));
  const panel = screen.getByRole("dialog");
  fireEvent.change(within(panel).getByLabelText("Reviewer identity"), { target: { value: "operator-a" } });
  expect(within(panel).getByRole("button", { name: "Approve as second supervisor" }).hasAttribute("disabled")).toBe(
    true,
  );
  fireEvent.change(within(panel).getByLabelText("Reviewer identity"), { target: { value: "supervisor-b" } });
  fireEvent.click(within(panel).getByRole("button", { name: "Approve as second supervisor" }));
  fireEvent.click(within(panel).getByText("Close"));
  fireEvent.click(button("Send simulated command"));
  fireEvent.click(screen.getByRole("alertdialog").querySelector("button:last-child") ?? document.body);
  await waitFor(() => {
    expect(button("Report execution").hasAttribute("disabled")).toBe(false);
  });
  fireEvent.click(button("Disconnect channel"));
  expect(screen.getByText("Outcome unknown; verify device state")).toBeTruthy();
});
it("binds handover review to a snapshot and supports breaks, sound errors and night preferences", async () => {
  render(<Operations />);
  fireEvent.change(screen.getByLabelText("Receiving operator"), { target: { value: "operator-b" } });
  fireEvent.click(screen.getByLabelText("I reviewed active alarms, shelved alarms and pending commands"));
  fireEvent.click(button("Accept shift briefing"));
  expect(screen.getByText(/Briefing accepted by: operator-b; snapshot:1/u)).toBeTruthy();
  fireEvent.click(button("Next minute"));
  expect(button("Accept shift briefing").hasAttribute("disabled")).toBe(true);
  fireEvent.click(screen.getByRole("switch", { name: "Break reminder after 45 minutes" }));
  fireEvent.click(button("Advance shift"));
  expect(screen.getByText("Plan a break and hand over active alarms")).toBeTruthy();
  fireEvent.click(button("Reminder reviewed"));
  fireEvent.click(screen.getByRole("switch", { name: "Scheduled night theme (22:00-06:00)" }));
  fireEvent.click(screen.getByRole("switch", { name: "Scheduled night theme (22:00-06:00)" }));
  fireEvent.click(button("Generate 500 alarms"));
  fireEvent.click(screen.getByRole("switch", { name: "Enable alarm sound" }));
  await waitFor(() => {
    expect(playAlert).toHaveBeenCalled();
  });
  fireEvent.change(screen.getByLabelText("Volume"), { target: { value: "50" } });
  vi.mocked(playAlert).mockRejectedValueOnce(new Error("blocked"));
  fireEvent.click(button("Test speakers"));
  await screen.findByText("Sound unavailable. Check browser permissions.");
  vi.mocked(playAlert).mockResolvedValue(undefined);
  fireEvent.click(button("Test speakers"));
  await waitFor(() => {
    expect(screen.queryByText("Sound unavailable. Check browser permissions.")).toBeNull();
  });
});
it("announces the aggregate once per interval and cleans up scheduled work", () => {
  vi.useFakeTimers();
  const { unmount } = render(<Operations />);
  fireEvent.click(button("Generate 500 alarms"));
  act(() => {
    vi.advanceTimersByTime(1000);
  });
  expect(screen.getByTestId("operations-announcement").textContent).toBe("Unacknowledged: 501");
  unmount();
  expect(vi.getTimerCount()).toBe(0);
});
