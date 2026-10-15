import { Button } from "@psim/design-system";
import { useEffect, useLayoutEffect, useRef, useState, useSyncExternalStore } from "react";
import { auth } from "../auth";
import { createFeed, type FeedRow } from "../feed/controller";

function IncidentRow({ row }: { readonly row: FeedRow }) {
  const element = useRef<HTMLElement>(null);
  useLayoutEffect(() => {
    if (element.current !== null) {
      element.current.dataset.renderedAt = String(performance.timeOrigin + performance.now());
    }
  }, [row]);
  return (
    <article
      ref={element}
      data-testid="incident-row"
      data-incident-id={row.id}
      data-event-id={row.eventId}
      data-version={row.version}
      className="rounded-lg border border-border border-l-4 border-l-amber-500 bg-card p-5"
    >
      <div className="flex items-start justify-between gap-4">
        <h2 className="font-semibold">{row.title}</h2>
        <span className="rounded bg-amber-100 px-2 py-1 text-xs font-semibold text-amber-900">
          {row.priority.replace("PRIORITY_", "")}
        </span>
      </div>
      <p className="mt-2 text-sm text-muted-foreground">
        Main entrance · {row.state.replace("INCIDENT_STATE_", "").toLowerCase()}
      </p>
    </article>
  );
}
export function HomePage() {
  const [feed] = useState(() => createFeed(() => auth.token()));
  const state = useSyncExternalStore(feed.subscribe, feed.getSnapshot);
  const [signedIn, setSignedIn] = useState(false);
  useEffect(() => {
    let cancelled = false;
    void auth
      .token()
      .then(async (token) => {
        if (token !== undefined && !cancelled) {
          setSignedIn(true);
          await feed.start();
        }
      })
      .catch((error: unknown) => {
        feed.fail(error);
      });
    return () => {
      cancelled = true;
      feed.stop();
    };
  }, [feed]);
  return (
    <main className="mx-auto flex max-w-4xl flex-col gap-6 p-8">
      <div>
        <p className="text-sm text-muted-foreground">Operations console</p>
        <h1 className="mt-1 text-2xl font-semibold">Open-Source PSIM Platform</h1>
      </div>
      <div className="flex items-center justify-between gap-4">
        <h2 className="text-lg font-semibold">Incident feed</h2>
        <span className="text-sm text-muted-foreground" data-testid="connection-status">
          {state.status}
        </span>
      </div>
      {state.error !== "" && (
        <p role="alert" className="rounded border border-red-300 p-4 text-red-800">
          {state.error}
        </p>
      )}
      {!signedIn ? (
        <section className="rounded-lg border border-border p-8">
          <h2 className="font-semibold">Sign in to your workspace</h2>
          <p className="my-3 text-sm text-muted-foreground">View incidents and receive updates as events arrive.</p>
          <Button
            onClick={() => {
              void auth.login().catch((error: unknown) => {
                feed.fail(error);
              });
            }}
          >
            Sign in
          </Button>
        </section>
      ) : (
        <section aria-label="Incidents" className="flex flex-col gap-3">
          {state.rows.length === 0 ? (
            <p className="rounded-lg border border-dashed border-border p-8 text-muted-foreground">
              No active incidents. New alarms will appear here.
            </p>
          ) : (
            state.rows.map((row) => <IncidentRow key={row.id} row={row} />)
          )}
        </section>
      )}
    </main>
  );
}
