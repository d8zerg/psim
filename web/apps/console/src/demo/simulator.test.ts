import { describe, expect, it } from "vitest";
import { attention, createSimulator, currentFrame, groupAlarms, isCurrent, simulate, soundDecision } from "./simulator";
describe("deterministic operational simulator", () => {
  it("replays the same seed, groups a flood without loss, and retains unchanged records", () => {
    const state = simulate(createSimulator(), { type: "flood" });
    expect(state).toEqual(simulate(createSimulator(), { type: "flood" }));
    const frame = currentFrame(state);
    expect(frame.records).toHaveLength(500);
    expect(groupAlarms(frame.records).reduce((total, group) => total + group.count, 0)).toBe(500);
    expect(attention(frame.records, frame.now)).toMatchObject({ flood: true, warning: true, unacknowledged: 500 });
    const next = currentFrame(simulate(state, { type: "advance" }));
    expect(next.records[0]).toBe(frame.records[0]);
  });
  it("prevents mutations in history and during disconnection, then briefs the recovered events", () => {
    let state = simulate(createSimulator(5), { type: "advance" });
    const old = currentFrame(state);
    state = simulate(state, { type: "disconnect" });
    expect(isCurrent(state)).toBe(false);
    const id = old.records[0]?.id ?? "";
    expect(simulate(state, { type: "alarm", action: "acknowledge", id })).toBe(state);
    state = simulate(state, { type: "flood" });
    expect(currentFrame(state).records).toBe(old.records);
    expect(currentFrame(state).queued).toBe(500);
    state = simulate(state, { type: "recover" });
    expect(currentFrame(state)).toMatchObject({ connected: true, queued: 0, recovered: 500 });
    expect(currentFrame(state).records).toHaveLength(501);
    expect(simulate(state, { type: "recover" })).toBe(state);
    const past = simulate(state, { type: "rewind", cursor: 1 });
    expect(currentFrame(past)).toBe(old);
    expect(simulate(past, { type: "flood" })).toBe(past);
    expect(isCurrent(past)).toBe(false);
    expect(currentFrame(simulate(past, { type: "rewind", cursor: 999 }))).toBe(currentFrame(state));
  });
  it("records audited shelving, acknowledgements, SLA risk, reassignment and shift progression", () => {
    let state = simulate(createSimulator(), { type: "advance" });
    const id = currentFrame(state).records[0]?.id ?? "";
    expect(simulate(state, { type: "alarm", id, action: "shelve", reason: " " })).toBe(state);
    state = simulate(state, { type: "alarm", id, action: "shelve", reason: "Maintenance" });
    expect(currentFrame(state).records[0]?.alarm.visibility.kind).toBe("shelved");
    state = simulate(state, { type: "alarm", id, action: "restore" });
    state = simulate(state, { type: "alarm", id, action: "suppress", reason: "Rule" });
    state = simulate(state, { type: "alarm", id, action: "clear" });
    expect(currentFrame(state).records[0]?.alarm.acknowledged).toBe(false);
    state = simulate(state, { type: "alarm", id, action: "acknowledge" });
    expect(state.trainingScore).toBe(1);
    state = simulate(state, { type: "assign", id, owner: "operator-b" });
    expect(currentFrame(state).records[0]?.owner).toBe("operator-b");
    expect(simulate(state, { type: "assign", id, owner: " " })).toBe(state);
    state = simulate(state, { type: "delay" });
    expect(currentFrame(state).latency).toBe(2400);
    state = simulate(state, { type: "delay" });
    expect(currentFrame(state).latency).toBe(35);
    state = simulate(state, { type: "shift" });
    expect(currentFrame(state).records[0]?.alarm.visibility.kind).toBe("visible");
    expect(attention(currentFrame(state).records, currentFrame(state).now + 600000).slaRisk).toBeGreaterThan(0);
  });
  it("limits sound by priority, acknowledgement, visibility and repeat interval", () => {
    const policy = { enabled: true, priority: "P1", acknowledged: false, visible: true, now: 50000 } as const;
    expect(soundDecision(policy)).toBe(true);
    expect(soundDecision({ ...policy, lastPlayed: 25000 })).toBe(false);
    expect(soundDecision({ ...policy, lastPlayed: 20000 })).toBe(true);
    for (const priority of ["P2", "P3", "P4"] as const)
      expect(soundDecision({ ...policy, priority, lastPlayed: 1 })).toBe(false);
    expect(soundDecision({ ...policy, priority: "P2" })).toBe(true);
    expect(soundDecision({ ...policy, enabled: false })).toBe(false);
    expect(soundDecision({ ...policy, acknowledged: true })).toBe(false);
    expect(soundDecision({ ...policy, visible: false })).toBe(false);
  });
});
