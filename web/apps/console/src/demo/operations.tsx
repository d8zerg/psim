import { useEffect, useReducer, useRef, useState } from "react";
import { Badge, Button, Card, Field, Notice, SelectField, Switch } from "@psim/design-system";
import { DataTable, FloorPlan, TrendChart } from "@psim/design-system/collections";
import { Checkbox, Slider } from "@psim/design-system/controls";
import {
  AckButton,
  AlarmBanner,
  AlarmStateBadge,
  AuditReasonField,
  CameraTile,
  CommandLifecycle,
  ConnectionIndicator,
  PriorityBadge,
  Provenance,
  SlaTimer,
  Timestamp,
} from "@psim/design-system/domain";
import { CommandPalette } from "@psim/design-system/navigation";
import { Sheet } from "@psim/design-system/overlays";
import { ConfirmAction } from "@psim/design-system/confirm-action";
import { draftCommand, transitionCommand, type CommandEvent, type CommandPhase } from "@psim/design-system/alarm-model";
import { useDemoStrings } from "../ui/strings";
import { setPreferences, usePreferences } from "../ui/preferences";
import { playAlert } from "../ui/alert-sound";
import { attention, createSimulator, currentFrame, groupAlarms, isCurrent, simulate, soundDecision } from "./simulator";

