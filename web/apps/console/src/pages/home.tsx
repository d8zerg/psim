import { cardVariants, Badge, Button, DataRegion, EmptyState, type Tone } from "@psim/design-system";
import { memo, useEffect, useLayoutEffect, useRef, useState, useSyncExternalStore } from "react";
import { auth } from "../auth";
import { createFeed, type FeedRow } from "../feed/controller";
import { useStrings } from "../ui/strings";
const tones: Partial<Record<string, Tone>> = { PRIORITY_P1: "danger", PRIORITY_P2: "warning", PRIORITY_P3: "neutral" };
const IncidentRow = memo(function IncidentRow({ row }: { readonly row: FeedRow }) {
  const t = useStrings();
  const element = useRef<HTMLElement>(null);
  useLayoutEffect(() => {
    if (element.current !== null)
      element.current.dataset.renderedAt = String(performance.timeOrigin + performance.now());
  }, [row]);
  const states: Record<string, string> = {
    INCIDENT_STATE_NEW: t.new,
    INCIDENT_STATE_ACKNOWLEDGED: t.acknowledged,
    INCIDENT_STATE_IN_PROGRESS: t.working,
    INCIDENT_STATE_RESOLVED: t.resolved,
    INCIDENT_STATE_CLOSED: t.closed,
  };
  const tone = tones[row.priority] ?? "neutral";
  return (
    <article
      ref={element}
      data-testid="incident-row"
      data-incident-id={row.id}
      data-event-id={row.eventId}
      data-version={row.version}
      className={cardVariants({ className: "incident-card" })}
      data-tone={tone}
    >
      <div className="flex items-start justify-between gap-4">
        <h3>{row.title}</h3>
        <Badge tone={tone} emphasis={row.priority === "PRIORITY_P1" ? "solid" : "subtle"}>
          {row.priority.replace("PRIORITY_", "")}
        </Badge>
      </div>
      <p className="mt-2 text-sm text-muted-foreground">{states[row.state] ?? t.unknown}</p>
    </article>
  );
});
export function HomePage() {
  const t = useStrings();
  const [feed] = useState(() => createFeed(() => auth.token()));
  const state = useSyncExternalStore(feed.subscribe, feed.getSnapshot);
  const [session, setSession] = useState<"checking" | "anonymous" | "authenticated">("checking");
  useEffect(() => {
    let cancelled = false;
    void auth
      .token()
      .then(async (token) => {
        if (cancelled) return;
        if (token === undefined) {
          setSession("anonymous");
          return;
        }
        setSession("authenticated");
        await feed.start();
      })
      .catch((error: unknown) => {
        if (!cancelled) {
          setSession("anonymous");
          feed.fail(error);
        }
      });
    return () => {
      cancelled = true;
      feed.stop();
    };
  }, [feed]);
  const statuses: Record<string, string> = {
    open: t.online,
    offline: t.offline,
    connecting: t.connecting,
    reconnecting: t.reconnecting,
  };
  return (
    <main tabIndex={-1} id="main-content" className="workbench max-w-5xl">
      <div className="page-heading">
        <div>
          <p className="eyebrow">{t.console}</p>
          <h1>{t.title}</h1>
        </div>
      </div>
      <div className="section-heading">
        <h2>{t.feed}</h2>
        <span data-testid="connection-status" data-status={state.status}>
          <Badge tone={state.status === "open" ? "neutral" : "warning"}>{statuses[state.status] ?? t.offline}</Badge>
        </span>
      </div>
      {session === "checking" ? (
        <p role="status">{t.loading}</p>
      ) : session === "anonymous" ? (
        <section className={cardVariants()}>
          <h2>{t.workspace}</h2>
          <p className="my-3 text-muted-foreground">{t.introduction}</p>
          <Button
            onClick={() => {
              void auth.login().catch((error: unknown) => {
                feed.fail(error);
              });
            }}
          >
            {t.signIn}
          </Button>
          {state.error && (
            <p role="alert" className="mt-4 text-danger">
              {t.signInError}
            </p>
          )}
        </section>
      ) : (
        <DataRegion
          loading={state.loading}
          hasData={state.loaded}
          error={state.error ? t.loadError : undefined}
          labels={t}
        >
          <section aria-label={t.incidents} className="flex flex-col gap-3">
            {state.rows.length === 0 ? (
              <EmptyState title={t.empty} description={t.emptyHint} />
            ) : (
              state.rows.map((row) => <IncidentRow key={row.id} row={row} />)
            )}
          </section>
        </DataRegion>
      )}
    </main>
  );
}
