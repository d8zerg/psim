import { beforeEach, describe, expect, it } from "vitest";

import { connectionLabel, useConnectionStore } from "./connection";

describe("connection store", () => {
  beforeEach(() => {
    useConnectionStore.setState({ status: "idle", openSince: null });
  });

  it("remembers when the connection opened and keeps it while open", () => {
    const { setStatus } = useConnectionStore.getState();
    setStatus("open", 1_000);
    setStatus("open", 2_000);
    expect(useConnectionStore.getState()).toMatchObject({ status: "open", openSince: 1_000 });
  });

  it("clears the open time when the connection drops", () => {
    const { setStatus } = useConnectionStore.getState();
    setStatus("open", 1_000);
    setStatus("reconnecting");
    expect(useConnectionStore.getState()).toMatchObject({ status: "reconnecting", openSince: null });
  });

  it.each([
    ["open", "online"],
    ["connecting", "connecting"],
    ["authenticating", "connecting"],
    ["reconnecting", "reconnecting"],
    ["idle", "offline"],
    ["stopped", "offline"],
  ] as const)("labels %s as %s", (status, label) => {
    expect(connectionLabel(status)).toBe(label);
  });
});
