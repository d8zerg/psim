import { OperationalDemo } from "../demo/entry";
import {
  cardVariants,
  Badge,
  Button,
  Card,
  DataRegion,
  EmptyState,
  Field,
  Notice,
  SelectField,
  Switch,
  TableContainer,
  type Tone,
} from "@psim/design-system";
import { ConfirmAction } from "@psim/design-system/confirm-action";
import { QueryClient, QueryClientProvider, useQuery } from "@tanstack/react-query";
import { useDeferredValue, useRef, useState, type ReactNode } from "react";
import { message } from "../ui/intl";
import { playAlert } from "../ui/alert-sound";
import { setPreferences, usePreferences } from "../ui/preferences";
import { useDemoStrings, type Strings } from "../ui/strings";

type StringKey = { [K in keyof Strings]: Strings[K] extends string ? K : never }[keyof Strings];
type Code = "P1" | "P2" | "P3" | "P4";
interface RecordRow {
  readonly id: string;
  readonly title: StringKey;
  readonly object: StringKey;
  readonly code: Code;
  readonly state: StringKey;
  readonly age: number;
}

const firstRecord: RecordRow = {
  id: "INC-1042",
  title: "smoke",
  object: "serverRoom",
  code: "P1",
  state: "new",
  age: 4,
};
const records: readonly RecordRow[] = [
  firstRecord,
  { id: "INC-1041", title: "alarm", object: "entrance", code: "P2", state: "acknowledged", age: 12 },
  { id: "INC-1038", title: "lost", object: "corridor", code: "P3", state: "working", age: 38 },
  { id: "INC-1035", title: "battery", object: "storage", code: "P4", state: "working", age: 64 },
];
const priorities: Record<Code, { readonly label: StringKey; readonly tone: Tone }> = {
  P1: { label: "critical", tone: "danger" },
  P2: { label: "high", tone: "warning" },
  P3: { label: "normal", tone: "neutral" },
  P4: { label: "low", tone: "neutral" },
};
const states: Partial<Record<StringKey, Tone>> = { new: "warning", acknowledged: "neutral", working: "neutral" };
const tokens = [
  "background",
  "card",
  "muted",
  "border",
  "input",
  "foreground",
  "muted-foreground",
  "primary",
  "link",
  "danger",
  "warning",
  "success",
  "info",
] as const;

function Priority({ code }: { readonly code: Code }) {
  const t = useDemoStrings();
  const { label, tone } = priorities[code];
  return (
    <Badge tone={tone} emphasis={code === "P1" ? "solid" : "subtle"}>
      {t[label]}
    </Badge>
  );
}

function ScenarioHeader({
  id,
  codes,
  roles,
  title,
  description,
  aside,
}: {
  readonly id: string;
  readonly codes?: string;
  readonly roles: string;
  readonly title: string;
  readonly description: string;
  readonly aside?: ReactNode;
}) {
  return (
    <div className="scenario-header">
      <div className="min-w-0">
        <p className="ds-eyebrow">
          {codes && <span className="scenario-code">{codes}</span>}
          {roles}
        </p>
        <h2 id={id}>{title}</h2>
        <p className="scenario-description">{description}</p>
      </div>
      {aside}
    </div>
  );
}

type StepState = "done" | "current" | "pending";

function Step({
  index,
  state,
  children,
}: {
  readonly index: number;
  readonly state: StepState;
  readonly children: string;
}) {
  const t = useDemoStrings();
  const label = { done: t.stepDone, current: t.stepCurrent, pending: t.stepPending }[state];
  return (
    <li data-state={state}>
      <span className="step-marker" aria-hidden="true">
        {state === "done" ? "✓" : index}
      </span>
      <span className="min-w-0 flex-1">{children}</span>
      <span className="step-state">{label}</span>
    </li>
  );
}

