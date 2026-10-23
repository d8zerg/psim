/** Alarm condition, acknowledgement and visibility are independent dimensions. */
export interface Alarm {
  readonly condition: "normal" | "active";
  readonly acknowledged: boolean;
  readonly visibility:
    | { readonly kind: "visible" }
    | {
        readonly kind: "shelved" | "suppressed";
        readonly reason: string;
        readonly actor: string;
        readonly until: number;
      };
}
export type AlarmEvent =
  | { readonly type: "raise" | "clear" | "acknowledge" | "restore" }
  | {
      readonly type: "shelve" | "suppress";
      readonly reason: string;
      readonly actor: string;
      readonly until: number;
      readonly now: number;
    }
  | { readonly type: "tick"; readonly now: number };
export type AlarmState = "normal" | "active-unacknowledged" | "active-acknowledged" | "returned-unacknowledged";
export const normalAlarm: Alarm = { condition: "normal", acknowledged: true, visibility: { kind: "visible" } };
export function alarmState(alarm: Alarm): AlarmState {
  if (alarm.condition === "active") return alarm.acknowledged ? "active-acknowledged" : "active-unacknowledged";
  return alarm.acknowledged ? "normal" : "returned-unacknowledged";
}
export function transitionAlarm(alarm: Alarm, event: AlarmEvent): Alarm {
  switch (event.type) {
    case "raise":
      return alarm.condition === "active" ? alarm : { ...alarm, condition: "active", acknowledged: false };
    case "clear":
      return alarm.condition === "normal" ? alarm : { ...alarm, condition: "normal" };
    case "acknowledge":
      return alarm.acknowledged ? alarm : { ...alarm, acknowledged: true };
    case "shelve":
    case "suppress":
      if (
        !event.reason.trim() ||
        !event.actor.trim() ||
        !Number.isFinite(event.until) ||
        !Number.isFinite(event.now) ||
        event.until <= event.now
      )
        return alarm;
      return {
        ...alarm,
        visibility: {
          kind: event.type === "shelve" ? "shelved" : "suppressed",
          reason: event.reason.trim(),
          actor: event.actor.trim(),
          until: event.until,
        },
      };
    case "restore":
      return alarm.visibility.kind === "visible" ? alarm : { ...alarm, visibility: { kind: "visible" } };
    case "tick":
      return alarm.visibility.kind !== "visible" && event.now >= alarm.visibility.until
        ? { ...alarm, visibility: { kind: "visible" } }
        : alarm;
  }
}
export type CommandPhase =
  | "draft"
  | "awaiting-approval"
  | "approved"
  | "sent"
  | "acknowledged"
  | "executed"
  | "failed"
  | "rejected"
  | "timed-out"
  | "cancelled"
  | "unknown";
export interface Command {
  readonly phase: CommandPhase;
  readonly actor: string;
  readonly target: string;
  readonly reason: string;
  readonly reviewer?: string;
}
export type CommandEvent =
  | {
      readonly type: "request";
      readonly actor: string;
      readonly target: string;
      readonly reason: string;
      readonly current: boolean;
    }
  | { readonly type: "approve"; readonly reviewer: string; readonly current: boolean }
  | { readonly type: "send"; readonly current: boolean }
  | { readonly type: "acknowledge" | "execute" | "fail" | "reject" | "timeout" | "cancel" | "disconnect" };
export const draftCommand: Command = { phase: "draft", actor: "", target: "", reason: "" };
/** The prototype cannot authorize operations; a live server must independently validate each transition. */
export function transitionCommand(command: Command, event: CommandEvent): Command {
  switch (event.type) {
    case "request":
      if (
        command.phase !== "draft" ||
        !event.current ||
        !event.actor.trim() ||
        !event.target.trim() ||
        !event.reason.trim()
      )
        return command;
      return {
        phase: "awaiting-approval",
        actor: event.actor.trim(),
        target: event.target.trim(),
        reason: event.reason.trim(),
      };
    case "approve":
      if (
        command.phase !== "awaiting-approval" ||
        !event.current ||
        !event.reviewer.trim() ||
        event.reviewer.trim() === command.actor
      )
        return command;
      return { ...command, phase: "approved", reviewer: event.reviewer.trim() };
    case "send":
      return command.phase === "approved" && event.current ? { ...command, phase: "sent" } : command;
    case "acknowledge":
      return command.phase === "sent" ? { ...command, phase: "acknowledged" } : command;
    case "execute":
      return command.phase === "sent" || command.phase === "acknowledged" ? { ...command, phase: "executed" } : command;
    case "fail":
      return command.phase === "sent" || command.phase === "acknowledged" ? { ...command, phase: "failed" } : command;
    case "reject":
      return command.phase === "awaiting-approval" ? { ...command, phase: "rejected" } : command;
    case "timeout":
      return command.phase === "sent" || command.phase === "acknowledged"
        ? { ...command, phase: "timed-out" }
        : command;
    case "cancel":
      return command.phase === "draft" || command.phase === "awaiting-approval" || command.phase === "approved"
        ? { ...command, phase: "cancelled" }
        : command;
    case "disconnect":
      return command.phase === "sent" || command.phase === "acknowledged" ? { ...command, phase: "unknown" } : command;
  }
}
