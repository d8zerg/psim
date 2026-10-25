import { Select as SelectField } from "./fields";
import { useInfiniteQuery, useQuery } from "@tanstack/react-query";
import { useState } from "react";
import { Badge, Button, Card, Field, Notice } from "@psim/design-system";
import { useStrings } from "../ui/strings";
import type { SessionProfile } from "../auth";
import {
  statusOf,
  type Command,
  type CommandRequest,
  type Incident,
  type OperatorApi,
  type Run,
  type Schema,
} from "./api";
import { queryDefaults, type Actions } from "./queries";
export function CommandsPanel({
  api,
  incident,
  run,
  plan,
  live,
  writable,
  profile,
  actions,
}: {
  readonly api: OperatorApi;
  readonly incident: Incident;
  readonly run: Run | undefined;
  readonly plan: Schema["psim.response.v1.ResponsePlan"] | undefined;
  readonly live: boolean;
  readonly writable: boolean;
  readonly profile: SessionProfile;
  readonly actions: Actions;
}) {
  const t = useStrings();
  const commands = useInfiniteQuery({
    ...queryDefaults,
    queryKey: ["commands", incident.incidentId],
    queryFn: ({ signal, pageParam }) => api.commands(incident.incidentId ?? "", signal, pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  const entries = commands.data?.pages.flatMap((page) => page.commands ?? []) ?? [];
  if ([401, 403].includes(statusOf(commands.error))) return <Notice tone="warning">{t.forbidden}</Notice>;
  return (
    <Card title={t.commands}>
      <CommandForm
        api={api}
        incident={incident}
        run={run}
        plan={plan}
        writable={writable && !commands.isError}
        actions={actions}
        live={live}
        knownCommands={entries}
      />
      {commands.isError && <Notice tone="warning">{t.detailsError}</Notice>}
      <ul>
        {entries.map((command) => (
          <CommandItem
            key={command.commandId}
            command={command}
            api={api}
            actions={actions}
            writable={writable && !commands.isError}
            profile={profile}
          />
        ))}
      </ul>
      {commands.hasNextPage && (
        <Button
          disabled={commands.isFetchingNextPage}
          onClick={() => {
            void commands.fetchNextPage();
          }}
        >
          {t.loadMore}
        </Button>
      )}
    </Card>
  );
}
function CommandForm({
  api,
  incident,
  run,
  plan,
  writable,
  actions,
  live,
  knownCommands,
}: {
  readonly knownCommands: readonly Command[];
  readonly api: OperatorApi;
  readonly incident: Incident;
  readonly run: Run | undefined;
  readonly plan: Schema["psim.response.v1.ResponsePlan"] | undefined;
  readonly writable: boolean;
  readonly actions: Actions;
  readonly live: boolean;
}) {
  const t = useStrings();
  const [device, setDevice] = useState("");
  const [capability, setCapability] = useState("");
  const [stepId, setStepId] = useState("");
  const [parameters, setParameters] = useState<Record<string, string>>({});
  const [reason, setReason] = useState("");
  const [proposal, setProposal] = useState<{
    command: CommandRequest;
    preview: Awaited<ReturnType<OperatorApi["preview"]>>;
    signature: string;
    run: Run | undefined;
    items: readonly { command: CommandRequest; preview: Awaited<ReturnType<OperatorApi["preview"]>> }[];
  } | null>(null);
  const [submitted, setSubmitted] = useState(false);
  const [results, setResults] = useState<readonly Command[]>([]);
  const [resolvedIds, setResolvedIds] = useState<readonly string[]>([]);
  const ids = proposal?.run
    ? resolvedIds.length
      ? resolvedIds
      : (run?.steps?.find((entry) => entry.stepId === proposal.command.stepId)?.result?.commandIds ?? [])
    : proposal?.command.commandId
      ? [proposal.command.commandId]
      : [];
  const known = knownCommands.find((command) => ids.includes(command.commandId ?? ""));
  const result = results.find((command) => ids.includes(command.commandId ?? ""));
  const current = finished(known) ? known : finished(result) ? result : (known ?? result);
  const settled =
    ids.length > 0 &&
    ids.every(
      (id) =>
        finished(knownCommands.find((command) => command.commandId === id)) ||
        finished(results.find((command) => command.commandId === id)),
    );
  const step = plan?.steps?.find((step) => step.stepId === stepId && run?.activeStepIds?.includes(stepId));
  const group = step?.command?.targetExpression === "zone.devices";
  const deviceId = step?.command?.deviceId?.length ? step.command.deviceId : device;
  const commandCapability = step?.command?.capability?.length ? step.command.capability : capability;
  const deviceQuery = useQuery({
    ...queryDefaults,
    queryKey: ["device", deviceId],
    queryFn: ({ signal }) => api.device(deviceId, signal),
    enabled: live && !group && /^[0-9a-f-]{36}$/iu.test(deviceId),
  });
  const deviceDenied = [401, 403].includes(statusOf(deviceQuery.error));
  const capabilities = deviceDenied ? [] : (deviceQuery.data?.deviceType?.capabilities ?? []);
  const chosen = capabilities.find((item) => item.code === commandCapability);
  const effective = step?.command?.parameters ?? parameters;
  const signature = JSON.stringify([
    deviceId,
    commandCapability,
    effective,
    stepId,
    run?.runId,
    stepId ? run?.version : undefined,
    incident.version,
    reason,
  ]);
  const valid = group
    ? Boolean(incident.zoneId && commandCapability)
    : Boolean(chosen) &&
      chosen?.parameters?.every((param) => !param.required || Boolean(effective[param.name ?? ""])) !== false;
  const allowed = writable && !deviceQuery.isError && valid && (!stepId || Boolean(step?.command));
  async function preview() {
    const command: CommandRequest = {
      commandId: crypto.randomUUID(),
      deviceId,
      capability: commandCapability,
      parameters: effective,
      incidentId: incident.incidentId ?? "",
      ...(stepId ? { runId: run?.runId ?? "", stepId } : {}),
    };
    await actions.perform(async () => {
      const targets: string[] = [];
      if (group) {
        let token = "";
        do {
          const page = await api.devices(incident.zoneId ?? "", AbortSignal.timeout(15000), token);
          for (const item of page.devices ?? []) if (item.deviceId) targets.push(item.deviceId);
          if (targets.length > 128) throw new Error("Command target count exceeds limit");
          token = page.nextPageToken ?? "";
        } while (token);
      } else targets.push(deviceId);
      if (!targets.length) throw new Error("Command has no targets");
      const items = [];
      for (const target of targets) {
        const request = { ...command, deviceId: target };
        items.push({ command: request, preview: await api.preview(request) });
      }
      const first = items[0];
      if (!first) throw new Error("Command has no preview");
      setProposal({ ...first, signature, run: stepId ? run : undefined, items });
      setResolvedIds([]);
      setSubmitted(false);
      setResults([]);
    });
  }
  return (
    <div className="grid gap-2">
      {deviceQuery.isError && <Notice tone="warning">{deviceDenied ? t.forbidden : t.detailsError}</Notice>}
      <SelectField
        label={t.plan}
        value={stepId}
        options={[
          { value: "", label: t.command },
          ...(plan?.steps ?? [])
            .filter((step) => step.command && run?.activeStepIds?.includes(step.stepId ?? ""))
            .map((step) => ({ value: step.stepId ?? "", label: step.title ?? "" })),
        ]}
        onChange={(event) => {
          setStepId(event.target.value);
        }}
      />
      <Field
        label={group ? t.zoneColumn : t.deviceId}
        value={group ? (incident.zoneId ?? "") : deviceId}
        disabled={Boolean(step?.command?.deviceId) || group || submitted}
        onChange={(event) => {
          setDevice(event.target.value);
          setCapability("");
          setParameters({});
        }}
      />
      <SelectField
        label={t.capability}
        value={commandCapability}
        disabled={Boolean(step?.command) || submitted}
        options={[
          { value: "", label: t.capability },
          ...(group
            ? [{ value: commandCapability, label: commandCapability }]
            : capabilities.map((item) => ({ value: item.code ?? "", label: item.name ?? item.code ?? "" }))),
        ]}
        onChange={(event) => {
          setCapability(event.target.value);
          setParameters({});
        }}
      />
      {chosen?.parameters?.map((param) => (
        <Field
          key={param.name}
          label={`${t.parameters}: ${param.name ?? ""}`}
          required={param.required}
          value={effective[param.name ?? ""] ?? ""}
          disabled={Boolean(step?.command) || submitted}
          onChange={(event) => {
            setParameters((previous) => ({ ...previous, [param.name ?? ""]: event.target.value }));
          }}
        />
      ))}
      <Field
        label={`${t.reason}: ${t.command}`}
        value={reason}
        disabled={submitted}
        onChange={(event) => {
          setReason(event.target.value);
        }}
      />
      <Button
        variant="secondary"
        disabled={!allowed || !reason.trim() || submitted}
        onClick={() => {
          void preview();
        }}
      >
        {t.previewCommand}
      </Button>
      {!deviceDenied && proposal && (
        <div>
          {proposal.items.map((item) => (
            <div key={item.command.deviceId}>
              <p>
                {t.deviceId}: {item.command.deviceId} · {t.capability}: {item.command.capability}
              </p>
              <p>
                {t.consequence}: {item.preview.consequenceUnknown ? t.unknownConsequence : item.preview.consequence}
              </p>
              {item.preview.requiresSecondSupervisor && <Notice live={false}>{t.requiresApproval}</Notice>}
            </div>
          ))}
          {proposal.signature !== signature && !submitted && <Notice tone="warning">{t.previewExpired}</Notice>}
          <Button
            variant="danger"
            disabled={!allowed || submitted || proposal.signature !== signature}
            onClick={() => {
              setSubmitted(true);
              void actions.perform(async () => {
                // Record the operational reason before dispatch; a lost dispatch response is reconciled by ID.
                await api.comment(incident, reason);
                if (proposal.run && proposal.command.stepId) {
                  const response = await api.step(proposal.run, proposal.command.stepId, "complete", { note: reason });
                  setResolvedIds(
                    response.run?.steps?.find((entry) => entry.stepId === proposal.command.stepId)?.result
                      ?.commandIds ?? [],
                  );
                } else {
                  const response = await api.send({
                    ...proposal.command,
                    previewFingerprint: proposal.preview.fingerprint ?? "",
                  });
                  setResults(response.command ? [response.command] : []);
                }
              });
            }}
          >
            {t.confirm}
          </Button>
          {submitted && (
            <>
              <p>
                {ids.length ? (
                  ids.map((id) => <code key={id}>{id} </code>)
                ) : (
                  <span>
                    {t.run}: <code>{proposal.run?.runId}</code>
                  </span>
                )}
              </p>
              <Notice tone="warning" live={false}>
                {current ? commandState(current, t) : t.outcomeUnknown}
              </Notice>
              <Button
                variant="secondary"
                disabled={!live || actions.pending}
                onClick={() => {
                  void actions.perform(async () => {
                    let requested = ids;
                    if (proposal.run) {
                      const response = await api.run(incident.incidentId ?? "", AbortSignal.timeout(15000));
                      requested =
                        response.run?.steps?.find((entry) => entry.stepId === proposal.command.stepId)?.result
                          ?.commandIds ?? [];
                      setResolvedIds(requested);
                    }
                    const checked: Command[] = [];
                    for (const id of requested) {
                      const response = await api.command(id, AbortSignal.timeout(15000));
                      if (response.command) checked.push(response.command);
                    }
                    setResults(checked);
                  });
                }}
              >
                {t.refreshCommand}
              </Button>
              <Button
                variant="secondary"
                disabled={!writable || !settled}
                onClick={() => {
                  setSubmitted(false);
                  setProposal(null);
                  setResults([]);
                  setResolvedIds([]);
                  setStepId("");
                  setReason("");
                }}
              >
                {t.newCommand}
              </Button>
            </>
          )}
        </div>
      )}
    </div>
  );
}
function finished(command: Command | null | undefined): boolean {
  return [
    "COMMAND_STATE_EXECUTED",
    "COMMAND_STATE_FAILED",
    "COMMAND_STATE_REJECTED",
    "COMMAND_STATE_TIMED_OUT",
    "COMMAND_STATE_CANCELLED",
  ].includes(command?.state ?? "");
}
function commandState(command: Command, t: ReturnType<typeof useStrings>): string {
  const labels = {
    COMMAND_STATE_UNSPECIFIED: t.unknown,
    COMMAND_STATE_REQUESTED: t.requested,
    COMMAND_STATE_AWAITING_APPROVAL: t.awaitingApproval,
    COMMAND_STATE_SENT: t.sent,
    COMMAND_STATE_ACKNOWLEDGED: t.deviceAcknowledged,
    COMMAND_STATE_EXECUTED: t.executed,
    COMMAND_STATE_FAILED: t.failed,
    COMMAND_STATE_REJECTED: t.rejected,
    COMMAND_STATE_TIMED_OUT: t.timedOut,
    COMMAND_STATE_CANCELLED: t.cancelled,
    COMMAND_STATE_UNKNOWN: t.outcomeUnknown,
  };
  return labels[command.state ?? "COMMAND_STATE_UNSPECIFIED"];
}
function CommandItem({
  command,
  api,
  actions,
  writable,
  profile,
}: {
  readonly command: Command;
  readonly api: OperatorApi;
  readonly actions: Actions;
  readonly writable: boolean;
  readonly profile: SessionProfile;
}) {
  const t = useStrings();
  const [preview, setPreview] = useState<{ fingerprint: string; version: string; consequence: string } | null>(null);
  const [reason, setReason] = useState("");
  const canApprove =
    profile.roles.includes("supervisor") &&
    command.requestedBy?.id !== profile.subject &&
    command.state === "COMMAND_STATE_AWAITING_APPROVAL";
  return (
    <li>
      <p>
        <code>{command.commandId}</code> · {command.deviceId} · {command.capability} ·{" "}
        <Badge>{commandState(command, t)}</Badge>
      </p>
      {command.resultCode && <p>{command.resultCode}</p>}
      {canApprove && (
        <>
          <Button
            variant="secondary"
            disabled={!writable}
            onClick={() => {
              void actions.perform(async () => {
                const value = await api.preview({
                  commandId: command.commandId ?? "",
                  deviceId: command.deviceId ?? "",
                  capability: command.capability ?? "",
                  parameters: command.parameters ?? {},
                  incidentId: command.incidentId ?? "",
                  runId: command.runId ?? "",
                  stepId: command.stepId ?? "",
                });
                setPreview({
                  fingerprint: value.fingerprint ?? "",
                  version: command.version ?? "",
                  consequence: value.consequenceUnknown ? t.unknownConsequence : (value.consequence ?? ""),
                });
              });
            }}
          >
            {t.previewCommand}
          </Button>
          {preview && (
            <>
              <p>
                {t.consequence}: {preview.consequence}
              </p>
              <Button
                variant="danger"
                disabled={!writable || preview.version !== command.version}
                onClick={() => {
                  void actions.perform(() => api.commandAction(command, "approve", preview.fingerprint));
                }}
              >
                {t.approveCommand}
              </Button>
            </>
          )}
          <Field
            label={`${t.reason}: ${command.commandId ?? ""}`}
            value={reason}
            onChange={(event) => {
              setReason(event.target.value);
            }}
          />
          <Button
            variant="secondary"
            disabled={!writable || !reason.trim()}
            onClick={() => {
              void actions.perform(() => api.commandAction(command, "reject", "", reason));
            }}
          >
            {t.rejectCommand}
          </Button>
        </>
      )}
      {["COMMAND_STATE_REQUESTED", "COMMAND_STATE_AWAITING_APPROVAL"].includes(command.state ?? "") && (
        <Button
          variant="secondary"
          disabled={!writable}
          onClick={() => {
            void actions.perform(() => api.commandAction(command, "cancel"));
          }}
        >
          {t.cancelCommand}
        </Button>
      )}
    </li>
  );
}