function Workbench() {
  const t = useDemoStrings();
  const language = usePreferences((s) => s.language);
  const sound = usePreferences((s) => s.sound);
  const [search, setSearch] = useState("");
  const [priority, setPriority] = useState("");
  const deferredSearch = useDeferredValue(search);
  const [selected, setSelected] = useState(firstRecord);
  const [accepted, setAccepted] = useState(false);
  const [command, setCommand] = useState(false);
  const [transfer, setTransfer] = useState(false);
  const [imported, setImported] = useState(false);
  const [zoneName, setZoneName] = useState("");
  const [zone, setZone] = useState("");
  const [reason, setReason] = useState("");
  const [notice, setNotice] = useState(false);
  const [soundFailed, setSoundFailed] = useState(false);
  const failed = useRef(false);
  const query = useQuery({
    queryKey: ["design-preview"],
    queryFn: async () => {
      await new Promise((resolve) => setTimeout(resolve, 500));
      if (failed.current) throw new Error("unavailable");
      return records;
    },
    retry: false,
    staleTime: Infinity,
    refetchOnWindowFocus: false,
  });
  const rows = (query.data ?? []).filter(
    (row) =>
      `${row.id} ${t[row.title]} ${t[row.object]}`
        .toLocaleLowerCase(language)
        .includes(deferredSearch.trim().toLocaleLowerCase(language)) &&
      (!priority || row.code === priority),
  );
  // A background refresh keeps the data current; only stale data blocks operational actions.
  const stale = query.isError;
  const number = new Intl.NumberFormat(language, { maximumFractionDigits: 1 });
  const duration = (minutes: number) =>
    minutes < 60
      ? `${number.format(minutes)} ${t.minutes}`
      : `${number.format(Math.floor(minutes / 60))} ${t.hours} ${number.format(minutes % 60)} ${t.minutes}`;
  function refresh(failure: boolean) {
    failed.current = failure;
    void query.refetch();
  }
  function select(row: RecordRow) {
    setSelected(row);
    setAccepted(false);
    setCommand(false);
    setReason("");
  }
  async function alert() {
    setNotice(true);
    if (sound) {
      try {
        await playAlert();
        setSoundFailed(false);
      } catch {
        setSoundFailed(true);
      }
    }
  }
  const metrics = [
    { label: t.total, value: number.format(128), change: t.changeTotal, tone: "neutral" },
    { label: t.mtta, value: `${number.format(42)} ${t.seconds}`, change: t.changeMtta, tone: "success" },
    { label: t.mttr, value: `${number.format(8)} ${t.minutes}`, change: t.changeMttr, tone: "warning" },
    { label: t.falseAlarms, value: `${number.format(12)}%`, change: t.changeFalse, tone: "success" },
    { label: t.slaRate, value: `${number.format(98.4)}%`, change: t.changeSla, tone: "success" },
  ] as const;
  const breakdown = [
    { code: "P1", total: 9, mtta: 18, mttr: 14, sla: 100 },
    { code: "P2", total: 27, mtta: 35, mttr: 11, sla: 96.3 },
    { code: "P3", total: 61, mtta: 47, mttr: 7, sla: 98.4 },
    { code: "P4", total: 31, mtta: 52, mttr: 5, sla: 100 },
  ] as const;
  function csv() {
    const value = [
      [t.priority, t.total, t.mtta, t.mttr, t.slaRate],
      ...breakdown.map((row) => [
        row.code,
        number.format(row.total),
        `${number.format(row.mtta)} ${t.seconds}`,
        `${number.format(row.mttr)} ${t.minutes}`,
        `${number.format(row.sla)}%`,
      ]),
    ];
    const content =
      "﻿" + value.map((row) => row.map((cell) => `"${cell.replaceAll('"', '""')}"`).join(",")).join("\r\n");
    const url = URL.createObjectURL(new Blob([content], { type: "text/csv;charset=utf-8" }));
    const link = document.createElement("a");
    link.href = url;
    link.download = "psim-report-sample.csv";
    link.click();
    // Revoke after the click task so that every browser has started the download.
    setTimeout(() => {
      URL.revokeObjectURL(url);
    });
  }
  const steps: readonly [StepState, StepState, StepState] = command
    ? ["done", "done", "current"]
    : accepted
      ? ["done", "current", "pending"]
      : ["current", "pending", "pending"];
  return (
    <main tabIndex={-1} id="main-content" className="workbench">
      <header className="page-heading">
        <div className="min-w-0">
          <p className="ds-eyebrow">{t.prototype}</p>
          <h1>{t.library}</h1>
          <p className="page-lead">{t.libraryLead}</p>
        </div>
        <Badge tone="info">{t.sample}</Badge>
      </header>
      <nav aria-label={t.contents} className="section-nav">
        <a href="#incident-layout">
          <span className="scenario-code">S6-S8</span>
          {t.response}
        </a>
        <a href="#catalog-layout">
          <span className="scenario-code">S2</span>
          {t.catalog}
        </a>
        <a href="#sla-layout">
          <span className="scenario-code">S9</span>
          {t.escalation}
        </a>
        <a href="#report-layout">
          <span className="scenario-code">S13</span>
          {t.reports}
        </a>
        <a href="#notice-layout">{t.notices}</a>
        <a href="#foundations">{t.foundations}</a>
      </nav>

      <section id="incident-layout" aria-labelledby="incident-heading" className="scenario">
        <ScenarioHeader
          id="incident-heading"
          codes="S6 · S7 · S8"
          roles={`${t.operator} · ${t.supervisor}`}
          title={t.response}
          description={t.responseHint}
          aside={
            <div className="demo-controls" role="group" aria-label={t.demoControls}>
              <span className="demo-controls-label">{t.demoControls}</span>
              <Button
                size="sm"
                variant="secondary"
                disabled={query.isFetching}
                onClick={() => {
                  refresh(!query.isError);
                }}
              >
                {query.isError ? t.recover : t.simulateError}
              </Button>
            </div>
          }
        />
        <div className="workspace-grid">
          <div className={cardVariants({ className: "feed-panel" })}>
            <div className="panel-header">
              <h3>{t.feed}</h3>
              <Badge tone={query.isError ? "warning" : "success"}>{query.isError ? t.offline : t.online}</Badge>
            </div>
            <div className="filter-bar">
              <Field
                label={t.search}
                type="search"
                placeholder={t.searchPlaceholder}
                value={search}
                onChange={(event) => {
                  setSearch(event.target.value);
                }}
              />
              <SelectField
                label={t.priority}
                value={priority}
                onChange={(event) => {
                  setPriority(event.target.value);
                }}
              >
                <option value="">{t.all}</option>
                {(Object.keys(priorities) as Code[]).map((code) => (
                  <option key={code} value={code}>
                    {t[priorities[code].label]}
                  </option>
                ))}
              </SelectField>
              <Button
                variant="secondary"
                disabled={query.isFetching}
                onClick={() => {
                  refresh(false);
                }}
              >
                {t.refresh}
              </Button>
            </div>
            <DataRegion
              loading={query.isFetching}
              hasData={query.data !== undefined}
              error={query.isError ? t.loadError : undefined}
              labels={t}
            >
              <TableContainer label={t.activeIncidents} data-testid="preview-table">
                <table className="ds-table">
                  <caption>
                    {t.activeIncidents}
                    <span className="caption-count">{number.format(rows.length)}</span>
                  </caption>
                  <thead>
                    <tr>
                      <th scope="col">{t.priority}</th>
                      <th scope="col">{t.incident}</th>
                      <th scope="col">{t.object}</th>
                      <th scope="col">{t.status}</th>
                      <th scope="col" data-align="end">
                        {t.age}
                      </th>
                    </tr>
                  </thead>
                  <tbody>
                    {rows.map((row) => (
                      <tr key={row.id} data-selected={selected.id === row.id} data-testid={`preview-${row.id}`}>
                        <td>
                          <Priority code={row.code} />
                        </td>
                        <td>
                          <button
                            className="record-link"
                            aria-current={selected.id === row.id ? "true" : undefined}
                            onClick={() => {
                              select(row);
                            }}
                          >
                            {t[row.title]}
                            <span className="record-id">{row.id}</span>
                          </button>
                        </td>
                        <td className="text-muted-foreground">{t[row.object]}</td>
                        <td>
                          <Badge tone={states[row.state] ?? "neutral"}>{t[row.state]}</Badge>
                        </td>
                        <td data-align="end" className="whitespace-nowrap">
                          {duration(row.age)}
                        </td>
                      </tr>
                    ))}
                  </tbody>
                </table>
                {rows.length === 0 && <EmptyState title={t.noMatches} description={t.noMatchesHint} />}
              </TableContainer>
            </DataRegion>
          </div>

          <section className={cardVariants({ className: "incident-detail" })} aria-labelledby="detail-heading">
            <div className="detail-header">
              <div className="min-w-0">
                <p className="record-id">{selected.id}</p>
                <h3 id="detail-heading">{t[selected.title]}</h3>
                <p className="mt-1 text-sm text-muted-foreground">
                  {t.site} / {t.building} / {t[selected.object]}
                </p>
              </div>
              <Priority code={selected.code} />
            </div>
            <div className="detail-status">
              <Badge tone={accepted ? "neutral" : "warning"}>{accepted ? t.acknowledged : t.new}</Badge>
              <span className="sla-chip" data-tone={accepted ? "success" : "warning"}>
                {accepted ? t.slaMet : t.slaDue}
              </span>
            </div>
            <dl className="meta-grid">
              <div>
                <dt>{t.owner}</dt>
                <dd>{t.operator}</dd>
              </div>
              <div>
                <dt>{t.createdAt}</dt>
                <dd>
                  <time dateTime="14:02:11">14:02:11</time>
                </dd>
              </div>
              <div>
                <dt>{t.source}</dt>
                <dd>{t.sensor}</dd>
              </div>
            </dl>
            <Button
              className="w-full"
              disabled={accepted || stale}
              onClick={() => {
                setAccepted(true);
              }}
            >
              {t.acknowledge}
            </Button>

            <h4 className="detail-subheading">{t.plan}</h4>
            <ol className="checklist">
              <Step index={1} state={steps[0]}>
                {t.step1}
              </Step>
              <Step index={2} state={steps[1]}>
                {t.step2}
              </Step>
              <Step index={3} state={steps[2]}>
                {t.step3}
              </Step>
            </ol>

            <div className="critical-block">
              <h4>{t.command}</h4>
              <p className="critical-target">
                {t.door} · {t[selected.object]} · {t.unlock}
              </p>
              <Field
                label={t.reason}
                hint={t.reasonHint}
                required
                value={reason}
                onChange={(event) => {
                  setReason(event.target.value);
                }}
              />
              <ConfirmAction
                trigger={t.command}
                disabled={!reason.trim() || stale}
                title={t.commandTitle}
                description={t.commandHint}
                details={
                  <dl className="confirm-summary">
                    <div>
                      <dt>{t.object}</dt>
                      <dd>
                        {t.door} · {t[selected.object]}
                      </dd>
                    </div>
                    <div>
                      <dt>{t.action}</dt>
                      <dd>{t.unlock}</dd>
                    </div>
                    <div>
                      <dt>{t.reason}</dt>
                      <dd>{reason.trim()}</dd>
                    </div>
                    <div>
                      <dt>{t.approval}</dt>
                      <dd>{t.secondSupervisor}</dd>
                    </div>
                  </dl>
                }
                confirm={t.confirm}
                cancel={t.cancel}
                onConfirm={() => {
                  setCommand(true);
                }}
              />
              {command && (
                <Notice tone="info" title={t.pending}>
                  {t.commandSent}
                </Notice>
              )}
            </div>

            <h4 className="detail-subheading">{t.timeline}</h4>
            <ol className="timeline">
              <li>
                <time dateTime="14:02:11">14:02:11</time>
                <span>{t.created}</span>
              </li>
              <li>
                <time dateTime="14:02:12">14:02:12</time>
                <span>{t.attached}</span>
              </li>
              {accepted && (
                <li>
                  <time dateTime="14:06:23">14:06:23</time>
                  <span>{t.acknowledgedNotice}</span>
                </li>
              )}
              {command && (
                <li data-tone="info">
                  <time dateTime="14:07:40">14:07:40</time>
                  <span>{t.commandSent}</span>
                </li>
              )}
            </ol>
          </section>
        </div>
      </section>

      <section id="catalog-layout" aria-labelledby="catalog-heading" className="scenario">
        <ScenarioHeader
          id="catalog-heading"
          codes="S2"
          roles={t.admin}
          title={t.catalog}
          description={t.catalogHint}
          aside={
            <Button
              variant="secondary"
              onClick={() => {
                setImported(true);
              }}
            >
              {t.importPreview}
            </Button>
          }
        />
        <div className="catalog-grid">
          <Card title={t.structure} headingLevel={3}>
            <ul className="object-tree">
              <li>
                <span data-kind="site">{t.site}</span>
                <ul>
                  <li>
                    <span data-kind="building">{t.building}</span>
                    <ul>
                      <li>
                        <span data-kind="floor" aria-current="true">
                          {t.floor}
                        </span>
                        <ul>
                          <li>{t.entrance}</li>
                          <li>{t.corridor}</li>
                          <li>{t.serverRoom}</li>
                          <li>{t.storage}</li>
                          {zone && <li data-new="true">{zone}</li>}
                        </ul>
                      </li>
                    </ul>
                  </li>
                </ul>
              </li>
            </ul>
            <form
              className="zone-form"
              onSubmit={(event) => {
                event.preventDefault();
                setZone(zoneName.trim());
              }}
            >
              <Field
                label={t.zoneName}
                value={zoneName}
                onChange={(event) => {
                  setZoneName(event.target.value);
                }}
              />
              <Button type="submit" variant="secondary" disabled={!zoneName.trim()}>
                {t.createObject}
              </Button>
            </form>
            {zone && (
              <Notice tone="success">
                {t.zoneCreated}: {zone}
              </Notice>
            )}
          </Card>
          <Card title={`${t.planTitle}: ${t.floor}`} headingLevel={3}>
            <div
              className="floor-plan"
              role="img"
              aria-label={`${t.floor}. ${t.serverRoom}: ${t.sensor}, ${t.lost}. ${t.entrance}: ${t.door}, ${t.maintenance}. ${t.corridor}: ${t.camera}, ${t.healthy}.`}
            >
              <div className="plan-room" data-area="server">
                <span className="plan-room-name">{t.serverRoom}</span>
                <span className="plan-device" data-tone="danger">
                  {t.sensor}
                </span>
              </div>
              <div className="plan-room" data-area="storage">
                <span className="plan-room-name">{t.storage}</span>
              </div>
              <div className="plan-room" data-area="corridor">
                <span className="plan-room-name">{t.corridor}</span>
                <span className="plan-device" data-tone="neutral">
                  {t.camera}
                </span>
              </div>
              <div className="plan-room" data-area="entrance">
                <span className="plan-room-name">{t.entrance}</span>
                <span className="plan-device" data-tone="warning">
                  {t.door}
                </span>
              </div>
              {zone && (
                <div className="plan-room" data-area="new">
                  <span className="plan-room-name">{zone}</span>
                </div>
              )}
            </div>
            <ul className="plan-legend" aria-label={t.legend}>
              <li data-tone="danger">{t.lost}</li>
              <li data-tone="warning">{t.maintenance}</li>
              <li data-tone="neutral">{t.healthy}</li>
            </ul>
          </Card>
        </div>
        {imported && (
          <Card title={t.import} headingLevel={3} className="mt-4">
            <Notice
              tone="warning"
              title={message(language, "importSummary", { total: 50000, accepted: 49998, rejected: 2 })}
            >
              {t.importNext}
            </Notice>
            <TableContainer label={t.rejectedRows} className="mt-4">
              <table className="ds-table">
                <caption>{t.rejectedRows}</caption>
                <thead>
                  <tr>
                    <th scope="col" data-align="end">
                      {t.row}
                    </th>
                    <th scope="col">{t.deviceId}</th>
                    <th scope="col">{t.zoneColumn}</th>
                    <th scope="col">{t.problem}</th>
                  </tr>
                </thead>
                <tbody>
                  {[
                    ["18", "DOOR-0117", "B-2-14"],
                    ["203", "CAM-0942", "B-9-01"],
                  ].map(([line = "", device, area]) => (
                    <tr key={line}>
                      <td data-align="end">{number.format(Number(line))}</td>
                      <td>
                        <code>{device}</code>
                      </td>
                      <td>
                        <code>{area}</code>
                      </td>
                      <td className="text-danger">{t.unknownZone}</td>
                    </tr>
                  ))}
                </tbody>
              </table>
            </TableContainer>
          </Card>
        )}
      </section>

      <section id="sla-layout" aria-labelledby="sla-heading" className="scenario">
        <ScenarioHeader
          id="sla-heading"
          codes="S9"
          roles={t.supervisor}
          title={t.escalation}
          description={t.escalationHint}
        />
        <div className="two-columns">
          <Card title={t.slaState} headingLevel={3}>
            <Notice tone="danger" live={false} title={`${t.sla} · INC-1042 · P1`}>
              {t.slaOverdue}
            </Notice>
            <div className="sla-progress" aria-hidden="true">
              <span />
            </div>
            <dl className="meta-grid">
              <div>
                <dt>{t.slaTarget}</dt>
                <dd>5 {t.minutes}</dd>
              </div>
              <div>
                <dt>{t.elapsed}</dt>
                <dd>
                  8 {t.minutes} 12 {t.seconds}
                </dd>
              </div>
              <div>
                <dt>{t.escalationLevel}</dt>
                <dd>{t.levelOf}</dd>
              </div>
            </dl>
          </Card>
          <Card title={t.escalationChain} headingLevel={3}>
            <ol className="escalation-chain">
              <li data-state="skipped">
                <span>{t.dutyOperator}</span>
                <Badge>{t.unavailable}</Badge>
              </li>
              <li data-state="current">
                <span>
                  {t.backup}: {t.supervisor}
                </span>
                <Badge tone="warning">{t.notified}</Badge>
              </li>
              <li data-state="pending">
                <span>{t.manager}</span>
                <Badge>{t.nextLevel}</Badge>
              </li>
            </ol>
            <h4 className="detail-subheading">{t.responsibility}</h4>
            <dl className="meta-grid">
              <div>
                <dt>{t.currentOwner}</dt>
                <dd>{t.operator}</dd>
              </div>
              <div>
                <dt>{t.recipient}</dt>
                <dd>{t.supervisor}</dd>
              </div>
            </dl>
            <Button
              variant="secondary"
              disabled={transfer}
              onClick={() => {
                setTransfer(true);
              }}
            >
              {t.transfer}
            </Button>
            {transfer && (
              <div className="mt-3">
                <Notice title={t.pending}>{t.transferPending}</Notice>
              </div>
            )}
          </Card>
        </div>
      </section>

      <section id="report-layout" aria-labelledby="report-heading" className="scenario">
        <ScenarioHeader
          id="report-heading"
          codes="S13"
          roles={t.manager}
          title={t.reports}
          description={t.reportsHint}
          aside={
            <Button variant="secondary" onClick={csv}>
              {t.export}
            </Button>
          }
        />
        <div className={cardVariants()}>
          <dl className="report-scope">
            <div>
              <dt>{t.scope}</dt>
              <dd>{t.site}</dd>
            </div>
            <div>
              <dt>{t.periodLabel}</dt>
              <dd>{t.period}</dd>
            </div>
            <div>
              <dt>{t.generated}</dt>
              <dd>
                <time dateTime="14:10">14:10</time>
              </dd>
            </div>
          </dl>
          <dl className="metric-grid">
            {metrics.map((metric) => (
              <div key={metric.label} className="metric">
                <dt>{metric.label}</dt>
                <dd className="metric-value">{metric.value}</dd>
                <dd className="metric-change" data-tone={metric.tone}>
                  {metric.change}
                </dd>
              </div>
            ))}
          </dl>
          <TableContainer label={t.byPriority} className="mt-5">
            <table className="ds-table">
              <caption>{t.byPriority}</caption>
              <thead>
                <tr>
                  <th scope="col">{t.priority}</th>
                  <th scope="col" data-align="end">
                    {t.total}
                  </th>
                  <th scope="col" data-align="end">
                    {t.mtta}
                  </th>
                  <th scope="col" data-align="end">
                    {t.mttr}
                  </th>
                  <th scope="col" data-align="end">
                    {t.slaRate}
                  </th>
                </tr>
              </thead>
              <tbody>
                {breakdown.map((row) => (
                  <tr key={row.code}>
                    <td>
                      <Priority code={row.code} />
                    </td>
                    <td data-align="end">{number.format(row.total)}</td>
                    <td data-align="end">
                      {number.format(row.mtta)} {t.seconds}
                    </td>
                    <td data-align="end">
                      {number.format(row.mttr)} {t.minutes}
                    </td>
                    <td data-align="end">{number.format(row.sla)}%</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </TableContainer>
        </div>
      </section>

      <section id="notice-layout" aria-labelledby="notice-heading" className="scenario">
        <ScenarioHeader id="notice-heading" roles={t.operator} title={t.notices} description={t.soundHint} />
        <div className="two-columns">
          <Card title={t.alertCheck} headingLevel={3}>
            <Switch
              label={t.sound}
              checked={sound}
              onCheckedChange={(value) => {
                setPreferences({ sound: value });
              }}
            />
            <div className="my-4 flex flex-wrap gap-2">
              <Button
                variant="secondary"
                onClick={() => {
                  void alert();
                }}
              >
                {t.preview}
              </Button>
              <Button
                variant="ghost"
                disabled={!sound}
                onClick={() => {
                  void playAlert().catch(() => {
                    setSoundFailed(true);
                  });
                }}
              >
                {t.soundTest}
              </Button>
            </div>
            <div className="grid gap-3">
              {notice && (
                <Notice
                  tone="danger"
                  title={t.alertTitle}
                  action={
                    <Button size="sm" asChild>
                      <a
                        href="#incident-layout"
                        onClick={() => {
                          select(firstRecord);
                        }}
                      >
                        {t.openIncident}
                      </a>
                    </Button>
                  }
                >
                  {t.alertPreview}
                </Notice>
              )}
              {soundFailed && <Notice tone="warning">{t.soundFailed}</Notice>}
            </div>
          </Card>
          <Card title={t.alertRules} headingLevel={3}>
            <TableContainer label={t.alertRulesTable}>
              <table className="ds-table">
                <caption className="sr-only">{t.alertRules}</caption>
                <thead>
                  <tr>
                    <th scope="col">{t.priority}</th>
                    <th scope="col">{t.visual}</th>
                    <th scope="col">{t.sound}</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td>
                      <Priority code="P1" />
                    </td>
                    <td>{t.ruleP1}</td>
                    <td>{t.soundOnce}</td>
                  </tr>
                  <tr>
                    <td>
                      <Priority code="P2" />
                    </td>
                    <td>{t.ruleP2}</td>
                    <td>{t.soundOnce}</td>
                  </tr>
                  <tr>
                    <td>
                      <Priority code="P3" />
                    </td>
                    <td>{t.ruleP3}</td>
                    <td>{t.soundNone}</td>
                  </tr>
                </tbody>
              </table>
            </TableContainer>
            <p className="mt-3 text-sm text-muted-foreground">{t.alertLimits}</p>
          </Card>
        </div>
      </section>

      <OperationalDemo />
      <section id="foundations" aria-labelledby="foundations-heading" className="scenario">
        <ScenarioHeader
          id="foundations-heading"
          roles={t.designers}
          title={t.foundations}
          description={t.accessibility}
        />
        <div className="two-columns">
          <Card title={t.tokens} headingLevel={3}>
            <ul className="swatch-grid">
              {tokens.map((token) => (
                <li key={token}>
                  <span className="swatch" style={{ background: `var(--${token})` }} aria-hidden="true" />
                  <code>{token}</code>
                </li>
              ))}
            </ul>
            <h4 className="detail-subheading">{t.statusLabels}</h4>
            <div className="flex flex-wrap gap-2">
              {(Object.keys(priorities) as Code[]).map((code) => (
                <Priority key={code} code={code} />
              ))}
              <Badge>{t.healthy}</Badge>
              <Badge tone="warning">{t.maintenance}</Badge>
              <Badge>{t.unknown}</Badge>
            </div>
          </Card>
          <Card title={t.componentLibrary} headingLevel={3}>
            <div className="flex flex-wrap gap-2">
              <Button>{t.primaryAction}</Button>
              <Button variant="secondary">{t.secondaryAction}</Button>
              <Button variant="danger">{t.dangerAction}</Button>
              <Button variant="ghost">{t.ghostAction}</Button>
              <Button disabled>{t.disabledAction}</Button>
            </div>
            <div className="component-fields">
              <Field label={t.badgeNumber} hint={t.badgeHint} />
              <Field label={t.employeeNumber} error={t.employeeError} defaultValue="77-" />
            </div>
            <div className="grid gap-2">
              <Notice tone="success" live={false}>
                {t.savedNotice}
              </Notice>
              <Notice tone="warning" live={false}>
                {t.stale}
              </Notice>
            </div>
            <EmptyState title={t.forbidden} description={t.forbiddenHint} headingLevel={4} />
          </Card>
        </div>
      </section>
    </main>
  );
}
export function DesignSystemPage() {
  const [client] = useState(() => new QueryClient());
  return (
    <QueryClientProvider client={client}>
      <Workbench />
    </QueryClientProvider>
  );
}
