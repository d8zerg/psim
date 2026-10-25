import { Select as SelectField } from "../operator/fields";
import { Button, DataRegion, EmptyState, Field, Switch, Notice } from "@psim/design-system";
import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import type { FeedState } from "../feed/controller";
import { useStrings } from "../ui/strings";
import { setPreferences, usePreferences } from "../ui/preferences";
import { playAlert } from "../ui/alert-sound";
import { filteredFeed } from "../operator/model";
import { VirtualFeed } from "../operator/feed";
export function HomePage({
  state,
  retry,
  selected = "",
  choose = () => undefined,
  children,
}: {
  readonly state: FeedState;
  readonly retry: () => void;
  readonly selected?: string;
  readonly choose?: (id: string) => void;
  readonly children?: ReactNode;
}) {
  const t = useStrings();
  const [search, setSearch] = useState("");
  const [priority, setPriority] = useState("");
  const [filterState, setFilterState] = useState("");
  const [site, setSite] = useState("");
  const [assignee, setAssignee] = useState("");
  const sound = usePreferences((s) => s.sound);
  const mode = usePreferences((s) => s.mode);
  const [volume, setVolume] = useState("30");
  const [soundFailed, setSoundFailed] = useState(false);
  const rows = useMemo(
    () => filteredFeed(state.rows, search, priority, filterState, site, assignee),
    [state.rows, search, priority, filterState, site, assignee],
  );
  const count = state.rows.filter((row) => row.audible).length;
  const counter = useRef(count);
  const [announced, setAnnounced] = useState(0);
  useEffect(() => {
    counter.current = count;
  }, [count]);
  useEffect(() => {
    const timer = window.setInterval(() => {
      setAnnounced(counter.current);
    }, 1000);
    return () => {
      window.clearInterval(timer);
    };
  }, []);
  useEffect(() => {
    const original = document.title;
    const update = () => {
      document.title = document.hidden && count ? `[${String(count)}] ${original}` : original;
    };
    update();
    document.addEventListener("visibilitychange", update);
    return () => {
      document.removeEventListener("visibilitychange", update);
      document.title = original;
    };
  }, [count]);
  const played = useRef(new Map<string, number>());
  const candidates = useMemo(
    () => state.rows.filter((row) => row.audible && ["PRIORITY_P1", "PRIORITY_P2"].includes(row.priority)),
    [state.rows],
  );
  useEffect(() => {
    if (!sound || state.status !== "open" || mode === "training" || mode === "wall") return;
    let disposed = false;
    const notify = () => {
      const now = Date.now();
      const candidate = candidates.find(
        (row) =>
          !played.current.has(row.id) ||
          (row.priority === "PRIORITY_P1" && now - (played.current.get(row.id) ?? 0) >= 10000),
      );
      if (!candidate) return;
      // One signal per interval limits bursts without dropping the visual counter.
      played.current.set(candidate.id, now);
      void playAlert({ priority: candidate.priority.replace("PRIORITY_", ""), volume: Number(volume) / 100 }).then(
        () => {
          if (!disposed) setSoundFailed(false);
        },
        () => {
          if (!disposed) setSoundFailed(true);
        },
      );
    };
    notify();
    const timer = window.setInterval(notify, 1000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [candidates, sound, volume, state.status, mode]);
  useEffect(() => {
    const shortcut = (event: KeyboardEvent) => {
      if (
        !event.altKey ||
        event.ctrlKey ||
        event.metaKey ||
        (event.target instanceof HTMLElement &&
          (event.target.matches("input,textarea,select") || event.target.isContentEditable))
      )
        return;
      if (event.code === "KeyF") {
        event.preventDefault();
        document.getElementById("operator-search")?.focus();
      }
      if (event.code === "KeyR") {
        event.preventDefault();
        retry();
      }
    };
    window.addEventListener("keydown", shortcut);
    return () => {
      window.removeEventListener("keydown", shortcut);
    };
  }, [retry]);
  const priorities = [
    { value: "", label: t.all },
    ...[t.critical, t.high, t.normal, t.low].map((label, index) => ({
      value: `PRIORITY_P${String(index + 1)}`,
      label,
    })),
  ];
  const states = [
    { value: "", label: t.allStates },
    ...["NEW", "ACKNOWLEDGED", "IN_PROGRESS", "RESOLVED", "CLOSED"].map((value, index) => ({
      value: `INCIDENT_STATE_${value}`,
      label: [t.new, t.acknowledged, t.working, t.resolved, t.closed][index] ?? t.unknown,
    })),
  ];
  return (
    <main tabIndex={-1} id="main-content" className="workbench">
      <div className="page-heading">
        <div>
          <p className="eyebrow">{t.console}</p>
          <h1>{t.title}</h1>
        </div>
      </div>
      <p className="text-sm text-muted-foreground">{t.shortcuts}</p>
      <div className="operator-layout">
        <section className="min-w-0" aria-label={t.feed}>
          <div className="section-heading">
            <h2>{t.feed}</h2>
            <Button variant="secondary" onClick={retry}>
              {t.refresh}
            </Button>
          </div>
          <div className="operator-filters">
            <Field
              id="operator-search"
              label={t.search}
              value={search}
              onChange={(event) => {
                setSearch(event.target.value);
              }}
            />
            <SelectField
              label={t.priority}
              value={priority}
              options={priorities}
              onChange={(event) => {
                setPriority(event.target.value);
              }}
            />
            <SelectField
              label={t.filterState}
              value={filterState}
              options={states}
              onChange={(event) => {
                setFilterState(event.target.value);
              }}
            />
            <SelectField
              label={t.object}
              value={site}
              options={[
                { value: "", label: t.allSites },
                ...[...new Set(state.rows.flatMap((row) => (row.siteId ? [row.siteId] : [])))].map((value) => ({
                  value,
                  label: value,
                })),
              ]}
              onChange={(event) => {
                setSite(event.target.value);
              }}
            />
            <SelectField
              label={t.owner}
              value={assignee}
              options={[
                { value: "", label: t.allOwners },
                ...[...new Set(state.rows.flatMap((row) => (row.assigneeId ? [row.assigneeId] : [])))].map((value) => ({
                  value,
                  label: value,
                })),
              ]}
              onChange={(event) => {
                setAssignee(event.target.value);
              }}
            />
          </div>
          <p role="status">
            {t.newAlarms}: {announced}
          </p>
          <DataRegion
            loading={state.loading}
            hasData={state.loaded}
            error={state.error ? t.loadError : state.loaded && state.status !== "open" ? t.stale : undefined}
            labels={t}
          >
            {rows.length ? (
              <VirtualFeed rows={rows} selected={selected} choose={choose} />
            ) : (
              <EmptyState
                title={!state.loaded ? t.unavailable : state.rows.length ? t.noMatches : t.empty}
                description={!state.loaded ? t.loadError : state.rows.length ? t.noMatchesHint : t.emptyHint}
              />
            )}
          </DataRegion>
          <details>
            <summary>{t.sound}</summary>
            <p>{t.soundHint}</p>
            <Switch
              label={t.sound}
              checked={sound}
              onCheckedChange={(value) => {
                setPreferences({ sound: value });
              }}
            />
            <Field
              type="range"
              min="0"
              max="100"
              label={t.volume}
              value={volume}
              onChange={(event) => {
                setVolume(event.target.value);
              }}
            />
            <Button
              variant="secondary"
              onClick={() => {
                void playAlert({ volume: Number(volume) / 100 }).then(
                  () => {
                    setSoundFailed(false);
                  },
                  () => {
                    setSoundFailed(true);
                  },
                );
              }}
            >
              {t.soundTest}
            </Button>
            {soundFailed && <Notice tone="warning">{t.soundFailed}</Notice>}
          </details>
        </section>
        <div className="min-w-0">{children}</div>
      </div>
    </main>
  );
}
