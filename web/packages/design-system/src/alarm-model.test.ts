import { expect, it } from "vitest";
import {
  alarmState,
  normalAlarm,
  transitionAlarm,
  draftCommand,
  transitionCommand,
  type Command,
  type CommandEvent,
} from "./alarm-model";
it("keeps return-to-normal separate from acknowledgment and suppression", () => {
  expect(alarmState(normalAlarm)).toBe("normal");
  const active = transitionAlarm(normalAlarm, { type: "raise" });
  expect(transitionAlarm(active, { type: "raise" })).toBe(active);
  expect(alarmState(active)).toBe("active-unacknowledged");
  const acknowledged = transitionAlarm(active, { type: "acknowledge" });
  expect(alarmState(acknowledged)).toBe("active-acknowledged");
  expect(transitionAlarm(acknowledged, { type: "acknowledge" })).toBe(acknowledged);
  const returned = transitionAlarm(active, { type: "clear" });
  expect(alarmState(returned)).toBe("returned-unacknowledged");
  expect(transitionAlarm(returned, { type: "clear" })).toBe(returned);
  expect(alarmState(transitionAlarm(returned, { type: "acknowledge" }))).toBe("normal");
  for (const type of ["shelve", "suppress"] as const) {
    const hidden = transitionAlarm(active, { type, reason: "Maintenance", actor: "supervisor", now: 100, until: 200 });
    expect(hidden.acknowledged).toBe(false);
    expect(hidden.visibility.kind).toBe(type === "shelve" ? "shelved" : "suppressed");
    expect(transitionAlarm(hidden, { type: "tick", now: 199 })).toBe(hidden);
    expect(transitionAlarm(hidden, { type: "tick", now: 200 }).visibility.kind).toBe("visible");
    expect(transitionAlarm(hidden, { type: "restore" }).visibility.kind).toBe("visible");
    for (const invalid of [{ reason: "" }, { actor: "" }, { until: 100 }, { until: NaN }])
      expect(
        transitionAlarm(active, { type, reason: "Valid", actor: "supervisor", now: 100, until: 200, ...invalid }),
      ).toBe(active);
  }
  expect(transitionAlarm(active, { type: "restore" })).toBe(active);
  expect(transitionAlarm(active, { type: "tick", now: 1 })).toBe(active);
});
const requested = () =>
  transitionCommand(draftCommand, {
    type: "request",
    actor: "a",
    target: "door-1",
    reason: "Verify exit",
    current: true,
  });
it("requires current data, an audit reason and a second person before sending a command", () => {
  for (const invalid of [{ actor: "" }, { target: "" }, { reason: " " }, { current: false }])
    expect(
      transitionCommand(draftCommand, {
        type: "request",
        actor: "a",
        target: "door",
        reason: "Reason",
        current: true,
        ...invalid,
      }),
    ).toBe(draftCommand);
  const pending = requested();
  expect(
    transitionCommand(pending, { type: "request", actor: "a", target: "door", reason: "Reason", current: true }),
  ).toBe(pending);
  expect(transitionCommand(pending, { type: "approve", reviewer: "a", current: true })).toBe(pending);
  expect(transitionCommand(pending, { type: "approve", reviewer: "", current: true })).toBe(pending);
  expect(transitionCommand(pending, { type: "approve", reviewer: "b", current: false })).toBe(pending);
  expect(transitionCommand(draftCommand, { type: "approve", reviewer: "b", current: true })).toBe(draftCommand);
  expect(transitionCommand(pending, { type: "send", current: true })).toBe(pending);
  const approved = transitionCommand(pending, { type: "approve", reviewer: "b", current: true });
  expect(transitionCommand(approved, { type: "send", current: false })).toBe(approved);
  const sent = transitionCommand(approved, { type: "send", current: true });
  expect(sent.phase).toBe("sent");
  expect(transitionCommand(sent, { type: "execute" }).phase).toBe("executed");
  const acknowledged = transitionCommand(sent, { type: "acknowledge" });
  expect(acknowledged.phase).toBe("acknowledged");
  expect(transitionCommand(acknowledged, { type: "execute" }).phase).toBe("executed");
  for (const type of ["fail", "timeout", "disconnect"] as const) {
    expect(transitionCommand(sent, { type }).phase).toBe(
      type === "fail" ? "failed" : type === "timeout" ? "timed-out" : "unknown",
    );
    expect(transitionCommand(acknowledged, { type })).not.toBe(acknowledged);
  }
  expect(transitionCommand(pending, { type: "reject" }).phase).toBe("rejected");
  for (const state of [draftCommand, pending, approved])
    expect(transitionCommand(state, { type: "cancel" }).phase).toBe("cancelled");
});
it("never automatically retries terminal or unknown outcomes", () => {
  const terminal: Command = { ...requested(), phase: "unknown" };
  const events: readonly CommandEvent[] = [
    { type: "send", current: true },
    { type: "approve", reviewer: "b", current: true },
    { type: "acknowledge" },
    { type: "execute" },
    { type: "fail" },
    { type: "reject" },
    { type: "timeout" },
    { type: "cancel" },
    { type: "disconnect" },
  ];
  for (const event of events) expect(transitionCommand(terminal, event)).toBe(terminal);
});
