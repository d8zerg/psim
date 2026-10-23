import { memo, useEffect, useState, type ComponentProps, type ReactNode } from "react";
import { Button, type NativeButtonProps } from "./button";
import { Badge, Field, Notice } from "./patterns";
import { Popover } from "./overlays";
import { alarmState, type Alarm, type AlarmState, type Command, type CommandPhase } from "../alarm-model";

export type Priority = "P1" | "P2" | "P3" | "P4";
/** Priority code and severity text repeat the meaning independently of hue. Routine priorities are neutral. */
export function PriorityBadge({
  priority,
  labels,
}: {
  readonly priority: Priority;
  readonly labels: Record<Priority, string>;
}) {
  return (
    <Badge
      tone={priority === "P1" ? "danger" : priority === "P2" ? "warning" : "neutral"}
      emphasis={priority === "P1" ? "solid" : "subtle"}
    >
      {priority} - {labels[priority]}
    </Badge>
  );
}
export type AlarmLabels = Record<AlarmState | "visible" | "shelved" | "suppressed", string>;
export function AlarmStateBadge({ alarm, labels }: { readonly alarm: Alarm; readonly labels: AlarmLabels }) {
  const state = alarmState(alarm);
  return (
    <span data-state={state} className="inline-flex flex-wrap gap-2">
      <Badge
        tone={
          state === "active-unacknowledged" ? "danger" : state === "returned-unacknowledged" ? "warning" : "neutral"
        }
      >
        {labels[state]}
      </Badge>
      {alarm.visibility.kind !== "visible" && (
        <Badge>
          {labels[alarm.visibility.kind]}: {alarm.visibility.reason} ({alarm.visibility.actor})
        </Badge>
      )}
    </span>
  );
}
export function ConnectionIndicator({
  state,
  labels,
}: {
  readonly state: "connected" | "delayed" | "disconnected";
  readonly labels: Record<"connected" | "delayed" | "disconnected", string>;
}) {
  return (
    <Badge tone={state === "connected" ? "neutral" : "warning"}>
      {state === "connected" ? "=" : "!"} {labels[state]}
    </Badge>
  );
}
function useClock(live: boolean, fixed: number) {
  const [now, setNow] = useState(fixed);
  useEffect(() => {
    if (!live) return;
    const timer = window.setInterval(() => {
      setNow(Date.now());
    }, 1000);
    return () => {
      window.clearInterval(timer);
    };
  }, [live]);
  return live ? now : fixed;
}
/** Own clock state updates only this leaf, keeping the incident feed out of the ticking render path. */
export function SlaTimer({
  deadline,
  now,
  live = false,
  label,
  expiredLabel,
  locale = "en",
}: {
  readonly deadline: number;
  readonly now: number;
  readonly live?: boolean;
  readonly label: string;
  readonly expiredLabel: string;
  readonly locale?: string;
}) {
  const current = useClock(live, now);
  const seconds = Math.ceil((deadline - current) / 1000);
  return (
    <span className="font-mono tabular-nums" data-state={seconds <= 0 ? "expired" : seconds <= 60 ? "risk" : "current"}>
      {seconds <= 0 ? expiredLabel : label}: {new Intl.NumberFormat(locale).format(Math.abs(seconds))} s
    </span>
  );
}
export function AckButton({
  acknowledged,
  label,
  acknowledgedLabel,
  ...props
}: Omit<NativeButtonProps, "children"> & {
  readonly acknowledged: boolean;
  readonly label: string;
  readonly acknowledgedLabel: string;
}) {
  return (
    <Button {...props} disabled={acknowledged || props.disabled} data-state={acknowledged ? "acknowledged" : "pending"}>
      {acknowledged ? acknowledgedLabel : label}
    </Button>
  );
}
export function CommandLifecycle({
  command,
  labels,
  label,
}: {
  readonly command: Command;
  readonly labels: Record<CommandPhase, string>;
  readonly label: string;
}) {
  return (
    <dl aria-label={label} className="grid gap-1">
      <dt>{command.target || label}</dt>
      <dd>
        <Badge
          tone={
            command.phase === "failed" || command.phase === "unknown" || command.phase === "timed-out"
              ? "warning"
              : "neutral"
          }
        >
          {labels[command.phase]}
        </Badge>
      </dd>
      {command.reason && <dd>{command.reason}</dd>}
      {command.reviewer && (
        <dd>
          {command.actor} - {command.reviewer}
        </dd>
      )}
    </dl>
  );
}
/** The caller owns validation and must pass the resulting reason to the command guard. */
export function AuditReasonField(props: ComponentProps<typeof Field>) {
  return <Field {...props} required />;
}
export function DeviceMarker({
  label,
  state,
  stateLabel,
  selected,
  onSelect,
}: {
  readonly label: string;
  readonly state: "normal" | "alarm" | "offline";
  readonly stateLabel: string;
  readonly selected: boolean;
  readonly onSelect: () => void;
}) {
  return (
    <Button variant="secondary" aria-pressed={selected} onClick={onSelect}>
      <Badge tone={state === "alarm" ? "danger" : state === "offline" ? "warning" : "neutral"}>
        {state === "normal" ? "=" : "!"} {label}: {stateLabel}
      </Badge>
    </Button>
  );
}
export function AlarmBanner({
  label,
  count,
  priority,
  action,
  countLabel,
}: {
  readonly label: string;
  readonly count: number;
  readonly priority: Priority;
  readonly action?: ReactNode;
  readonly countLabel: string;
}) {
  return (
    <Notice
      live={false}
      tone={count > 0 && priority === "P1" ? "danger" : count > 0 ? "warning" : "neutral"}
      title={`${priority}: ${label}`}
      action={action}
    >
      {countLabel}: {count}
    </Notice>
  );
}
export function CameraTile({
  label,
  state,
  stateLabel,
  image,
  onOpen,
  openLabel,
}: {
  readonly label: string;
  readonly state: "available" | "offline" | "restricted";
  readonly stateLabel: string;
  readonly image?: string;
  readonly onOpen: () => void;
  readonly openLabel: string;
}) {
  return (
    <section aria-label={label} className="grid gap-2 rounded border border-border p-3">
      <h4>{label}</h4>
      {state === "available" && image ? (
        <img src={image} alt={label} className="aspect-video w-full object-cover" />
      ) : (
        <p className="grid aspect-video place-items-center bg-muted">{stateLabel}</p>
      )}
      <Button variant="secondary" disabled={state !== "available"} onClick={onOpen}>
        {openLabel}
      </Button>
    </section>
  );
}
export interface Evidence {
  readonly id: string;
  readonly label: string;
  readonly value: string;
  readonly sequence: string;
}
/** Source IDs and versions remain inspectable even when the value is stale. */
export function Provenance({
  label,
  value,
  stale,
  staleLabel,
  whyLabel,
  sources,
}: {
  readonly label: string;
  readonly value: ReactNode;
  readonly stale: boolean;
  readonly staleLabel: string;
  readonly whyLabel: string;
  readonly sources: readonly Evidence[];
}) {
  return (
    <div className="grid gap-1">
      <p>
        {label}: {value} {stale && <Badge tone="warning">{staleLabel}</Badge>}
      </p>
      <Popover label={whyLabel} trigger={<Button variant="ghost">{whyLabel}</Button>}>
        <ol>
          {sources.map((source) => (
            <li key={source.id}>
              <code>{source.id}</code> - {source.label}: {source.value} <code>{source.sequence}</code>
            </li>
          ))}
        </ol>
      </Popover>
    </div>
  );
}
const TimeLeaf = memo(function TimeLeaf({
  timestamp,
  now,
  live,
  locale,
  timeZone,
}: {
  readonly timestamp: number;
  readonly now: number;
  readonly live: boolean;
  readonly locale: string;
  readonly timeZone: string;
}) {
  const current = useClock(live, now);
  const seconds = Math.round((timestamp - current) / 1000);
  const unit =
    Math.abs(seconds) < 60
      ? "second"
      : Math.abs(seconds) < 3600
        ? "minute"
        : Math.abs(seconds) < 86400
          ? "hour"
          : "day";
  const relative = Math.round(seconds / (unit === "day" ? 86400 : unit === "hour" ? 3600 : unit === "minute" ? 60 : 1));
  const absolute = new Intl.DateTimeFormat(locale, { timeStyle: "medium", timeZone }).format(timestamp);
  return (
    <time
      dateTime={new Date(timestamp).toISOString()}
      title={new Intl.DateTimeFormat(locale, { dateStyle: "medium", timeStyle: "long", timeZone }).format(timestamp)}
    >
      {absolute} {timeZone} ({new Intl.RelativeTimeFormat(locale, { numeric: "auto" }).format(relative, unit)})
    </time>
  );
});
export function Timestamp({
  timestamp,
  now,
  live = false,
  locale,
  timeZone,
}: {
  readonly timestamp: number;
  readonly now: number;
  readonly live?: boolean;
  readonly locale: string;
  readonly timeZone: string;
}) {
  return <TimeLeaf timestamp={timestamp} now={now} live={live} locale={locale} timeZone={timeZone} />;
}
