import { useCallback, useEffect, useId, useLayoutEffect, useMemo, useRef, useState } from "react";
import { QueryClient, QueryClientProvider, useQuery } from "@tanstack/react-query";
import { Button, Field, Notice, TableContainer } from "@psim/design-system";
import { useShell } from "../shell/context";
import { status } from "../ui/http";
import { formatAbsolute } from "../ui/format";
import {
  InsightsApi,
  groupings,
  initialFilter,
  integer,
  parameters,
  priorities,
  seconds,
  validRange,
  validSequence,
  type AuditRecord,
  type Filter,
  type Grouping,
  type Metrics,
  type Verification,
} from "./model";
import { useInsightsStrings, type Strings } from "./strings";
interface Access {
  readonly api: InsightsApi;
  readonly online: boolean;
  readonly global: boolean;
  readonly denied: () => void;
}
function useDenied(error: unknown, denied: () => void) {
  useEffect(() => {
    if ([401, 403].includes(status(error))) denied();
  }, [error, denied]);
}
function time(value: string | undefined, language: string) {
  const parsed = Date.parse(value ?? "");
  return Number.isFinite(parsed) ? formatAbsolute(language === "en" ? "en" : "ru", parsed, "UTC") + " UTC" : "-";
}
function Export({
  access,
  query,
  audit,
  disabled,
}: {
  readonly access: Access;
  readonly query: string;
  readonly audit: boolean;
  readonly disabled: boolean;
}) {
  const { t } = useInsightsStrings();
  const [state, setState] = useState<"idle" | "busy" | "error" | "done">("idle");
  const current = useRef<AbortController | undefined>(undefined);
  const url = useRef<string | undefined>(undefined);
  useEffect(
    () => () => {
      current.current?.abort();
      if (url.current) URL.revokeObjectURL(url.current);
    },
    [],
  );
  const download = async () => {
    if ((current.current && !current.current.signal.aborted) || disabled) return;
    const controller = new AbortController();
    current.current = controller;
    setState("busy");
    try {
      const blob = await access.api.blob(
        `/v1/${audit ? "audit/records" : "reports/incidents"}:export?${query}`,
        controller.signal,
      );
      controller.signal.throwIfAborted();
      if (blob.type.toLowerCase().split(";")[0]?.trim() !== (audit ? "application/x-ndjson" : "text/csv"))
        throw new Error("Unexpected export content type");
      if (url.current) URL.revokeObjectURL(url.current);
      url.current = URL.createObjectURL(blob);
      const link = document.createElement("a");
      link.href = url.current;
      link.download = audit ? "psim-audit.jsonl" : "psim-incidents.csv";
      document.body.append(link);
      link.click();
      link.remove();
      setState("done");
    } catch (error) {
      if (!controller.signal.aborted) {
        if ([401, 403].includes(status(error))) access.denied();
        setState("error");
      }
    } finally {
      if (current.current === controller) current.current = undefined;
    }
  };
  return (
    <div>
      <Button
        disabled={disabled || state === "busy"}
        onClick={() => {
          void download();
        }}
      >
        {audit ? t.exportAudit : t.exportReport}
      </Button>
      {state !== "idle" && (
        <p role={state === "error" ? "alert" : "status"}>
          {state === "busy" ? t.downloadBusy : state === "error" ? t.downloadError : t.downloadDone}
        </p>
      )}
    </div>
  );
}
function Integrity({ access }: { readonly access: Access }) {
  const { t, language } = useInsightsStrings();
  const [from, setFrom] = useState("0"),
    [to, setTo] = useState("0"),
    [result, setResult] = useState<Verification>(),
    [error, setError] = useState(false),
    [busy, setBusy] = useState(false);
  const controller = useRef<AbortController | undefined>(undefined);
  useEffect(() => () => controller.current?.abort(), []);
  const valid = validSequence(from, to);
  const check = async () => {
    if (!valid || !access.online || controller.current) return;
    const request = new AbortController();
    controller.current = request;
    setResult(undefined);
    setError(false);
    setBusy(true);
    try {
      const value = await access.api.verify(from, to, request.signal);
      request.signal.throwIfAborted();
      setResult(value);
    } catch (error) {
      if (!request.signal.aborted) {
        if ([401, 403].includes(status(error))) access.denied();
        setError(true);
      }
    } finally {
      if (!request.signal.aborted) setBusy(false);
      if (controller.current === request) controller.current = undefined;
    }
  };
  const checkpoint = result?.lastCheckpoint;
  return (
    <section className="insights-section">
      <h2>{t.verifyTitle}</h2>
      <p>{t.chainHelp}</p>
      {!access.global ? (
        <Notice>{t.global}</Notice>
      ) : (
        <>
          <form
            onSubmit={(event) => {
              event.preventDefault();
              void check();
            }}
            className="insights-fields"
          >
            <Field
              label={t.fromSeq}
              inputMode="numeric"
              value={from}
              disabled={busy}
              onChange={(event) => {
                setFrom(event.target.value);
                setResult(undefined);
              }}
            />
            <Field
              label={t.toSeq}
              inputMode="numeric"
              value={to}
              disabled={busy}
              onChange={(event) => {
                setTo(event.target.value);
                setResult(undefined);
              }}
            />
            <Button type="submit" disabled={!valid || busy || !access.online}>
              {t.verify}
            </Button>
          </form>
          {!valid && <p role="alert">{t.sequenceError}</p>}
          {busy && <p role="status">{t.loading}</p>}
          {error && <Notice tone="warning">{t.error}</Notice>}
          {result && (
            <div data-testid="integrity-result">
              <Notice tone={result.intact === true ? "success" : "danger"}>
                {result.intact === true ? t.intact : t.broken}
              </Notice>
              <dl className="insights-details">
                <dt>{t.checked}</dt>
                <dd>{integer(result.recordsChecked, language)}</dd>
                <dt>{t.firstBroken}</dt>
                <dd>{result.firstBrokenSeq && result.firstBrokenSeq !== "0" ? result.firstBrokenSeq : "-"}</dd>
                <dt>{t.details}</dt>
                <dd>{result.detail ?? "-"}</dd>
              </dl>
              <h3>{checkpoint?.signature ? t.checkpoint : t.noCheckpoint}</h3>
              {checkpoint && (
                <dl className="insights-details">
                  {[
                    [t.sequence, checkpoint.auditSeq],
                    [t.hash, checkpoint.hash],
                    [t.key, checkpoint.keyId],
                    [t.signature, checkpoint.signature],
                    [t.tenant, checkpoint.tenantId],
                    [t.recorded, time(checkpoint.createdAt, language)],
                  ].map(([label, value]) => (
                    <div key={label}>
                      <dt>{label}</dt>
                      <dd>{value ?? "-"}</dd>
                    </div>
                  ))}
                </dl>
              )}
            </div>
          )}
        </>
      )}
    </section>
  );
}
function AuditDetails({ row }: { readonly row: AuditRecord }) {
  const { t, language } = useInsightsStrings();
  const entry = row.entry;
  return (
    <details>
      <summary>
        {t.record} {row.auditSeq}
      </summary>
      <dl className="insights-details">
        {[
          [t.tenant, row.tenantId],
          [t.source, row.sourceEventId],
          [t.actor, entry?.actor?.id],
          [t.action, entry?.action],
          [t.objectType, entry?.objectType],
          [t.objectId, entry?.objectId],
          [t.site, entry?.siteId],
          [t.outcome, entry?.outcome],
          [t.occurred, time(row.occurredAt, language)],
          [t.recorded, time(row.recordedAt, language)],
          [t.prevHash, row.prevHash],
          [t.hash, row.hash],
        ].map(([label, value]) => (
          <div key={label}>
            <dt>{label}</dt>
            <dd>{value ?? "-"}</dd>
          </div>
        ))}
        {Object.entries(entry?.details ?? {}).map(([key, value]) => (
          <div key={`detail-${key}`}>
            <dt>{key}</dt>
            <dd>{value}</dd>
          </div>
        ))}
      </dl>
    </details>
  );
}
function Audit({
  access,
  query,
  revision,
}: {
  readonly access: Access;
  readonly query: string;
  readonly revision: number;
}) {
  const { t, language } = useInsightsStrings();
  const [pages, setPages] = useState([""]),
    [page, setPage] = useState(0);
  const result = useQuery({
    queryKey: ["audit", query, revision, pages[page]],
    queryFn: ({ signal }) => access.api.search(query, pages[page] ?? "", signal),
    retry: false,
  });
  useDenied(result.error, access.denied);
  return (
    <section className="insights-section" aria-label={t.results}>
      {result.isFetching && <p role="status">{t.loading}</p>}
      {result.isError && <Notice tone="warning">{t.error}</Notice>}
      {result.isSuccess && !result.isFetching && (
        <>
          {!result.data.records?.length ? (
            <Notice>{t.empty}</Notice>
          ) : (
            <TableContainer className="insights-table" label={t.audit}>
              <table>
                <caption>{t.audit}</caption>
                <thead>
                  <tr>
                    {[t.sequence, t.occurred, t.actor, t.action, t.objectId, t.details].map((label) => (
                      <th key={label} scope="col">
                        {label}
                      </th>
                    ))}
                  </tr>
                </thead>
                <tbody>
                  {result.data.records.map((row) => (
                    <tr key={row.auditSeq}>
                      <td>{row.auditSeq}</td>
                      <td>{time(row.occurredAt, language)}</td>
                      <td>{row.entry?.actor?.id}</td>
                      <td>{row.entry?.action}</td>
                      <td>{row.entry?.objectId}</td>
                      <td>
                        <AuditDetails row={row} />
                      </td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </TableContainer>
          )}
          <nav className="insights-actions" aria-label={t.page}>
            <Button
              disabled={!page || !access.online}
              onClick={() => {
                setPage(page - 1);
              }}
            >
              {t.previous}
            </Button>
            <span>
              {t.page} {page + 1}
            </span>
            <Button
              disabled={!result.data.nextPageToken || !access.online}
              onClick={() => {
                setPages([...pages.slice(0, page + 1), result.data.nextPageToken ?? ""]);
                setPage(page + 1);
              }}
            >
              {t.next}
            </Button>
          </nav>
        </>
      )}
    </section>
  );
}
const metricKeys = [
  "incidentsCreated",
  "incidentsResolved",
  "mtta",
  "mttr",
  "falseAlarmRate",
  "ackSlaCompliance",
  "resolveSlaCompliance",
  "escalated",
  "ackSlaEvaluated",
  "resolveSlaEvaluated",
] as const;
const metricLabels: Record<(typeof metricKeys)[number], keyof Strings> = {
  incidentsCreated: "created",
  incidentsResolved: "resolved",
  mtta: "mtta",
  mttr: "mttr",
  falseAlarmRate: "falseAlarm",
  ackSlaCompliance: "ackSla",
  resolveSlaCompliance: "resolveSla",
  escalated: "escalated",
  ackSlaEvaluated: "ackEvaluated",
  resolveSlaEvaluated: "resolveEvaluated",
};
export function metric(row: Metrics, key: (typeof metricKeys)[number], language: string, noSamples: string): string {
  if (key === "mtta" || key === "mttr")
    return new Intl.NumberFormat(language, { maximumFractionDigits: 9 }).format(seconds(row[key]));
  if (key === "falseAlarmRate" || key === "ackSlaCompliance" || key === "resolveSlaCompliance") {
    const observations =
      key === "falseAlarmRate"
        ? row.incidentsResolved
        : key === "ackSlaCompliance"
          ? row.ackSlaEvaluated
          : row.resolveSlaEvaluated;
    return !observations || BigInt(observations) === 0n
      ? noSamples
      : new Intl.NumberFormat(language, { style: "percent", maximumFractionDigits: 2 }).format(row[key] ?? 0);
  }
  return integer(row[key], language);
}
function group(row: Metrics, grouping: Grouping, t: Strings) {
  return grouping === "NONE"
    ? t.NONE
    : grouping === "PRIORITY" && row.group
      ? `P${row.group}`
      : row.group === "" || row.group === undefined
        ? t.unassigned
        : row.group;
}
function Chart({
  rows,
  grouping,
  duration,
}: {
  readonly rows: readonly Metrics[];
  readonly grouping: Grouping;
  readonly duration: boolean;
}) {
  const { t, language } = useInsightsStrings();
  const id = useId();
  const keys = duration ? (["mtta", "mttr"] as const) : (["incidentsCreated", "incidentsResolved"] as const);
  const values = rows.map((row) =>
    keys.map((key) => (duration ? BigInt(Math.round(seconds(row[key]) * 1000)) : BigInt(row[key] ?? "0"))),
  );
  const max = values.flat().reduce((a, b) => (a > b ? a : b), 1n);
  return (
    <figure>
      <figcaption id={id}>{duration ? t.durations : t.counts}</figcaption>
      <p>{keys.map((key) => t[metricLabels[key]]).join(" / ")}</p>
      {/* eslint-disable-next-line jsx-a11y/no-noninteractive-tabindex -- a scrollable region must take keyboard focus */}
      <div className="insights-chart-scroll" role="region" aria-labelledby={id} tabIndex={0}>
        <svg role="img" aria-labelledby={id} viewBox={`0 0 640 ${String(Math.max(rows.length, 1) * 48)}`}>
          {rows.map((row, index) => (
            <g key={row.group ?? ""} transform={`translate(0 ${String(index * 48)})`}>
              <text x="0" y="12">
                {group(row, grouping, t)}
              </text>
              {keys.map((key, series) => (
                <rect
                  key={key}
                  className={series ? "insights-series-secondary" : "insights-series-primary"}
                  x="0"
                  y={18 + series * 12}
                  height="8"
                  width={Number(((values[index]?.[series] ?? 0n) * 640n) / max)}
                >
                  <title>{`${group(row, grouping, t)}: ${t[metricLabels[key]]} ${metric(row, key, language, t.noSamples)}`}</title>
                </rect>
              ))}
            </g>
          ))}
        </svg>
      </div>
    </figure>
  );
}
function ReportResult({
  access,
  query,
  revision,
  grouping,
}: {
  readonly access: Access;
  readonly query: string;
  readonly revision: number;
  readonly grouping: Grouping;
}) {
  const { t, language } = useInsightsStrings();
  const [page, setPage] = useState(0);
  const result = useQuery({
    queryKey: ["report", query, revision],
    queryFn: ({ signal }) => access.api.report(query, signal),
    retry: false,
  });
  useDenied(result.error, access.denied);
  const rows = grouping === "NONE" ? [] : (result.data?.rows ?? []),
    visible = rows.slice(page * 50, (page + 1) * 50);
  return (
    <section className="insights-section" aria-label={t.results}>
      {result.isFetching && <p role="status">{t.loading}</p>}
      {result.isError && <Notice tone="warning">{t.error}</Notice>}
      {result.isSuccess && !result.isFetching && (
        <>
          <p>
            {t.generated}: {time(result.data.generatedAt, language)}
          </p>
          <h2>{t.NONE}</h2>
          <dl className="insights-totals">
            {metricKeys.map((key) => (
              <div key={key}>
                <dt>{t[metricLabels[key]]}</dt>
                <dd data-testid={`total-${key}`}>{metric(result.data.totals ?? {}, key, language, t.noSamples)}</dd>
              </div>
            ))}
          </dl>
          {!rows.length && grouping !== "NONE" && <Notice>{t.empty}</Notice>}
          {(visible.length > 0 || grouping === "NONE") && (
            <>
              <p>{t.chartHelp}</p>
              <div className="insights-charts">
                <Chart
                  rows={(grouping === "NONE" ? [result.data.totals ?? {}] : visible).slice(0, 20)}
                  grouping={grouping}
                  duration={false}
                />
                <Chart
                  rows={(grouping === "NONE" ? [result.data.totals ?? {}] : visible).slice(0, 20)}
                  grouping={grouping}
                  duration
                />
              </div>
            </>
          )}
          {rows.length > 0 && (
            <>
              <p>
                {t.shown}: {visible.length} / {rows.length}
              </p>
              <TableContainer className="insights-table" label={t.reports}>
                <table>
                  <caption>{t.reports}</caption>
                  <thead>
                    <tr>
                      <th scope="col">{t.group}</th>
                      {metricKeys.map((key) => (
                        <th key={key} scope="col">
                          {t[metricLabels[key]]}
                        </th>
                      ))}
                    </tr>
                  </thead>
                  <tbody>
                    {visible.map((row) => (
                      <tr key={row.group ?? ""}>
                        <th scope="row">{group(row, grouping, t)}</th>
                        {metricKeys.map((key) => (
                          <td key={key}>{metric(row, key, language, t.noSamples)}</td>
                        ))}
                      </tr>
                    ))}
                  </tbody>
                </table>
              </TableContainer>
              <nav className="insights-actions" aria-label={t.page}>
                <Button
                  disabled={!page}
                  onClick={() => {
                    setPage(page - 1);
                  }}
                >
                  {t.previous}
                </Button>
                <span>
                  {t.page} {page + 1}
                </span>
                <Button
                  disabled={(page + 1) * 50 >= rows.length}
                  onClick={() => {
                    setPage(page + 1);
                  }}
                >
                  {t.next}
                </Button>
              </nav>
            </>
          )}
        </>
      )}
    </section>
  );
}
function Workspace({ access, audit }: { readonly access: Access; readonly audit: boolean }) {
  const { t } = useInsightsStrings();
  const [filter, setFilter] = useState(() => initialFilter(audit));
  const [applied, setApplied] = useState<{ query: string; revision: number; filter: Filter }>();
  const valid = validRange(filter, audit),
    query = valid ? parameters(filter, audit) : "",
    dirty = applied?.query !== query;
  const update = <K extends keyof Filter>(key: K, value: Filter[K]) => {
    setFilter((previous) => ({ ...previous, [key]: value }));
  };
  const text = (
    key: "from" | "to" | "site" | "actor" | "action" | "objectType" | "objectId" | "types",
    label: string,
  ) => (
    <Field
      key={key}
      label={label}
      type={key === "from" || key === "to" ? "datetime-local" : "text"}
      step={key === "from" || key === "to" ? "1" : undefined}
      value={filter[key]}
      onChange={(event) => {
        update(key, event.target.value);
      }}
    />
  );
  return (
    <>
      <p>{audit ? t.exportHelp : t.reportHelp}</p>
      <form
        className="insights-fields"
        onSubmit={(event) => {
          event.preventDefault();
          if (valid && access.online)
            setApplied({ query, filter: { ...filter }, revision: (applied?.revision ?? 0) + 1 });
        }}
      >
        {text("from", t.from)}
        {text("to", audit ? t.auditTo : t.to)}
        {text("site", audit ? t.site : t.sites)}
        {audit ? (
          <>
            {text("actor", t.actor)}
            {text("action", t.action)}
            {text("objectType", t.objectType)}
            {text("objectId", t.objectId)}
            <label>
              {t.outcome}
              <select
                value={filter.outcome}
                onChange={(event) => {
                  update("outcome", event.target.value);
                }}
              >
                <option value="">{t.all}</option>
                <option value="AUDIT_OUTCOME_SUCCESS">{t.success}</option>
                <option value="AUDIT_OUTCOME_DENIED">{t.denied}</option>
                <option value="AUDIT_OUTCOME_FAILED">{t.failed}</option>
              </select>
            </label>
          </>
        ) : (
          <>
            {text("types", t.types)}
            <label>
              {t.priority}
              <select
                multiple
                value={[...filter.priority]}
                onChange={(event) => {
                  update(
                    "priority",
                    Array.from(event.target.selectedOptions, (option) => option.value),
                  );
                }}
              >
                {priorities.map((priority) => (
                  <option key={priority} value={priority}>
                    {priority.replace("PRIORITY_", "")}
                  </option>
                ))}
              </select>
            </label>
            <label>
              {t.grouping}
              <select
                value={filter.grouping}
                onChange={(event) => {
                  update("grouping", event.target.value as Grouping);
                }}
              >
                {groupings.map((group) => (
                  <option key={group} value={group}>
                    {t[group]}
                  </option>
                ))}
              </select>
            </label>
          </>
        )}
        <Button type="submit" disabled={!valid || !access.online}>
          {audit ? t.search : t.build}
        </Button>
      </form>
      {!audit && <p>{t.filterHelp}</p>}
      {!valid && <p role="alert">{audit ? t.auditRange : t.range}</p>}
      {applied && dirty && <p role="status">{t.apply}</p>}
      {applied && (
        <>
          <h2>{t.results}</h2>
          <p>
            {applied.filter.from} UTC - {applied.filter.to} UTC
          </p>
          <Export
            key={applied.query + String(applied.revision)}
            access={access}
            query={applied.query}
            audit={audit}
            disabled={dirty || !access.online || (audit && !access.global)}
          />
          {audit ? (
            <Audit key={applied.revision} access={access} query={applied.query} revision={applied.revision} />
          ) : (
            <ReportResult
              key={applied.revision}
              access={access}
              query={applied.query}
              revision={applied.revision}
              grouping={applied.filter.grouping}
            />
          )}
        </>
      )}
      {audit && <Integrity access={access} />}
    </>
  );
}
function Boundary({ access, audit }: { readonly access: Access; readonly audit: boolean }) {
  const [client] = useState(
    () => new QueryClient({ defaultOptions: { queries: { retry: false, refetchOnWindowFocus: false } } }),
  );
  useEffect(
    () => () => {
      void client.cancelQueries();
      client.clear();
    },
    [client],
  );
  return (
    <QueryClientProvider client={client}>
      <Workspace access={access} audit={audit} />
    </QueryClientProvider>
  );
}
function Session({ audit }: { readonly audit: boolean }) {
  const { shell, session } = useShell();
  const { t } = useInsightsStrings();
  const [client] = useState(() => new QueryClient());
  const api = useMemo(() => new InsightsApi(() => shell.auth.token()), [shell]);
  useLayoutEffect(() => {
    api.activate();
    return () => {
      api.dispose();
      void client.cancelQueries();
      client.clear();
    };
  }, [api, client]);
  return (
    <QueryClientProvider client={client}>
      <AccessGate
        api={api}
        audit={audit}
        context={session.profile?.context ?? ""}
        title={audit ? t.audit : t.reports}
      />
    </QueryClientProvider>
  );
}
function AccessGate({
  api,
  audit,
  context,
  title,
}: {
  readonly api: InsightsApi;
  readonly audit: boolean;
  readonly context: string;
  readonly title: string;
}) {
  const { t } = useInsightsStrings();
  const [online, setOnline] = useState(navigator.onLine),
    [revoked, setRevoked] = useState(false);
  useEffect(() => {
    const update = () => {
      setOnline(navigator.onLine);
    };
    window.addEventListener("online", update);
    window.addEventListener("offline", update);
    return () => {
      window.removeEventListener("online", update);
      window.removeEventListener("offline", update);
    };
  }, []);
  const me = useQuery({
    queryKey: ["insights", "me"],
    queryFn: ({ signal }) => api.me(signal),
    retry: false,
    refetchInterval: 5000,
    refetchOnWindowFocus: true,
  });
  const denied = useCallback(() => {
    setRevoked(true);
  }, []);
  const role = me.data?.roles?.some((role) => (audit ? ["auditor"] : ["manager", "supervisor"]).includes(role));
  const blocked = revoked || [401, 403].includes(status(me.error)) || (me.isSuccess && !role);
  const scope = JSON.stringify([
    context,
    me.data?.userId,
    me.data?.tenantId,
    me.data?.roles,
    me.data?.siteIds,
    me.data?.zoneIds,
    me.data?.actions,
  ]);
  return (
    <main id="main-content" tabIndex={-1} className="workbench">
      <h1>{title}</h1>
      {!online && <Notice tone="warning">{t.offline}</Notice>}
      {blocked ? (
        <>
          <Notice tone="danger">{t.access}</Notice>
          <Button
            onClick={() => {
              void me.refetch().then((value) => {
                if (value.isSuccess) setRevoked(false);
              });
            }}
          >
            {t.refresh}
          </Button>
        </>
      ) : me.data && role ? (
        <>
          <p>{me.data.displayName}</p>
          {me.isError && <Notice tone="warning">{t.error}</Notice>}
          <Boundary
            key={scope}
            access={{ api, online: online && me.isSuccess, global: me.data.siteIds?.includes("*") ?? false, denied }}
            audit={audit}
          />
        </>
      ) : me.isError ? (
        <>
          <Notice tone="warning">{t.error}</Notice>
          <Button
            onClick={() => {
              void me.refetch();
            }}
          >
            {t.refresh}
          </Button>
        </>
      ) : (
        <p role="status">{t.loading}</p>
      )}
    </main>
  );
}
export function InsightsPage({ audit }: { readonly audit: boolean }) {
  const { session } = useShell();
  return <Session key={`${session.profile?.context ?? ""}/${String(audit)}`} audit={audit} />;
}
