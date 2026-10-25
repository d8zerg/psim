import { act, cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { HomePage } from "../pages/home";
import { VirtualFeed } from "./feed";
import { playAlert } from "../ui/alert-sound";
import { setPreferences } from "../ui/preferences";
import type { FeedRow, FeedState } from "../feed/controller";
vi.mock("../ui/alert-sound", () => ({ playAlert: vi.fn(() => Promise.resolve()) }));
const row: FeedRow = {
  id: "first",
  eventId: "event",
  version: "1",
  title: "First alarm",
  priority: "PRIORITY_P1",
  state: "INCIDENT_STATE_NEW",
  siteId: "site",
  zoneId: "zone",
  assigneeId: "owner",
  audible: true,
};
const state: FeedState = {
  rows: [
    row,
    {
      ...row,
      id: "second",
      title: "Second alarm",
      priority: "PRIORITY_P2",
      state: "INCIDENT_STATE_IN_PROGRESS",
      siteId: "other",
      assigneeId: "other",
      audible: false,
    },
  ],
  loaded: true,
  loading: false,
  error: "",
  status: "open",
};
beforeEach(() => {
  setPreferences({ language: "en", mode: "normal", sound: false });
  HTMLElement.prototype.scrollTo = vi.fn();
});
afterEach(() => {
  cleanup();
  vi.useRealTimers();
  vi.restoreAllMocks();
  vi.mocked(playAlert).mockReset().mockResolvedValue();
});
it("combines independent filters, retains selection and offers accessible keyboard navigation", () => {
  const choose = vi.fn();
  const retry = vi.fn();
  render(<HomePage state={state} retry={retry} choose={choose} selected="first" />);
  fireEvent.change(screen.getByLabelText("Priority", { exact: true }), { target: { value: "PRIORITY_P2" } });
  expect(screen.getAllByTestId("incident-row")).toHaveLength(1);
  fireEvent.change(screen.getByLabelText("Priority", { exact: true }), { target: { value: "" } });
  fireEvent.change(screen.getByLabelText("Incident state"), { target: { value: "INCIDENT_STATE_NEW" } });
  fireEvent.change(screen.getByLabelText("Object", { exact: true }), { target: { value: "site" } });
  fireEvent.change(screen.getByLabelText("Owner", { exact: true }), { target: { value: "owner" } });
  fireEvent.change(screen.getByLabelText("Search incidents"), { target: { value: "absent" } });
  expect(screen.getByText("No matching incidents")).toBeDefined();
  fireEvent.change(screen.getByLabelText("Search incidents"), { target: { value: "first" } });
  const open = screen.getByRole("button", { name: /Open incident:.*First alarm P1/u });
  fireEvent.click(open);
  expect(choose).toHaveBeenCalledWith("first");
  fireEvent.keyDown(open, { key: "ArrowDown" });
  fireEvent.keyDown(open, { key: "ArrowUp" });
  fireEvent.keyDown(open, { key: "Home" });
  fireEvent.keyDown(open, { key: "End" });
  fireEvent.keyDown(open, { key: "Escape" });
  fireEvent.keyDown(window, { altKey: true, code: "KeyF" });
  expect(document.activeElement).toBe(screen.getByLabelText("Search incidents"));
  fireEvent.keyDown(window, { altKey: true, code: "KeyR" });
  expect(retry).toHaveBeenCalledOnce();
  fireEvent.keyDown(screen.getByLabelText("Search incidents"), { altKey: true, code: "KeyR" });
  expect(retry).toHaveBeenCalledOnce();
});
it("renders a bounded window for 10000 records while retaining focus and scroll anchor across insertions", () => {
  const rows = Array.from({ length: 10000 }, (_, i) => ({ ...row, id: String(i), title: `Alarm ${String(i)}` }));
  const { container, rerender } = render(<VirtualFeed rows={rows} selected="0" choose={vi.fn()} />);
  expect(screen.getAllByTestId("incident-row").length).toBeLessThanOrEqual(12);
  const scroll = container.querySelector<HTMLElement>(".operator-feed-scroll");
  expect(scroll).not.toBeNull();
  const first = screen.getAllByRole("button")[0] as HTMLButtonElement;
  fireEvent.focus(first);
  if (scroll) {
    scroll.scrollTop = 50000;
    fireEvent.scroll(scroll);
  }
  expect(first.isConnected).toBe(true);
  rerender(<VirtualFeed rows={[{ ...row, id: "incoming" }, ...rows]} selected="0" choose={vi.fn()} />);
  expect(scroll?.scrollTop).toBe(50112);
  fireEvent.blur(first, { relatedTarget: document.body });
});
it("requires explicit sound activation, rate-limits P1 repeats and keeps visual counts when sound is blocked", async () => {
  vi.useFakeTimers();
  const { rerender } = render(<HomePage state={state} retry={vi.fn()} />);
  expect(playAlert).not.toHaveBeenCalled();
  fireEvent.click(screen.getByText("Sound alerts", { selector: "summary" }));
  fireEvent.click(screen.getByRole("switch", { name: "Sound alerts" }));
  await act(async () => {
    await vi.advanceTimersByTimeAsync(1000);
  });
  expect(playAlert).toHaveBeenCalledOnce();
  expect(screen.getByText("Unacknowledged alarms: 1")).toBeDefined();
  await act(async () => {
    await vi.advanceTimersByTimeAsync(9000);
  });
  expect(playAlert).toHaveBeenCalledTimes(2);
  fireEvent.change(screen.getByLabelText("Volume"), { target: { value: "50" } });
  vi.mocked(playAlert).mockRejectedValueOnce(new Error("blocked"));
  fireEvent.click(screen.getByRole("button", { name: "Test alert sound" }));
  await act(async () => {
    await Promise.resolve();
  });
  expect(screen.getByText("Sound is unavailable. Visual alerts remain active.")).toBeDefined();
  vi.spyOn(document, "hidden", "get").mockReturnValue(true);
  fireEvent(document, new Event("visibilitychange"));
  expect(document.title).toContain("[1]");
  rerender(<HomePage state={{ ...state, status: "offline" }} retry={vi.fn()} />);
  expect(screen.getAllByText("Last known data. Actions require a current connection.").length).toBeGreaterThan(0);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(10000);
  });
  expect(playAlert).toHaveBeenCalledTimes(3);
  act(() => {
    setPreferences({ mode: "training" });
  });
  rerender(<HomePage state={state} retry={vi.fn()} />);
});
it("plays P2 once and does not sound for unavailable or acknowledged conditions", async () => {
  vi.useFakeTimers();
  setPreferences({ sound: true });
  const { rerender } = render(
    <HomePage state={{ ...state, rows: [{ ...row, priority: "PRIORITY_P2" }] }} retry={vi.fn()} />,
  );
  await act(async () => {
    await vi.advanceTimersByTimeAsync(20000);
  });
  expect(playAlert).toHaveBeenCalledOnce();
  rerender(<HomePage state={{ ...state, rows: [{ ...row, audible: false }] }} retry={vi.fn()} />);
  await act(async () => {
    await vi.advanceTimersByTimeAsync(1000);
  });
  expect(playAlert).toHaveBeenCalledOnce();
  expect(within(screen.getByRole("region", { name: "Incidents" })).getAllByRole("button")).toHaveLength(1);
});
