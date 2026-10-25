import { Select as SelectField } from "./fields";
import { useInfiniteQuery, useQuery } from "@tanstack/react-query";
import { useEffect, useState } from "react";
import { Badge, Button, Card, Field, Notice } from "@psim/design-system";
import type { SessionProfile } from "../auth";
import { useStrings } from "../ui/strings";
import { statusOf, type Incident, type OperatorApi } from "./api";
import { queryDefaults, type Actions } from "./queries";
import { RunPanel } from "./response";
import { CommandsPanel } from "./commands";
export function IncidentPanel({
  id,
  api,
  live,
  writable,
  profile,
  actions,
}: {
  readonly id: string;
  readonly api: OperatorApi;
  readonly live: boolean;
  readonly writable: boolean;
  readonly profile: SessionProfile;
  readonly actions: Actions;
}) {
  const t = useStrings();
  const [reason, setReason] = useState("");
  const [comment, setComment] = useState("");
  const [recipient, setRecipient] = useState("");
  const [resolution, setResolution] = useState<Incident["resolution"]>("RESOLUTION_CONFIRMED");
  const [until, setUntil] = useState("");
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const timer = window.setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => {
      window.clearInterval(timer);
    };
  }, []);
  const [reviewed, setReviewed] = useState("");
  const operations = useQuery({
    ...queryDefaults,
    queryKey: ["operations", id],
    queryFn: ({ signal }) => api.operations(id, signal),
    enabled: live,
  });
  const run = useQuery({
    ...queryDefaults,
    queryKey: ["run", id],
    queryFn: ({ signal }) => api.run(id, signal),
    enabled: live,
  });
  const timeline = useInfiniteQuery({
    ...queryDefaults,
    queryKey: ["timeline", id],
    queryFn: ({ signal, pageParam }) => api.timeline(id, signal, pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  const signals = useInfiniteQuery({
    ...queryDefaults,
    queryKey: ["signals", id],
    queryFn: ({ signal, pageParam }) => api.signals(id, signal, pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  const incident = operations.data?.incident;
  const forbidden = [operations.error, run.error, timeline.error, signals.error].some((error) =>
    [401, 403].includes(statusOf(error)),
  );
  const ready = writable && Boolean(incident?.version) && !operations.isError && !forbidden;
  const supervisor = profile.roles.includes("supervisor");
  const act = (action: "acknowledge" | "start-work") => {
    if (incident && ready) void actions.perform(() => api.incidentAction(incident, action));
  };
  useEffect(() => {
    const shortcut = (event: KeyboardEvent) => {
      if (
        !ready ||
        !event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        (event.target instanceof HTMLElement &&
          (event.target.matches("input,textarea,select") || event.target.isContentEditable))
      )
        return;
      if (event.code === "KeyA" && incident?.state === "INCIDENT_STATE_NEW") {
        event.preventDefault();
        void actions.perform(() => api.incidentAction(incident, "acknowledge"));
      }
      if (event.code === "KeyW" && incident?.state === "INCIDENT_STATE_ACKNOWLEDGED") {
        event.preventDefault();
        void actions.perform(() => api.incidentAction(incident, "start-work"));
      }
    };
    window.addEventListener("keydown", shortcut);
    return () => {
      window.removeEventListener("keydown", shortcut);
    };
  }, [ready, incident, api, actions]);
  if (forbidden) return <Notice tone="warning">{t.forbidden}</Notice>;
  if (!incident) return <Notice tone="warning">{operations.isPending && live ? t.loading : t.detailsError}</Notice>;
  const alarm = incident.operational;
  const version = `${incident.version ?? ""}:${run.data?.run?.version ?? ""}`;
  const transfer = incident.pendingTransfer;
  const validTransfer = transfer?.requestedVersion === incident.version;
  const riskLabels: Record<string, string> = {
    unknown: t.unknown,
    "on-time": t.onTime,
    "at-risk": t.atRisk,
    breached: t.breached,
    completed: t.completed,
  };
  const slaRisk = operations.data?.slaRisk ?? "unknown";
  const liveWarning = !live || operations.isError;
  return (
    <>
      <Card title={incident.title ?? t.incident}>
        <p>
          <code>{id}</code> · {t.version}: {incident.version} · {t.owner}:{" "}
          {incident.assigneeId?.length ? incident.assigneeId : t.unknown}
        </p>
        {liveWarning && <Notice tone="warning">{t.stale}</Notice>}
        <div className="flex flex-wrap gap-2">
          <Button
            disabled={!ready || incident.state !== "INCIDENT_STATE_NEW"}
            onClick={() => {
              act("acknowledge");
            }}
          >
            {t.acknowledge}
          </Button>
          <Button
            disabled={!ready || incident.state !== "INCIDENT_STATE_ACKNOWLEDGED"}
            onClick={() => {
              act("start-work");
            }}
          >
            {t.startWork}
          </Button>
        </div>
        <h3>{t.alarmState}</h3>
        <p>
          {alarm?.conditionKnown ? (alarm.active ? t.activeAlarm : t.returnedAlarm) : t.unknown} ·{" "}
          {alarm?.acknowledged ? t.alarmAcknowledged : t.alarmUnacknowledged}
        </p>
        <Button
          variant="secondary"
          disabled={!ready || !alarm?.conditionKnown || alarm.acknowledged}
          onClick={() => {
            void actions.perform(() => api.alarm(incident, "acknowledge", ""));
          }}
        >
          {t.acknowledgeAlarm}
        </Button>
        {[alarm?.shelved, alarm?.suppressed].map((control, index) =>
          control ? (
            <p key={index}>
              {index === 0 ? t.shelve : t.suppress}: {control.reason} · {control.actorId} · {control.until}
            </p>
          ) : null,
        )}
        <Field
          label={t.reason}
          value={reason}
          maxLength={8000}
          onChange={(event) => {
            setReason(event.target.value);
          }}
        />
        <Field
          label={t.until}
          type="datetime-local"
          value={until}
          onChange={(event) => {
            setUntil(event.target.value);
          }}
        />
        <div className="flex flex-wrap gap-2">
          {["shelve", "unshelve", ...(supervisor ? ["suppress", "unsuppress"] : [])].map((action) => (
            <Button
              key={action}
              variant="secondary"
              disabled={
                !ready ||
                !reason.trim() ||
                ((action === "shelve" || action === "suppress") &&
                  (!until ||
                    !Number.isFinite(Date.parse(until)) ||
                    Date.parse(until) <= now ||
                    Date.parse(until) > now + 86400000))
              }
              onClick={() => {
                void actions.perform(() =>
                  api.alarm(
                    incident,
                    action,
                    reason,
                    action === "shelve" || action === "suppress" ? new Date(until).toISOString() : undefined,
                  ),
                );
              }}
            >
              {action === "shelve"
                ? t.shelve
                : action === "unshelve"
                  ? t.unshelve
                  : action === "suppress"
                    ? t.suppress
                    : t.unsuppress}
            </Button>
          ))}
        </div>
        <h3>{t.escalation}</h3>
        <Badge tone={slaRisk === "breached" ? "danger" : slaRisk === "at-risk" ? "warning" : "neutral"}>
          {riskLabels[slaRisk] ?? t.unknown}
        </Badge>
        <dl>
          <dt>{t.slaTarget}</dt>
          <dd>{alarm?.acknowledgeDue ?? t.unknown}</dd>
          <dt>{t.resolveTarget}</dt>
          <dd>{alarm?.resolveDue ?? t.unknown}</dd>
          <dt>{t.escalationLevel}</dt>
          <dd data-testid="escalation-level">{incident.escalationLevel ?? 0}</dd>
        </dl>
        <h3>{t.presence}</h3>
        <ul>
          {alarm?.presence
            ?.filter((lease) => Date.parse(lease.until ?? "") > now)
            .map((lease) => (
              <li key={lease.leaseId}>
                {lease.actorId}: {lease.editing ? t.editing : t.viewing} · {lease.until}
              </li>
            ))}
        </ul>
        <Field
          label={t.recipient}
          value={recipient}
          onChange={(event) => {
            setRecipient(event.target.value);
          }}
        />
        {supervisor && (
          <Button
            variant="secondary"
            disabled={!ready || !recipient.trim() || !reason.trim()}
            onClick={() => {
              void actions.perform(() => api.incidentAction(incident, "assign", { assigneeId: recipient, reason }));
            }}
          >
            {t.assign}
          </Button>
        )}
        <h3>{t.responsibility}</h3>
        {transfer && (
          <p>
            {t.currentOwner}: {incident.assigneeId} · {t.recipient}: {transfer.recipientId} · {transfer.comment}
          </p>
        )}
        {transfer && !validTransfer && <Notice tone="warning">{t.transferInvalid}</Notice>}
        <Button
          variant="secondary"
          disabled={!ready || !recipient.trim() || !reason.trim()}
          onClick={() => {
            void actions.perform(() => api.transfer(incident, recipient, reason));
          }}
        >
          {t.transfer}
        </Button>
        {transfer?.recipientId === profile.subject && (
          <>
            <label>
              <input
                type="checkbox"
                checked={reviewed === version}
                disabled={!validTransfer || !run.isSuccess || run.isError}
                onChange={(event) => {
                  setReviewed(event.target.checked ? version : "");
                }}
              />
              {t.reviewTransfer}
            </label>
            <Button
              disabled={!ready || !validTransfer || reviewed !== version || !run.isSuccess || run.isError}
              onClick={() => {
                void actions.perform(() => api.transfer(incident, "", "", transfer.transferId));
              }}
            >
              {t.acceptTransfer}
            </Button>
          </>
        )}
        <SelectField
          label={t.resolution}
          value={resolution ?? "RESOLUTION_CONFIRMED"}
          options={[
            { value: "RESOLUTION_CONFIRMED", label: t.confirmed },
            { value: "RESOLUTION_FALSE_ALARM", label: t.falseAlarm },
            { value: "RESOLUTION_TEST", label: t.testResolution },
            { value: "RESOLUTION_DUPLICATE", label: t.duplicate },
          ]}
          onChange={(event) => {
            const value = event.target.value;
            if (
              value === "RESOLUTION_CONFIRMED" ||
              value === "RESOLUTION_FALSE_ALARM" ||
              value === "RESOLUTION_TEST" ||
              value === "RESOLUTION_DUPLICATE"
            )
              setResolution(value);
          }}
        />
        <div className="flex flex-wrap gap-2">
          <Button
            disabled={
              !ready ||
              !reason.trim() ||
              incident.state !== "INCIDENT_STATE_IN_PROGRESS" ||
              Boolean(incident.blockingSteps?.length) ||
              !run.isSuccess ||
              run.isError ||
              Boolean(run.data.run?.blockingStepIds?.length)
            }
            onClick={() => {
              void actions.perform(() =>
                api.incidentAction(incident, "resolve", {
                  resolution: resolution ?? "RESOLUTION_CONFIRMED",
                  note: reason,
                  knownResponseRunVersion: run.data?.run?.version ?? incident.responseRunVersion ?? "0",
                }),
              );
            }}
          >
            {t.resolveIncident}
          </Button>
          <Button
            variant="secondary"
            disabled={!ready || !reason.trim() || incident.state !== "INCIDENT_STATE_RESOLVED"}
            onClick={() => {
              void actions.perform(() => api.incidentAction(incident, "reopen", { reason }));
            }}
          >
            {t.reopenIncident}
          </Button>
          {supervisor && (
            <Button
              variant="secondary"
              disabled={!ready || incident.state !== "INCIDENT_STATE_RESOLVED"}
              onClick={() => {
                void actions.perform(() => api.incidentAction(incident, "close"));
              }}
            >
              {t.closeIncident}
            </Button>
          )}
        </div>
        <Field
          label={t.comment}
          value={comment}
          maxLength={8000}
          onChange={(event) => {
            setComment(event.target.value);
          }}
        />
        <Button
          variant="secondary"
          disabled={!ready || !comment.trim()}
          onClick={() => {
            void actions
              .perform(() => api.comment(incident, comment))
              .then((ok) => {
                if (ok) setComment("");
              });
          }}
        >
          {t.addComment}
        </Button>
      </Card>
      <RunPanel
        api={api}
        response={run.data}
        writable={ready && !run.isError}
        actions={actions}
        supervisor={supervisor}
      />
      {run.isError && <Notice tone="warning">{t.detailsError}</Notice>}
      <CommandsPanel
        api={api}
        incident={incident}
        run={run.data?.run}
        plan={run.data?.plan}
        live={live}
        writable={ready}
        profile={profile}
        actions={actions}
      />
      <Card title={t.timeline}>
        <ol>
          {timeline.data?.pages
            .flatMap((page) => page.entries ?? [])
            .map((entry) => (
              <li key={entry.entryId}>
                <time>{entry.at}</time> · {entry.actor?.id} · {entry.text} {entry.refId && <code>{entry.refId}</code>}
              </li>
            ))}
        </ol>
        {timeline.isError && <Notice tone="warning">{t.detailsError}</Notice>}
        {timeline.hasNextPage && (
          <Button
            disabled={timeline.isFetchingNextPage}
            onClick={() => {
              void timeline.fetchNextPage();
            }}
          >
            {t.loadMore}
          </Button>
        )}
      </Card>
      <Card title={t.signals}>
        {signals.data?.pages
          .flatMap((page) => page.signals ?? [])
          .map((item) => (
            <SignalEntry key={item.signalId} item={item} api={api} live={live} />
          ))}
        {signals.isError && <Notice tone="warning">{t.detailsError}</Notice>}
        {signals.hasNextPage && (
          <Button
            disabled={signals.isFetchingNextPage}
            onClick={() => {
              void signals.fetchNextPage();
            }}
          >
            {t.loadMore}
          </Button>
        )}
      </Card>
    </>
  );
}
function SignalEntry({
  item,
  api,
  live,
}: {
  readonly item: Awaited<ReturnType<OperatorApi["signals"]>>["signals"] extends (infer T)[] | undefined ? T : never;
  readonly api: OperatorApi;
  readonly live: boolean;
}) {
  const t = useStrings();
  const [selected, setSelected] = useState("");
  const event = useQuery({
    queryKey: ["event", selected],
    queryFn: ({ signal }) => api.event(selected, signal),
    enabled: live && Boolean(selected),
    retry: false,
  });
  return (
    <details>
      <summary>
        {item.signalId} · {item.signal?.origin?.ruleId} · {item.attachedAt}
      </summary>
      <ul>
        {item.signal?.eventIds?.map((id) => (
          <li key={id}>
            <Button
              variant="ghost"
              onClick={() => {
                setSelected(id);
              }}
            >
              {t.events}: {id}
            </Button>
          </li>
        ))}
      </ul>
      {selected && (
        <pre className="whitespace-pre-wrap break-all">
          {event.isError ? t.detailsError : event.data ? JSON.stringify(event.data, null, 2) : t.loading}
        </pre>
      )}
    </details>
  );
}
