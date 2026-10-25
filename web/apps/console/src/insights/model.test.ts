import { expect, it, vi } from "vitest";
import { InsightsApi, initialFilter, integer, parameters, seconds, validRange, validSequence } from "./model";
import { metric } from "./page";
it("serializes applied audit filters, repeated report filters and a fixed UTC period", () => {
  const filter = {
    ...initialFilter(false),
    site: "a, b c",
    types: "type-a,type-b",
    priority: ["PRIORITY_P1", "PRIORITY_P4"],
    actor: "actor",
    action: "incident.",
    objectType: "incident",
    objectId: "object",
    outcome: "AUDIT_OUTCOME_DENIED",
  };
  const report = new URLSearchParams(parameters(filter, false));
  expect(report.getAll("filter.siteIds")).toEqual(["a", "b", "c"]);
  expect(report.getAll("filter.incidentTypeIds")).toEqual(["type-a", "type-b"]);
  expect(report.getAll("filter.priorities")).toEqual(filter.priority);
  expect(report.get("grouping")).toBe("REPORT_GROUPING_DAY");
  const audit = new URLSearchParams(parameters(filter, true));
  expect(audit.get("query.actorId")).toBe("actor");
  expect(audit.get("query.siteId")).toBe("a, b c");
  expect(audit.get("query.action")).toBe("incident.");
  expect(audit.get("query.to")).toMatch(/Z$/u);
  const empty = new URLSearchParams(parameters(initialFilter(true), true));
  expect(empty.has("query.siteId")).toBe(false);
});
it("bounds periods and sequences without converting uint64 to floating point", () => {
  const f = initialFilter(false);
  expect(validRange(f)).toBe(true);
  expect(validRange(initialFilter(true))).toBe(true);
  expect(validRange({ ...f, from: "" })).toBe(false);
  expect(validRange({ ...f, to: f.from })).toBe(false);
  expect(validRange({ ...f, to: new Date(Date.parse(f.from + "Z") + 401 * 86400000).toISOString().slice(0, 19) })).toBe(
    false,
  );
  for (const [from, to, expected] of [
    ["0", "0", true],
    ["9007199254740993", "18446744073709551615", true],
    ["18446744073709551616", "0", false],
    ["9", "8", false],
    ["9", "00", true],
    ["-1", "0", false],
    ["", "0", false],
    ["1", "001", true],
  ] as const)
    expect(validSequence(from, to)).toBe(expected);
  expect(integer("18446744073709551615", "en")).toBe("18,446,744,073,709,551,615");
  expect(integer(undefined, "en")).toBe("0");
  expect(integer("nan", "ru")).toBe("-");
});
it("formats server metrics and keeps absent SLA observations distinct from zero compliance", () => {
  expect(metric({}, "ackSlaCompliance", "en", "none")).toBe("none");
  expect(metric({ ackSlaEvaluated: "1" }, "ackSlaCompliance", "en", "none")).toBe("0%");
  expect(metric({ resolveSlaEvaluated: "3", resolveSlaCompliance: 1 }, "resolveSlaCompliance", "en", "none")).toBe(
    "100%",
  );
  expect(metric({ incidentsResolved: "0" }, "falseAlarmRate", "en", "none")).toBe("none");
  expect(metric({ incidentsResolved: "5", falseAlarmRate: 0.2 }, "falseAlarmRate", "en", "none")).toBe("20%");
  expect(metric({ mtta: "1.123456789s" }, "mtta", "en", "none")).toBe("1.123456789");
  expect(metric({}, "mttr", "en", "none")).toBe("0");
  expect(metric({}, "escalated", "ru", "none")).toBe("0");
  expect(seconds("bad")).toBe(0);
  expect(seconds("-1s")).toBe(0);
  expect(seconds(undefined)).toBe(0);
});
it("uses authenticated, cancellable public endpoints and never rounds sequence requests", async () => {
  const fetcher = vi.fn<typeof fetch>().mockImplementation(() => Promise.resolve(Response.json({})));
  const api = new InsightsApi(() => Promise.resolve("current"), fetcher);
  const controller = new AbortController();
  await api.me(controller.signal);
  await api.search("query.actorId=x", "cursor", controller.signal);
  await api.report("filter.siteIds=a", controller.signal);
  await api.verify("9007199254740993", "0", controller.signal);
  expect(fetcher.mock.calls[1]?.[0]).toEqual(expect.stringContaining("pageToken=cursor"));
  expect(fetcher.mock.calls[3]?.[1]?.body).toBe('{"fromSeq":"9007199254740993","toSeq":"0"}');
  controller.abort();
  await expect(api.verify("0", "0", controller.signal)).rejects.toMatchObject({ name: "AbortError" });
  expect(fetcher).toHaveBeenCalledTimes(4);
});
