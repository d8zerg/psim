import { describe, expect, it, vi } from "vitest";
import { ApiProblemError, createApiClient } from "@psim/api-client";
import { data, headers, OperatorApi, RequestFailure, statusOf } from "./api";
import { deviceHealth, errorKind, filteredFeed, locationTree } from "./model";
const signal = new AbortController().signal;
function fixture(body: unknown = {}) {
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockImplementation(() =>
      Promise.resolve(new Response(JSON.stringify(body), { headers: { "content-type": "application/json" } })),
    );
  const api = new OperatorApi(createApiClient({ baseUrl: "https://psim.test", getAccessToken: () => "token", fetch }));
  return { api, fetch };
}
describe("operator API boundaries", () => {
  it("preserves typed pagination, scope, authentication and private image requests", async () => {
    const { api, fetch } = fixture({});
    await api.operations("id", signal);
    await api.timeline("id", signal, "next");
    await api.signals("id", signal, "next");
    await api.run("id", signal);
    await api.commands("id", signal, "next");
    await api.locations(signal, "root", "next");
    await api.devices("floor", signal, "next");
    await api.device("device", signal);
    await api.locationStatus("floor", signal);
    await api.event("event", signal);
    await api.duty("site", signal);
    await api.command("command", signal);
    const requests = fetch.mock.calls.map(([request]) => request as Request);
    expect(requests.every((request) => request.headers.get("Authorization") === "Bearer token")).toBe(true);
    expect(requests[1]?.url).toContain("pageToken=next");
    expect(requests[4]?.url).toContain("incidentId=id");
    expect(requests[5]?.url).toContain("parentId=root");
    expect(requests[6]?.url).toContain("locationId=floor");
    fetch.mockResolvedValueOnce(
      new Response(new Blob(["image"], { type: "image/png" }), { headers: { "content-type": "image/png" } }),
    );
    expect((await api.floorPlan("floor", signal)).type).toBe("image/png");
    fetch.mockResolvedValueOnce(new Response("{}", { status: 404 }));
    expect(await api.run("id", signal)).toEqual({});
    fetch.mockResolvedValueOnce(new Response("{}", { status: 404 }));
    expect(await api.duty("site", signal)).toEqual({});
  });
  it("binds mutations to current versions and unique keys; command identity is its delivery key", async () => {
    const { api, fetch } = fixture({ results: [{ incident: { version: "8" } }] });
    const incident = { incidentId: "incident", version: "7" };
    for (const action of ["acknowledge", "start-work", "assign", "resolve", "reopen", "close"] as const)
      await api.incidentAction(incident, action, { reason: "reason" });
    await api.comment(incident, "note");
    await api.alarm(incident, "shelve", "inspection", "future");
    await api.alarm(incident, "unshelve", "done");
    await api.transfer(incident, "recipient", "handover");
    await api.transfer(incident, "", "", "transfer");
    for (const action of ["complete", "skip", "retry", "override"] as const)
      await api.step({ runId: "run", version: "5" }, "step", action, { note: "reason", checkedItemIds: ["check"] });
    await api.preview({ commandId: "command", deviceId: "device", capability: "unlock" });
    await api.send({
      commandId: "command",
      deviceId: "device",
      capability: "unlock",
      previewFingerprint: "fingerprint",
    });
    for (const action of ["approve", "reject", "cancel"] as const)
      await api.commandAction({ commandId: "command", version: "3" }, action, "fingerprint", "reason");
    await api.setDuty("site", { userId: "user", fallbackUserId: "backup", version: "2" });
    const requests = fetch.mock.calls.map(([request]) => request as Request);
    expect(requests[0]?.headers.get("If-Match")).toBe('"7"');
    expect(requests[11]?.headers.get("If-Match")).toBe('"5"');
    expect(requests[16]?.headers.get("Idempotency-Key")).toBe("command");
    expect(requests[17]?.headers.get("If-Match")).toBe('"3"');
    expect(new Set(requests.map((request) => request.headers.get("Idempotency-Key"))).size).toBe(requests.length);
    expect(await requests[16]?.json()).toMatchObject({ previewFingerprint: "fingerprint" });
    expect(await requests[10]?.json()).toMatchObject({ items: [{ transferId: "transfer", expectedVersion: "7" }] });
  });
  it("surfaces per-item transfer rejection and unknown outcomes without an automatic retry", async () => {
    const { api, fetch } = fixture({ results: [{ errorCode: "COMMON_CONCURRENT_MODIFICATION" }] });
    await expect(api.transfer({ incidentId: "id", version: "1" }, "user", "reason")).rejects.toMatchObject({
      status: 409,
    });
    fetch.mockRejectedValueOnce(new TypeError("offline"));
    await expect(api.send({ commandId: "delivery" })).rejects.toThrow("offline");
    expect(fetch).toHaveBeenCalledTimes(2);
    const problem = { type: "about:blank", title: "Forbidden", status: 403, code: "AUTH_FORBIDDEN" };
    expect(() => data({ error: problem, response: new Response("", { status: 403 }) })).toThrow(ApiProblemError);
    expect(() => data({ response: new Response("", { status: 503 }) })).toThrow(RequestFailure);
    expect(statusOf(new ApiProblemError(problem))).toBe(403);
    expect(statusOf(new RequestFailure(412, "conflict"))).toBe(412);
    expect(statusOf(new Error())).toBe(0);
    expect(headers("4", "key")).toEqual({ "Idempotency-Key": "key", "If-Match": '"4"' });
  });
});
it("filters independently by identity, priority, lifecycle, site and responsible operator", () => {
  const row = {
    id: "incident",
    title: "Alarm",
    version: "1",
    eventId: "event",
    state: "NEW",
    priority: "P1",
    siteId: "site",
    zoneId: "zone",
    assigneeId: "owner",
  };
  expect(filteredFeed([row], " ALARM ", "P1", "NEW", "site", "owner")).toEqual([row]);
  for (const filters of [
    ["absent", "", "", "", ""],
    ["", "P2", "", "", ""],
    ["", "", "CLOSED", "", ""],
    ["", "", "", "other", ""],
    ["", "", "", "", "other"],
  ] as const)
    expect(filteredFeed([row], filters[0], filters[1], filters[2], filters[3], filters[4])).toEqual([]);
  expect(locationTree([{ locationId: "zone", parentId: "invisible-parent", name: "Zone" }])[0]?.id).toBe("zone");
  expect(locationTree([{ locationId: "site" }, { locationId: "floor", parentId: "site" }])[0]?.children[0]?.id).toBe(
    "floor",
  );
});
it("never infers healthy equipment from an event or missing connection evidence", () => {
  const device = { state: "RESOURCE_STATE_ACTIVE" } as const;
  const status = {
    state: "EVENT_CLASS_STATE",
    quality: "DATA_QUALITY_ACTUAL",
    connection: "online",
    online: true,
  } as const;
  expect(deviceHealth(device, status, true, false)).toBe("normal");
  expect(deviceHealth(device, { ...status, state: "EVENT_CLASS_ALARM" }, true, false)).toBe("alarm");
  for (const variant of [
    undefined,
    { ...status, quality: "DATA_QUALITY_STALE" },
    { ...status, connection: "unknown" },
    { ...status, online: false },
  ] as const)
    expect(deviceHealth(device, variant, true, false)).toBe("offline");
  expect(deviceHealth(device, status, false, false)).toBe("offline");
  expect(deviceHealth(device, status, true, true)).toBe("offline");
  expect(deviceHealth({ state: "RESOURCE_STATE_DISABLED" }, status, true, false)).toBe("offline");
  expect([0, 409, 412, 401, 403, 400, 503].map(errorKind)).toEqual([
    "unknownResult",
    "conflict",
    "conflict",
    "forbidden",
    "forbidden",
    "operationFailed",
    "unknownResult",
  ]);
});
