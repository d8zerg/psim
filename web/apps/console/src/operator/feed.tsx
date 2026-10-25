import { memo, useLayoutEffect, useMemo, useRef, useState, type KeyboardEvent } from "react";
import { Badge, Button, cardVariants } from "@psim/design-system";
import type { FeedRow } from "../feed/controller";
import { useStrings } from "../ui/strings";
const ROW_HEIGHT = 112;
const IncidentRow = memo(function IncidentRow({
  row,
  index,
  total,
  selected,
  choose,
}: {
  readonly row: FeedRow;
  readonly index: number;
  readonly total: number;
  readonly selected: boolean;
  readonly choose: (id: string) => void;
}) {
  const t = useStrings();
  const element = useRef<HTMLElement>(null);
  useLayoutEffect(() => {
    if (element.current) element.current.dataset.renderedAt = String(performance.timeOrigin + performance.now());
  }, [row]);
  const states: Record<string, string> = {
    INCIDENT_STATE_NEW: t.new,
    INCIDENT_STATE_ACKNOWLEDGED: t.acknowledged,
    INCIDENT_STATE_IN_PROGRESS: t.working,
    INCIDENT_STATE_RESOLVED: t.resolved,
    INCIDENT_STATE_CLOSED: t.closed,
  };
  const tone = row.priority === "PRIORITY_P1" ? "danger" : row.priority === "PRIORITY_P2" ? "warning" : "neutral";
  return (
    <article
      ref={element}
      data-testid="incident-row"
      data-incident-id={row.id}
      data-event-id={row.eventId}
      data-version={row.version}
      className={cardVariants({ className: "operator-feed-row" })}
      data-tone={tone}
      style={{ top: index * ROW_HEIGHT, height: ROW_HEIGHT }}
    >
      <Button
        variant="ghost"
        aria-pressed={selected}
        onClick={() => {
          choose(row.id);
        }}
        className="w-full justify-between"
      >
        <span className="sr-only">{t.openIncident}: </span>
        <span className="truncate">{row.title}</span> <Badge tone={tone}>{row.priority.replace("PRIORITY_", "")}</Badge>
      </Button>
      <p className="truncate text-sm text-muted-foreground">
        {states[row.state] ?? t.unknown} · {row.siteId?.length ? row.siteId : t.unknown} · {index + 1}/{total}
      </p>
    </article>
  );
});
export function VirtualFeed({
  rows,
  selected,
  choose,
}: {
  readonly rows: readonly FeedRow[];
  readonly selected: string;
  readonly choose: (id: string) => void;
}) {
  const t = useStrings();
  const scroll = useRef<HTMLDivElement>(null);
  const [top, setTop] = useState(0);
  const [focused, setFocused] = useState("");
  const anchor = useRef<{ id: string; offset: number } | null>(null);
  const start = Math.max(0, Math.floor(top / ROW_HEIGHT) - 3);
  const indices = useMemo(() => {
    const visible = Array.from({ length: Math.max(0, Math.min(rows.length - start, 12)) }, (_, i) => i + start);
    const focusIndex = rows.findIndex((row) => row.id === focused);
    if (focusIndex >= 0 && !visible.includes(focusIndex)) visible.push(focusIndex);
    return visible.sort((a, b) => a - b);
  }, [rows, start, focused]);
  useLayoutEffect(() => {
    if (scroll.current?.scrollTop === 0) {
      anchor.current = null;
      return;
    }
    const previous = anchor.current;
    const index = previous ? rows.findIndex((row) => row.id === previous.id) : -1;
    if (scroll.current && index >= 0 && previous) scroll.current.scrollTop = index * ROW_HEIGHT + previous.offset;
  }, [rows]);
  function remember() {
    const position = scroll.current?.scrollTop ?? 0;
    const row = rows[Math.floor(position / ROW_HEIGHT)];
    anchor.current = position > 0 && row ? { id: row.id, offset: position % ROW_HEIGHT } : null;
    setTop(position);
  }
  function keyboard(event: KeyboardEvent) {
    if (!["ArrowDown", "ArrowUp", "Home", "End"].includes(event.key)) return;
    event.preventDefault();
    const current = rows.findIndex((row) => row.id === selected);
    const next =
      event.key === "Home"
        ? 0
        : event.key === "End"
          ? rows.length - 1
          : Math.max(0, Math.min(rows.length - 1, current + (event.key === "ArrowDown" ? 1 : -1)));
    const row = rows[next];
    if (row) {
      choose(row.id);
      scroll.current?.scrollTo({ top: next * ROW_HEIGHT });
    }
  }
  return (
    <section aria-label={t.incidents}>
      <div
        ref={scroll}
        className="operator-feed-scroll"
        role="toolbar"
        aria-label={t.feed}
        onScroll={remember}
        onKeyDown={keyboard}
        onFocusCapture={(event) => {
          setFocused(event.target.closest<HTMLElement>("[data-incident-id]")?.dataset.incidentId ?? "");
        }}
        onBlurCapture={(event) => {
          if (!event.currentTarget.contains(event.relatedTarget)) setFocused("");
        }}
      >
        <div className="operator-feed-space" style={{ height: rows.length * ROW_HEIGHT }}>
          {indices.map((index) => {
            const row = rows[index];
            return row ? (
              <IncidentRow
                key={row.id}
                row={row}
                index={index}
                total={rows.length}
                selected={selected === row.id}
                choose={choose}
              />
            ) : null;
          })}
        </div>
      </div>
    </section>
  );
}
