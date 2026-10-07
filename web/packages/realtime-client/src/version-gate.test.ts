import { describe, expect, it } from "vitest";

import { VersionGate } from "./version-gate";

describe("VersionGate", () => {
  it("applies only versions newer than the snapshot", () => {
    const gate = new VersionGate();
    gate.seed("inc-1", 5n);

    expect(gate.accept("inc-1", 4n)).toBe(false);
    expect(gate.accept("inc-1", 5n)).toBe(false);
    expect(gate.accept("inc-1", 6n)).toBe(true);
    expect(gate.accept("inc-1", 6n)).toBe(false);
  });

  it("accepts the first change of an unknown aggregate", () => {
    const gate = new VersionGate();
    expect(gate.accept("inc-2", 1n)).toBe(true);
  });

  it("keeps the newest seed and forgets on request", () => {
    const gate = new VersionGate();
    gate.seed("inc-3", 7n);
    gate.seed("inc-3", 3n);
    expect(gate.accept("inc-3", 7n)).toBe(false);
    gate.forget("inc-3");
    expect(gate.accept("inc-3", 1n)).toBe(true);
  });
});