export default function Operations() {
  const t = useDemoStrings();
  const language = usePreferences((state) => state.language);
  const mode = usePreferences((state) => state.mode);
  const [state, dispatch] = useReducer(simulate, 42, (seed) => simulate(createSimulator(seed), { type: "advance" }));
  const frame = currentFrame(state);
  const current = isCurrent(state);
  const [selectedId, setSelectedId] = useState("");
  const selected = frame.records.find((record) => record.id === selectedId) ?? frame.records[0];
  const [reason, setReason] = useState("");
  const [sourceFilter, setSourceFilter] = useState("");
  const [layout, setLayout] = useState("single");
  const [timeZone, setTimeZone] = useState("UTC");
  const [leaseUntil, setLeaseUntil] = useState(0);
  const lease = current && leaseUntil > frame.now;
  const [receiver, setReceiver] = useState("");
  const [reviewedRevision, setReviewedRevision] = useState<number | null>(null);
  const reviewed = reviewedRevision === frame.revision;
  const [handover, setHandover] = useState<{ receiver: string; revision: number } | null>(null);
  const [supervisor, setSupervisor] = useState("supervisor-b");
  const [command, setCommand] = useState(draftCommand);
  const executionButton = useRef<HTMLButtonElement>(null);
  const [question, setQuestion] = useState("");
  const [sound, setSound] = useState(false);
  const [volume, setVolume] = useState(30);
  const [soundError, setSoundError] = useState(false);
  const [background, setBackground] = useState(false);
  const [night, setNight] = useState(false);
  const [breakEnabled, setBreakEnabled] = useState(false);
  const [lastBreak, setLastBreak] = useState(frame.now);
  const budget = attention(frame.records, frame.now);
  const groups = groupAlarms(frame.records);
  const records = frame.records.filter((record) => !sourceFilter || record.source === sourceFilter);
  const priorityLabels = { P1: t.opCritical, P2: t.opHigh, P3: t.opNormalPriority, P4: t.opLow };
  const alarmLabels = {
    normal: t.opNormal,
    "active-unacknowledged": t.opActiveUnack,
    "active-acknowledged": t.opActiveAck,
    "returned-unacknowledged": t.opReturned,
    visible: t.opVisible,
    shelved: t.opShelved,
    suppressed: t.opSuppressed,
  };
  const commandLabels: Record<CommandPhase, string> = {
    draft: t.opDraft,
    "awaiting-approval": t.opAwaiting,
    approved: t.opApproved,
    sent: t.opSent,
    acknowledged: t.opDeviceAck,
    executed: t.opDone,
    failed: t.opFailed,
    rejected: t.opRejected,
    "timed-out": t.opTimedOut,
    cancelled: t.opCancelled,
    unknown: t.opUnknownPhase,
  };
  const channel = !frame.connected ? "disconnected" : frame.latency > 1000 ? "delayed" : "connected";
  const [announced, setAnnounced] = useState(0);
  const latestCount = useRef(0);
  useEffect(() => {
    latestCount.current = budget.unacknowledged;
  }, [budget.unacknowledged]);
  useEffect(() => {
    const timer = window.setInterval(() => {
      setAnnounced(latestCount.current);
    }, 1000);
    return () => {
      window.clearInterval(timer);
    };
  }, []);
  useEffect(() => {
    const visibility = () => {
      setBackground(document.hidden);
    };
    visibility();
    document.addEventListener("visibilitychange", visibility);
    return () => {
      document.removeEventListener("visibilitychange", visibility);
    };
  }, []);
  useEffect(() => {
    const title = document.title;
    if (background) document.title = `[${String(budget.unacknowledged)}] ${title}`;
    return () => {
      document.title = title;
    };
  }, [background, budget.unacknowledged]);
  useEffect(() => {
    if (!night) return;
    const previous = usePreferences.getState().theme;
    const apply = () => {
      const hour = new Date().getHours();
      setPreferences({ theme: hour >= 22 || hour < 6 ? "night" : previous });
    };
    apply();
    const timer = window.setInterval(apply, 60000);
    return () => {
      window.clearInterval(timer);
      setPreferences({ theme: previous });
    };
  }, [night]);
  const soundCandidate =
    frame.records.find(
      (record) => record.priority === "P1" && !record.alarm.acknowledged && record.alarm.visibility.kind === "visible",
    ) ??
    frame.records.find(
      (record) => record.priority === "P2" && !record.alarm.acknowledged && record.alarm.visibility.kind === "visible",
    );
  const played = useRef(new Map<string, number>());
  useEffect(() => {
    if (!soundCandidate || !current) return;
    let disposed = false;
    const play = () => {
      const now = Date.now();
      if (
        !soundDecision({
          enabled: sound,
          priority: soundCandidate.priority,
          acknowledged: soundCandidate.alarm.acknowledged,
          visible: soundCandidate.alarm.visibility.kind === "visible",
          now,
          lastPlayed: played.current.get(soundCandidate.id),
        })
      )
        return;
      played.current.set(soundCandidate.id, now);
      void playAlert({ priority: soundCandidate.priority, volume: volume / 100 }).then(
        () => {
          if (!disposed) setSoundError(false);
        },
        () => {
          if (!disposed) setSoundError(true);
        },
      );
    };
    play();
    const timer = window.setInterval(play, 1000);
    return () => {
      disposed = true;
      window.clearInterval(timer);
    };
  }, [soundCandidate, current, sound, volume]);
  function commandEvent(event: CommandEvent) {
    setCommand((previous) => transitionCommand(previous, event));
  }

  const sources = selected
    ? [
        { id: selected.source, label: t.opSource, value: selected.id, sequence: `snapshot:${String(frame.revision)}` },
        {
          id: "rule:priority",
          label: t.priority,
          value: `${selected.priority}: ${priorityLabels[selected.priority]}`,
          sequence: "rule:v1",
        },
        { id: "assignment", label: t.opOwner, value: selected.owner, sequence: `snapshot:${String(frame.revision)}` },
      ]
    : [];
  const evidence = sources.filter((source) =>
    `${source.id} ${source.label} ${source.value}`.toLocaleLowerCase().includes(question.toLocaleLowerCase()),
  );
  const actions = [
    {
      id: "next",
      label: t.opAdvance,
      disabled: state.cursor !== state.frames.length - 1,
      onSelect: () => {
        dispatch({ type: "advance" });
      },
    },
    {
      id: "ack",
      label: t.opAck,
      disabled: !selected || !current || selected.alarm.acknowledged,
      onSelect: () => {
        if (selected && current) dispatch({ type: "alarm", id: selected.id, action: "acknowledge" });
      },
    },
  ];
  return (
    <div className="grid min-w-0 gap-4" data-testid="operations" data-layout={layout}>
      <Notice live={false}>{t.opHint}</Notice>
      <div className="flex flex-wrap items-end gap-2">
        <CommandPalette label={t.opShortcuts} searchLabel={t.opFilter} closeLabel={t.opClose} actions={actions} />
        <SelectField
          label={t.opLayout}
          value={layout}
          onChange={(event) => {
            setLayout(event.target.value);
          }}
        >
          <option value="single">{t.opSingle}</option>
          <option value="dual">{t.opDual}</option>
          <option value="wall">{t.opWall}</option>
        </SelectField>
        <SelectField
          label={t.opTimeZone}
          value={timeZone}
          onChange={(event) => {
            setTimeZone(event.target.value);
          }}
        >
          <option>UTC</option>
          <option>Europe/Moscow</option>
        </SelectField>
        <code>{t.opSeed}: 42</code>
      </div>
      <p>{t.opShortcutText}</p>
      <div className="flex flex-wrap gap-2">
        {(
          [
            { type: "advance", label: t.opAdvance },
            { type: "flood", label: t.opFlood },
            { type: "disconnect", label: t.opDisconnect },
            { type: "recover", label: t.opRecover },
            { type: "delay", label: t.opDelay },
            { type: "shift", label: t.opShift },
          ] as const
        ).map((action) => (
          <Button
            key={action.type}
            variant="secondary"
            disabled={
              state.cursor !== state.frames.length - 1 ||
              (action.type === "recover" ? frame.connected : action.type === "disconnect" ? !frame.connected : false)
            }
            onClick={() => {
              if (action.type === "disconnect") commandEvent({ type: "disconnect" });
              dispatch({ type: action.type });
            }}
          >
            {action.label}
          </Button>
        ))}
      </div>
      <Slider
        label={t.opRewind}
        min={0}
        max={state.frames.length - 1}
        value={state.cursor}
        onChange={(event) => {
          dispatch({ type: "rewind", cursor: event.target.valueAsNumber });
        }}
      />
      {state.cursor !== state.frames.length - 1 && (
        <Notice
          tone="warning"
          live={false}
          action={
            <Button
              variant="secondary"
              onClick={() => {
                dispatch({ type: "rewind", cursor: state.frames.length - 1 });
              }}
            >
              {t.opLive}
            </Button>
          }
        >
          {t.opHistory}
        </Notice>
      )}
      <div className="flex flex-wrap gap-3">
        <ConnectionIndicator
          state={channel}
          labels={{ connected: t.opConnected, delayed: t.opDelayed, disconnected: t.opDisconnected }}
        />
        <span>
          {t.opLatency}: {frame.latency}
        </span>
        <span>
          {t.opQueue}: {frame.queued}
        </span>
      </div>
      {frame.recovered > 0 && (
        <Notice live={false}>
          {t.opRecovered}: {frame.recovered}; {t.opRisk}: {budget.slaRisk}
        </Notice>
      )}
      <p data-testid="operations-announcement" role="status" aria-live="polite" aria-atomic="true">
        {background ? t.opBackground : t.opUnack}: {announced}
      </p>
      <AlarmBanner
        label={budget.flood ? t.opFloodState : t.opAttention}
        count={budget.unacknowledged}
        priority={frame.records.some((record) => record.priority === "P1" && !record.alarm.acknowledged) ? "P1" : "P3"}
        countLabel={t.opUnack}
      />
      <div className={layout === "single" ? "grid gap-4" : "grid gap-4 xl:grid-cols-2"}>
        <Card title={t.opAttention} headingLevel={3}>
          <dl className="grid grid-cols-2 gap-2">
            <dt>{t.opUnack}</dt>
            <dd>{budget.unacknowledged}</dd>
            <dt>{t.opRecent}</dt>
            <dd>{budget.recent}</dd>
            <dt>{t.opRisk}</dt>
            <dd>{budget.slaRisk}</dd>
          </dl>
          <TrendChart
            label={t.opTrend}
            valueLabel={t.opCount}
            values={(["P1", "P2", "P3", "P4"] as const).map((priority) => ({
              label: priority,
              value: frame.records.filter((record) => record.priority === priority).length,
            }))}
          />
        </Card>
        <Card title={t.opGroups} headingLevel={3}>
          <Button
            variant="ghost"
            onClick={() => {
              setSourceFilter("");
            }}
          >
            {t.opAll}
          </Button>
          <ul className="grid gap-1 sm:grid-cols-2">
            {groups.map((group) => (
              <li key={group.id}>
                <Button
                  variant="ghost"
                  onClick={() => {
                    setSourceFilter(group.id);
                    setSelectedId(group.records[0]?.id ?? "");
                  }}
                >
                  {group.id}: {group.count} ({group.priority})
                </Button>
                <Timestamp timestamp={group.oldest} now={frame.now} locale={language} timeZone={timeZone} />
              </li>
            ))}
          </ul>
        </Card>
      </div>
      <DataTable
        label={t.opGrid}
        data={records}
        columns={[
          { accessorKey: "id", header: "ID" },
          { accessorKey: "source", header: t.opSource },
          {
            accessorKey: "priority",
            header: t.priority,
            cell: (info) => <PriorityBadge priority={info.row.original.priority} labels={priorityLabels} />,
          },
          { accessorKey: "owner", header: t.opOwner },
        ]}
        labels={{
          search: t.opFilter,
          select: t.opSelect,
          columns: t.opColumns,
          pin: t.opPin,
          bulk: t.opBulk,
          empty: t.opEmpty,
        }}
        bulkDisabled={!current}
        onActiveChange={setSelectedId}
        onBulkAction={(ids) => {
          if (current) for (const id of ids) dispatch({ type: "alarm", action: "acknowledge", id });
        }}
      />
      {selected && (
        <div className={layout === "single" ? "grid gap-4" : "grid gap-4 xl:grid-cols-2"}>
          <Card id="operations-evidence" title={`${t.opDetails}: ${selected.id}`} headingLevel={3}>
            <div className="grid gap-3">
              <PriorityBadge priority={selected.priority} labels={priorityLabels} />
              <AlarmStateBadge alarm={selected.alarm} labels={alarmLabels} />
              <Timestamp timestamp={selected.at} now={frame.now} locale={language} timeZone={timeZone} />
              <SlaTimer
                deadline={selected.deadline}
                now={frame.now}
                label={t.opSla}
                expiredLabel={t.opExpired}
                locale={language}
              />
              <p>
                {t.opOwner}: {selected.owner}
              </p>
              <Provenance
                label={t.opProvenance}
                value={selected.source}
                stale={!current}
                staleLabel={t.opStale}
                whyLabel={t.opWhy}
                sources={sources}
              />
              <AuditReasonField
                label={t.opReason}
                value={reason}
                onChange={(event) => {
                  setReason(event.target.value);
                }}
              />
              <div className="flex flex-wrap gap-2">
                <AckButton
                  label={t.opAck}
                  acknowledgedLabel={t.opAcknowledged}
                  acknowledged={selected.alarm.acknowledged}
                  disabled={!current}
                  onClick={() => {
                    dispatch({ type: "alarm", id: selected.id, action: "acknowledge" });
                  }}
                />
                {(
                  [
                    { action: "clear", label: t.opClear },
                    { action: "shelve", label: t.opShelve },
                    { action: "suppress", label: t.opSuppress },
                    { action: "restore", label: t.opRestore },
                  ] as const
                ).map((item) => (
                  <Button
                    key={item.action}
                    variant="secondary"
                    disabled={!current || ((item.action === "shelve" || item.action === "suppress") && !reason.trim())}
                    onClick={() => {
                      dispatch({ type: "alarm", id: selected.id, action: item.action, reason });
                    }}
                  >
                    {item.label}
                  </Button>
                ))}
                <Button
                  variant="secondary"
                  disabled={!current}
                  onClick={() => {
                    dispatch({ type: "assign", id: selected.id, owner: "operator-b" });
                  }}
                >
                  {t.opReassign}
                </Button>
              </div>
              <p>{t.opPresence}: operator-b</p>
              <p>{t.opLeaseHint}</p>
              <Button
                variant="ghost"
                aria-pressed={lease}
                disabled={!current}
                onClick={() => {
                  setLeaseUntil(lease ? 0 : frame.now + 300000);
                }}
              >
                {lease ? t.opReleaseLease : t.opTakeLease}
              </Button>
              {lease && <Badge>{t.opLease}: operator-a</Badge>}
            </div>
          </Card>
          <Card title={t.opPlan} headingLevel={3}>
            <FloorPlan
              label={t.opPlan}
              markers={groups.map((group, index) => ({
                id: group.id,
                label: group.id,
                x: 8 + (index % 5) * 20,
                y: 15 + Math.floor(index / 5) * 20,
                state: group.records.some((record) => record.alarm.condition === "active") ? "alarm" : "normal",
              }))}
              selected={selected.source}
              onSelect={(source) => {
                const record = frame.records.find((item) => item.source === source);
                if (record) setSelectedId(record.id);
              }}
              labels={{
                zoomIn: t.opZoomIn,
                zoomOut: t.opZoomOut,
                reset: t.opResetZoom,
                legend: t.opLegend,
                normal: t.opNormal,
                alarm: t.alarm,
                offline: t.opOffline,
              }}
            />
            <CameraTile
              label={t.opCamera}
              state="offline"
              stateLabel={t.opCameraUnavailable}
              onOpen={() => {
                /* The fixture has no video transport. */
              }}
              openLabel={t.opOpenCamera}
            />
          </Card>
        </div>
      )}
      <div className="grid gap-4 xl:grid-cols-2">
        <Card title={t.opCommand} headingLevel={3}>
          <p>{t.opConsequences}</p>
          <CommandLifecycle command={command} labels={commandLabels} label={t.opCommand} />
          <div className="flex flex-wrap gap-2">
            <Button
              disabled={!current || !reason.trim() || command.phase !== "draft"}
              onClick={() => {
                commandEvent({ type: "request", actor: "operator-a", target: "DOOR-42", reason, current });
              }}
            >
              {t.opRequest}
            </Button>
            <Sheet
              trigger={
                <Button
                  variant="secondary"
                  disabled={!current || (command.phase !== "awaiting-approval" && command.phase !== "approved")}
                >
                  {t.opApprove}
                </Button>
              }
              title={t.opApprove}
              description={t.opConsequences}
              closeLabel={t.opClose}
              className="max-w-sm"
            >
              <p>
                {command.target}: {command.reason}
              </p>
              <Field
                label={t.opSupervisor}
                value={supervisor}
                onChange={(event) => {
                  setSupervisor(event.target.value);
                }}
              />
              <Button
                disabled={
                  !current ||
                  !supervisor.trim() ||
                  supervisor.trim() === command.actor ||
                  command.phase !== "awaiting-approval"
                }
                onClick={() => {
                  commandEvent({ type: "approve", reviewer: supervisor, current });
                }}
              >
                {t.opApprove}
              </Button>
            </Sheet>
            <ConfirmAction
              disabled={!current || command.phase !== "approved"}
              trigger={t.opSend}
              title={t.opSend}
              description={t.opConsequences}
              details={
                <p>
                  {command.target}: {command.reason}
                </p>
              }
              confirm={t.confirm}
              cancel={t.cancel}
              onConfirm={() => {
                commandEvent({ type: "send", current });
              }}
              onCloseAutoFocus={(event) => {
                if (command.phase === "sent") {
                  event.preventDefault();
                  executionButton.current?.focus();
                }
              }}
            />
            <Button
              ref={executionButton}
              variant="secondary"
              disabled={!current || command.phase !== "sent"}
              onClick={() => {
                commandEvent({ type: "execute" });
              }}
            >
              {t.opExecuted}
            </Button>
          </div>
          {command.phase === "unknown" && (
            <Notice tone="warning" live={false}>
              {t.opUnknown}
            </Notice>
          )}
        </Card>
        <Card title={t.opHandover} headingLevel={3}>
          <p>
            {t.opUnack}: {budget.unacknowledged}; {t.opShelved}:{" "}
            {frame.records.filter((record) => record.alarm.visibility.kind !== "visible").length}; {t.opCommand}:{" "}
            {commandLabels[command.phase]}
          </p>
          <Field
            label={t.opReceiver}
            value={receiver}
            onChange={(event) => {
              setReceiver(event.target.value);
              setHandover(null);
            }}
          />
          <Checkbox
            label={t.opReviewed}
            checked={reviewed}
            onChange={(event) => {
              setReviewedRevision(event.target.checked ? frame.revision : null);
            }}
          />
          <Button
            disabled={!current || !reviewed || !receiver.trim() || receiver.trim() === "operator-a"}
            onClick={() => {
              setHandover({ receiver: receiver.trim(), revision: frame.revision });
            }}
          >
            {t.opAcceptShift}
          </Button>
          {handover && (
            <p>
              {t.opHandoverDone}: {handover.receiver}; snapshot:{handover.revision}
            </p>
          )}
        </Card>
        <Card title={t.opAssistant} headingLevel={3}>
          <p>{t.opAssistantHint}</p>
          <Field
            label={t.opQuestion}
            value={question}
            onChange={(event) => {
              setQuestion(event.target.value);
            }}
          />
          {evidence.length ? (
            <ul>
              {evidence.map((source) => (
                <li key={source.id}>
                  <a className="underline" href="#operations-evidence">
                    [{source.id}]
                  </a>{" "}
                  {source.label}: {source.value} <code>{source.sequence}</code>
                </li>
              ))}
            </ul>
          ) : (
            <p>{t.opNoSources}</p>
          )}
        </Card>
        <Card title={t.training} headingLevel={3}>
          <div className="flex flex-wrap gap-2">
            <Button
              variant="secondary"
              onClick={() => {
                setPreferences({ mode: "training" });
              }}
            >
              {t.opTraining}
            </Button>
            <Button
              variant="secondary"
              onClick={() => {
                setPreferences({ mode: "crisis" });
              }}
            >
              {t.opCrisis}
            </Button>
            <Button
              variant="ghost"
              onClick={() => {
                setPreferences({ mode: "normal" });
              }}
            >
              {t.opNormalMode}
            </Button>
          </div>
          {mode === "training" && (
            <p>
              {t.opTrainingScore}: {state.trainingScore}
            </p>
          )}
          <Switch label={t.opSound} checked={sound} onCheckedChange={setSound} />
          <Slider
            label={t.opVolume}
            min={0}
            max={100}
            value={volume}
            onChange={(event) => {
              setVolume(event.target.valueAsNumber);
            }}
          />
          <Button
            variant="secondary"
            onClick={() => {
              void playAlert({ priority: "P2", volume: volume / 100 }).then(
                () => {
                  setSoundError(false);
                },
                () => {
                  setSoundError(true);
                },
              );
            }}
          >
            {t.opSpeaker}
          </Button>
          {soundError && (
            <Notice tone="warning" live={false}>
              {t.opSoundBlocked}
            </Notice>
          )}
          <Switch label={t.opNight} checked={night} onCheckedChange={setNight} />
          <Switch label={t.opBreak} checked={breakEnabled} onCheckedChange={setBreakEnabled} />
          {breakEnabled && frame.now - lastBreak >= 2700000 && (
            <Notice
              live={false}
              action={
                <Button
                  variant="secondary"
                  onClick={() => {
                    setLastBreak(frame.now);
                  }}
                >
                  {t.opBreakAck}
                </Button>
              }
            >
              {t.opBreakDue}
            </Notice>
          )}
        </Card>
      </div>
    </div>
  );
}
