import { normalAlarm, transitionAlarm, type Alarm } from "@psim/design-system/alarm-model";
export type Priority = "P1" | "P2" | "P3" | "P4";
export interface SimRecord {
  readonly id: string;
  readonly source: string;
  readonly priority: Priority;
  readonly at: number;
  readonly deadline: number;
  readonly owner: string;
  readonly alarm: Alarm;
}
export interface Frame {
  readonly revision: number;
  readonly now: number;
  readonly records: readonly SimRecord[];
  readonly connected: boolean;
  readonly latency: number;
  readonly queued: number;
  readonly recovered: number;
}
export interface Simulator {
  readonly seed: number;
  readonly frames: readonly Frame[];
  readonly cursor: number;
  readonly trainingScore: number;
}
export type SimEvent =
  | { readonly type: "advance" | "flood" | "disconnect" | "recover" | "delay" | "shift" }
  | { readonly type: "rewind"; readonly cursor: number }
  | {
      readonly type: "alarm";
      readonly id: string;
      readonly action: "acknowledge" | "clear" | "shelve" | "suppress" | "restore";
      readonly reason?: string;
    }
  | { readonly type: "assign"; readonly id: string; readonly owner: string };
const epoch = 1704110400000;
const priorities: readonly Priority[] = ["P1", "P2", "P3", "P4"];
function generate(seed: number, count: number, now: number, revision: number) {
  const records: SimRecord[] = [];
  for (let index = 0; index < count; index++) {
    seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0;
    const priority = priorities[(seed >>> 8) % priorities.length] ?? "P4";
    const at = now - (count - index - 1);
    records.push({
      id: `SIM-${String(revision)}-${String(index)}`,
      source: `SENSOR-${String(seed % 20).padStart(2, "0")}`,
      priority,
      at,
      deadline: at + (priority === "P1" ? 120000 : 600000),
      owner: "operator-a",
      alarm: transitionAlarm(normalAlarm, { type: "raise" }),
    });
  }
  return { records, seed };
}
export function createSimulator(seed = 42): Simulator {
  return {
    seed,
    frames: [{ revision: 0, now: epoch, records: [], connected: true, latency: 35, queued: 0, recovered: 0 }],
    cursor: 0,
    trainingScore: 0,
  };
}
export function currentFrame(state: Simulator): Frame {
  const frame = state.frames[state.cursor];
  if (!frame) throw new Error("Invalid simulator cursor");
  return frame;
}
export function isCurrent(state: Simulator): boolean {
  return state.cursor === state.frames.length - 1 && currentFrame(state).connected;
}
/** Seed, logical clock and inputs fully determine the generated history. No server commands are issued. */
export function simulate(state: Simulator, event: SimEvent): Simulator {
  if (event.type === "rewind")
    return { ...state, cursor: Math.max(0, Math.min(state.frames.length - 1, Math.trunc(event.cursor))) };
  if (state.cursor !== state.frames.length - 1) return state;
  const previous = currentFrame(state);
  let frame: Frame = { ...previous, revision: previous.revision + 1, recovered: 0 };
  let seed = state.seed;
  let score = state.trainingScore;
  switch (event.type) {
    case "advance":
    case "flood":
    case "shift": {
      const count = event.type === "flood" ? 500 : event.type === "shift" ? 5 : 1;
      frame = { ...frame, now: previous.now + (event.type === "shift" ? 3600000 : 60000) };
      if (!previous.connected) frame = { ...frame, queued: previous.queued + count };
      else {
        const generated = generate(seed, count, frame.now, frame.revision);
        seed = generated.seed;
        frame = { ...frame, records: [...previous.records, ...generated.records] };
      }
      break;
    }
    case "disconnect":
      frame = { ...frame, connected: false };
      break;
    case "recover": {
      if (previous.connected) return state;
      const pending = previous.queued;
      frame = { ...frame, connected: true, latency: 35, queued: 0, recovered: pending };
      const generated = generate(state.seed, pending, previous.now, frame.revision);
      frame = { ...frame, records: [...previous.records, ...generated.records] };
      seed = generated.seed;
      break;
    }
    case "delay":
      frame = { ...frame, latency: previous.latency > 1000 ? 35 : 2400 };
      break;
    case "alarm": {
      if (!previous.connected) return state;
      if ((event.action === "shelve" || event.action === "suppress") && !event.reason?.trim()) return state;
      frame = {
        ...frame,
        records: previous.records.map((record) =>
          record.id !== event.id
            ? record
            : {
                ...record,
                alarm: transitionAlarm(
                  record.alarm,
                  event.action === "shelve" || event.action === "suppress"
                    ? {
                        type: event.action,
                        reason: event.reason ?? "",
                        actor: "operator-a",
                        until: frame.now + 600000,
                        now: frame.now,
                      }
                    : { type: event.action },
                ),
              },
        ),
      };
      if (
        event.action === "acknowledge" &&
        previous.records.some((record) => record.id === event.id && !record.alarm.acknowledged)
      )
        score++;
      break;
    }
    case "assign":
      if (!previous.connected || !event.owner.trim()) return state;
      frame = {
        ...frame,
        records: previous.records.map((record) =>
          record.id === event.id ? { ...record, owner: event.owner.trim() } : record,
        ),
      };
      break;
  }
  const ticked = frame.records.map((record) => {
    const alarm = transitionAlarm(record.alarm, { type: "tick", now: frame.now });
    return alarm === record.alarm ? record : { ...record, alarm };
  });
  if (ticked.some((record, index) => record !== frame.records[index])) frame = { ...frame, records: ticked };
  return { seed, frames: [...state.frames, frame], cursor: state.frames.length, trainingScore: score };
}
export function groupAlarms(records: readonly SimRecord[]) {
  const groups = new Map<
    string,
    { id: string; count: number; oldest: number; priority: Priority; records: SimRecord[] }
  >();
  for (const record of records) {
    const group = groups.get(record.source) ?? {
      id: record.source,
      count: 0,
      oldest: record.at,
      priority: record.priority,
      records: [],
    };
    group.count++;
    group.oldest = Math.min(group.oldest, record.at);
    if (record.priority < group.priority) group.priority = record.priority;
    group.records.push(record);
    groups.set(record.source, group);
  }
  return [...groups.values()];
}
export function attention(records: readonly SimRecord[], now: number) {
  const active = records.filter((record) => !record.alarm.acknowledged && record.alarm.visibility.kind === "visible");
  const recent = records.filter((record) => now >= record.at && now - record.at < 600000).length;
  return {
    unacknowledged: active.length,
    recent,
    flood: recent > 10,
    warning: records.filter((record) => now >= record.at && now - record.at < 3600000).length > 6,
    slaRisk: active.filter((record) => record.deadline - now <= 60000).length,
  };
}
/** P2 sounds once; P1 repeats at most once per 30 seconds. Muting never acknowledges an alarm. */
export function soundDecision({
  enabled,
  priority,
  acknowledged,
  visible,
  now,
  lastPlayed,
}: {
  readonly enabled: boolean;
  readonly priority: Priority;
  readonly acknowledged: boolean;
  readonly visible: boolean;
  readonly now: number;
  readonly lastPlayed?: number | undefined;
}): boolean {
  if (!enabled || acknowledged || !visible || (priority !== "P1" && priority !== "P2")) return false;
  return lastPlayed === undefined || (priority === "P1" && now - lastPlayed >= 30000);
}
