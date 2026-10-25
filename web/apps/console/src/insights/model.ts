import type { components, paths } from "@psim/api-client";
import { SessionHttp, write } from "../ui/http";
export type Schema = components["schemas"];
export type AuditRecord = Schema["psim.audit.v1.AuditChainEntry"];
export type Verification = Schema["psim.api.v1.VerifyAuditIntegrityResponse"];
export type Metrics = Schema["psim.api.v1.IncidentMetrics"];
export type Report = Schema["psim.api.v1.GetIncidentReportResponse"];
export type Me = Schema["psim.api.v1.GetMeResponse"];
export const groupings = ["NONE", "SITE", "INCIDENT_TYPE", "PRIORITY", "OPERATOR", "DAY"] as const;
export type Grouping = (typeof groupings)[number];
export const priorities = ["PRIORITY_P1", "PRIORITY_P2", "PRIORITY_P3", "PRIORITY_P4"] as const;
export interface Filter {
  readonly from: string;
  readonly to: string;
  readonly site: string;
  readonly actor: string;
  readonly action: string;
  readonly objectType: string;
  readonly objectId: string;
  readonly outcome: string;
  readonly types: string;
  readonly priority: readonly string[];
  readonly grouping: Grouping;
}
export function initialFilter(audit: boolean): Filter {
  const now = Date.now();
  return {
    from: new Date(now - (audit ? 1 : 30) * 86400000).toISOString().slice(0, 19),
    to: new Date(now).toISOString().slice(0, 19),
    site: "",
    actor: "",
    action: "",
    objectType: "",
    objectId: "",
    outcome: "",
    types: "",
    priority: [],
    grouping: "DAY",
  };
}
export function validRange(filter: Filter, audit = false) {
  const from = Date.parse(filter.from + "Z"),
    to = Date.parse(filter.to + "Z");
  return Number.isFinite(from) && Number.isFinite(to) && to > from && (audit || to - from <= 400 * 86400000);
}
export function parameters(filter: Filter, audit: boolean): string {
  const query = new URLSearchParams();
  query.set(audit ? "query.from" : "filter.from", new Date(filter.from + "Z").toISOString());
  query.set(audit ? "query.to" : "filter.to", new Date(filter.to + "Z").toISOString());
  if (audit) {
    const values = {
      actorId: filter.actor,
      action: filter.action,
      objectType: filter.objectType,
      objectId: filter.objectId,
      siteId: filter.site,
      outcome: filter.outcome,
    };
    for (const [key, value] of Object.entries(values)) if (value.trim()) query.set(`query.${key}`, value.trim());
  } else {
    for (const [key, value] of [
      ["siteIds", filter.site],
      ["incidentTypeIds", filter.types],
    ] as const)
      for (const item of value.split(/[\s,]+/u).filter(Boolean)) query.append(`filter.${key}`, item);
    for (const value of filter.priority) query.append("filter.priorities", value);
    query.set("grouping", `REPORT_GROUPING_${filter.grouping}`);
  }
  return query.toString();
}
export function validSequence(from: string, to: string) {
  const valid = (value: string) => /^\d{1,20}$/u.test(value) && BigInt(value) <= 18446744073709551615n;
  return valid(from) && valid(to) && (BigInt(to) === 0n || BigInt(to) >= BigInt(from));
}
export function integer(value: string | undefined, locale: string): string {
  return /^\d+$/u.test(value ?? "0") ? new Intl.NumberFormat(locale).format(BigInt(value ?? "0")) : "-";
}
export function seconds(value: string | undefined): number {
  return /^(?:0|[1-9]\d*)(?:\.\d{1,9})?s$/u.test(value ?? "0s") ? Number((value ?? "0s").slice(0, -1)) : 0;
}
export class InsightsApi extends SessionHttp {
  async me(signal: AbortSignal) {
    return (await this.get("/v1/me", signal)) as Me;
  }
  async search(query: string, page: string, signal: AbortSignal) {
    const params = new URLSearchParams(query);
    params.set("pageSize", "100");
    if (page) params.set("pageToken", page);
    const path: keyof paths = "/v1/audit/records";
    return (await this.get(`${path}?${params}`, signal)) as Schema["psim.api.v1.SearchAuditRecordsResponse"];
  }
  async report(query: string, signal: AbortSignal) {
    const path: keyof paths = "/v1/reports/incidents";
    return (await this.get(`${path}?${query}`, signal)) as Report;
  }
  async verify(fromSeq: string, toSeq: string, signal: AbortSignal) {
    return (await this.send(
      write("/v1/audit:verify", { fromSeq: BigInt(fromSeq).toString(), toSeq: BigInt(toSeq).toString() }),
      signal,
    )) as Verification;
  }
}
