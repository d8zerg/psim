import { act, cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { DomainStatus } from "../../stories/domain";
import { AlarmBanner, CameraTile, CommandLifecycle, DeviceMarker, Provenance, SlaTimer, Timestamp } from "./domain";
import { draftCommand, type CommandPhase } from "../alarm-model";
afterEach(() => {
  cleanup();
  vi.useRealTimers();
});
it("shows every alarm state and duplicates priority and connection meanings in text", () => {
  render(<DomainStatus />);
  for (const text of [
    "P1 - Critical",
    "P2 - High",
    "P3 - Routine",
    "P4 - Low",
    "Active, unacknowledged",
    "Active, acknowledged",
    "Returned, unacknowledged",
    "Normal",
  ])
    expect(screen.getAllByText(text).length).toBeGreaterThan(0);
  fireEvent.click(screen.getByRole("button", { name: "Acknowledge alarm" }));
  expect(screen.getByRole("button", { name: "Alarm acknowledged" }).hasAttribute("disabled")).toBe(true);
  fireEvent.click(screen.getByRole("button", { name: /SENSOR-42: Alarm/u }));
  expect(screen.getByRole("button", { name: /SENSOR-42: Alarm/u }).getAttribute("aria-pressed")).toBe("true");
});
it("keeps normal signals neutral and represents unavailable cameras honestly", () => {
  const open = vi.fn();
  render(
    <>
      <DeviceMarker label="Door" state="normal" stateLabel="Normal" selected={false} onSelect={open} />
      <DeviceMarker label="Sensor" state="offline" stateLabel="Offline" selected onSelect={open} />
      <AlarmBanner label="Normal" count={0} priority="P4" countLabel="Alarms" />
      <AlarmBanner label="High" count={1} priority="P2" countLabel="Alarms" />
      <CameraTile
        label="Camera"
        state="available"
        image="data:image/svg+xml,%3Csvg/%3E"
        stateLabel="Available"
        openLabel="Open"
        onOpen={open}
      />
      <Provenance label="Value" value="Normal" stale={false} staleLabel="Stale" whyLabel="Why" sources={[]} />
    </>,
  );
  fireEvent.click(screen.getByRole("button", { name: "Open" }));
  expect(open).toHaveBeenCalledOnce();
  expect(screen.getByAltText("Camera")).toBeTruthy();
});
it("renders all command outcomes and preserves actor, target and reason", () => {
  const labels: Record<CommandPhase, string> = {
    draft: "Draft",
    "awaiting-approval": "Pending",
    approved: "Approved",
    sent: "Sent",
    acknowledged: "Ack",
    executed: "Done",
    failed: "Failed",
    rejected: "Rejected",
    "timed-out": "Timeout",
    cancelled: "Cancelled",
    unknown: "Unknown",
  };
  const { rerender } = render(<CommandLifecycle command={draftCommand} labels={labels} label="Command" />);
  for (const phase of Object.keys(labels) as CommandPhase[]) {
    // Keys come from the exhaustive map above.
    rerender(
      <CommandLifecycle
        command={{ phase, actor: "a", reviewer: "b", target: "DOOR-42", reason: "Checked" }}
        labels={labels}
        label="Command"
      />,
    );
    expect(screen.getByText(labels[phase])).toBeTruthy();
    expect(screen.getByText("Checked")).toBeTruthy();
  }
});
it("updates isolated clocks and releases intervals on unmount", () => {
  vi.useFakeTimers();
  vi.setSystemTime(1704110400000);
  const { unmount } = render(
    <>
      <SlaTimer live deadline={1704110402000} now={1704110400000} label="Remaining" expiredLabel="Expired" />
      <Timestamp live timestamp={1704110400000} now={1704110400000} locale="en" timeZone="UTC" />
    </>,
  );
  expect(screen.getByText("Remaining: 2 s")).toBeTruthy();
  act(() => {
    vi.advanceTimersByTime(3000);
  });
  expect(screen.getByText("Expired: 1 s")).toBeTruthy();
  expect(screen.getByText(/3 seconds ago/u)).toBeTruthy();
  unmount();
  expect(vi.getTimerCount()).toBe(0);
});
