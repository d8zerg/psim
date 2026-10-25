import { expect, it, vi } from "vitest";
import { ApiProblemError } from "@psim/api-client";
import { AdminApi, AdminFailure, identityConsole, saveRequest, status, write } from "./api";
import { editable, initial, list, newRecord, record, resolve, resources, string, type ResourceKey } from "./model";
import { validFile } from "./files";
it("sends the current token and freezes the complete request for exact retries", async () => {
  const calls: RequestInit[] = [];
  const fetcher = vi.fn<typeof fetch>().mockImplementation((_, options) => {
    calls.push(options ?? {});
    return Promise.resolve(Response.json({ version: "2" }));
  });
  const api = new AdminApi(() => Promise.resolve("fresh"), fetcher);
  const body = { name: "original" };
  const request = write("/v1/locations/id", body, "PUT", "1");
  body.name = "changed";
  await api.send(request);
  await api.send(request);
  expect(calls[0]?.body).toBe('{"name":"original"}');
  expect(calls[1]?.body).toBe(calls[0]?.body);
  const headers = new Headers(calls[0]?.headers);
  expect(headers.get("authorization")).toBe("Bearer fresh");
  expect(headers.get("if-match")).toBe('"1"');
  expect(headers.get("idempotency-key")).toBe(new Headers(calls[1]?.headers).get("idempotency-key"));
  expect(calls[0]?.cache).toBe("no-store");
  expect(calls[0]?.credentials).toBe("omit");
});
it("checks authentication before any network operation and propagates structured and unstructured errors", async () => {
  const fetcher = vi.fn<typeof fetch>();
  const anonymous = new AdminApi(() => Promise.resolve(undefined), fetcher);
  await expect(anonymous.get("/v1/me")).rejects.toEqual(new AdminFailure(401));
  expect(fetcher).not.toHaveBeenCalled();
  fetcher
    .mockResolvedValueOnce(
      Response.json({ type: "about:blank", title: "Denied", code: "AUTH_SCOPE_DENIED", status: 403 }, { status: 403 }),
    )
    .mockResolvedValueOnce(new Response("bad", { status: 502 }));
  const api = new AdminApi(() => Promise.resolve("token"), fetcher);
  await expect(api.get("/v1/me")).rejects.toBeInstanceOf(ApiProblemError);
  await expect(api.get("/v1/me")).rejects.toEqual(new AdminFailure(502));
  expect(status(new Error())).toBe(0);
  expect(status(new AdminFailure(412))).toBe(412);
  expect(status(new ApiProblemError({ type: "about:blank", title: "Conflict", code: "conflict", status: 409 }))).toBe(
    409,
  );
});
it("aborts outstanding reads and discards responses after disposal", async () => {
  const fetcher = vi.fn<typeof fetch>().mockResolvedValue(Response.json({ confidential: true }));
  const api = new AdminApi(() => Promise.resolve("token"), fetcher);
  api.dispose();
  await expect(api.get("/v1/me")).rejects.toMatchObject({ name: "AbortError" });
  expect(fetcher).not.toHaveBeenCalled();
  api.activate();
  expect(await api.get("/v1/me")).toEqual({ confidential: true });
});
it("transfers floor plans as binary authenticated bodies and reads private images", async () => {
  const blob = new Blob(["png"], { type: "image/png" });
  const fetcher = vi
    .fn<typeof fetch>()
    .mockResolvedValueOnce(Response.json({ file: { version: "1" } }))
    .mockResolvedValueOnce(new Response("png", { headers: { "Content-Type": "image/png" } }));
  const api = new AdminApi(() => Promise.resolve("token"), fetcher);
  const request = write("/v1/locations/floor/floor-plan", blob, "PUT", "3", "image/png");
  await api.send(request);
  expect(fetcher.mock.calls[0]?.[1]?.body).toBe(blob);
  expect((await api.blob("/v1/locations/floor/floor-plan?version=1")).type).toBe("image/png");
});
it("creates updates with the original version, distinct keys and safe certificate encoding", () => {
  for (const key of [
    "locations",
    "devices",
    "deviceTypes",
    "incidentTypes",
    "policies",
    "maintenance",
    "processing",
    "retention",
  ] as ResourceKey[]) {
    const request = saveRequest(key, { name: "new" }, { version: "7" });
    expect(request.method).toBe("PUT");
    expect(request.version).toBe("7");
    expect(saveRequest(key, {}, { version: "7" }).key).not.toBe(request.key);
  }
  const request = saveRequest("connectors", { connectorId: "id" }, undefined, "CERT\n+&");
  expect(new URL(request.path, "https://psim.test").searchParams.get("certificatePem")).toBe("CERT\n+&");
  expect(saveRequest("locations", { name: "new" }).method).toBe("POST");
});
it("preserves immutable identities while saving a new draft and versioning a mapping", () => {
  for (const key of ["rules", "plans"] as const) {
    const request = saveRequest(key, { name: "new", state: "old" }, { version: 4 });
    expect(request.version).toBe("4");
    expect(JSON.parse(typeof request.body === "string" ? request.body : "")).toMatchObject({
      version: 5,
      state: key === "rules" ? "RULE_STATE_DRAFT" : "PLAN_STATE_DRAFT",
    });
  }
  expect(saveRequest("mappings", { connectorType: "vendor/test" }).path).toBe("/v1/mappings/vendor%2Ftest");
  expect(saveRequest("mappings", {}).version).toBe("0");
});
it("provides forms for every resource and strips server metadata without changing the source", () => {
  for (const key of Object.keys(resources) as ResourceKey[])
    expect(resolve({ ref: resources[key].schema }).properties).toBeDefined();
  for (const key of Object.keys(resources) as ResourceKey[]) expect(newRecord(key)).toBeTypeOf("object");
  const source = { version: "1", state: "active", updatedAt: "private", certificates: [], floorPlan: {}, name: "Name" };
  expect(editable("maintenance", source)).toEqual({ version: "1", name: "Name" });
  expect(source.updatedAt).toBe("private");
  expect(record(null)).toEqual({});
  expect(record([])).toEqual({});
  expect(record({ test: true })).toEqual({ test: true });
  expect(string(false)).toBe("");
  expect(string(1)).toBe("1");
  expect(list("x")).toEqual([]);
  expect(list([1])).toEqual([1]);
  expect(initial({ enum: ["UNSPECIFIED_UNSPECIFIED", "ACTIVE"] })).toBe("ACTIVE");
  expect(initial({ enum: [] })).toBe("");
  expect(initial({ type: "boolean" })).toBe(false);
  expect(initial({ type: "integer" })).toBe(0);
  expect(initial({ type: "array" })).toEqual([]);
  expect(initial({ type: "object" })).toEqual({});
  expect(initial({})).toBe("");
  expect(resolve({ ref: "missing" })).toEqual({});
});
it("accepts only supported files and never constructs an identity link from an unsafe issuer", () => {
  expect(validFile(new File(["x"], "devices.csv"))).toBe(true);
  expect(validFile(new File(["x"], "devices.JSON"))).toBe(true);
  expect(validFile(new File([], "devices.csv"))).toBe(false);
  expect(validFile(new File(["x"], "devices.exe"))).toBe(false);
  expect(validFile(new File(["x"], "plan.png", { type: "image/png" }), true)).toBe(true);
  expect(validFile(new File(["x"], "plan.svg", { type: "image/svg+xml" }), true)).toBe(false);
  const huge = new File(["x"], "large.csv");
  Object.defineProperty(huge, "size", { value: 65 * 1024 * 1024 });
  expect(validFile(huge)).toBe(false);
  expect(identityConsole("https://identity.test/auth/realms/psim")).toBe(
    "https://identity.test/auth/admin/psim/console/",
  );
  for (const issuer of [
    "javascript:alert(1)",
    "broken",
    "https://u:p@host/realms/psim",
    "https://host/",
    "https://host/realms/",
    "https://host/realms/a/b",
  ])
    expect(identityConsole(issuer)).toBeUndefined();
});
