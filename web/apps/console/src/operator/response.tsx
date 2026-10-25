import { Select as SelectField } from "./fields";
import { useState } from "react";
import { Badge, Button, Card, Field } from "@psim/design-system";
import type { OperatorApi, Run, Schema, Step } from "./api";
import type { Actions } from "./queries";
import { useStrings } from "../ui/strings";
export function RunPanel({
  api,
  response,
  writable,
  actions,
  supervisor,
}: {
  readonly api: OperatorApi;
  readonly response: Awaited<ReturnType<OperatorApi["run"]>> | undefined;
  readonly writable: boolean;
  readonly actions: Actions;
  readonly supervisor: boolean;
}) {
  const t = useStrings();
  const run = response?.run;
  return (
    <Card title={t.plan}>
      {run ? (
        <>
          <p>
            {response.plan?.name} · {t.version}: {run.planVersion}
          </p>
          {run.steps?.map((execution) => {
            const step = response.plan?.steps?.find((step) => step.stepId === execution.stepId);
            return step ? (
              <StepPanel
                key={`${run.runId ?? ""}:${step.stepId ?? ""}:${String(execution.attempt ?? 0)}`}
                api={api}
                run={run}
                step={step}
                execution={execution}
                writable={writable}
                actions={actions}
                supervisor={supervisor}
              />
            ) : null;
          })}
        </>
      ) : (
        <p>{t.noRun}</p>
      )}
    </Card>
  );
}
function StepPanel({
  api,
  run,
  step,
  execution,
  writable,
  actions,
  supervisor,
}: {
  readonly api: OperatorApi;
  readonly run: Run;
  readonly step: Step;
  readonly execution: Schema["psim.response.v1.StepExecution"];
  readonly writable: boolean;
  readonly actions: Actions;
  readonly supervisor: boolean;
}) {
  const t = useStrings();
  const [checked, setChecked] = useState<readonly string[]>([]);
  const [option, setOption] = useState("");
  const [note, setNote] = useState("");
  const labels = {
    STEP_STATE_UNSPECIFIED: t.unknown,
    STEP_STATE_PENDING: t.stepPendingLabel,
    STEP_STATE_ACTIVE: t.stepActive,
    STEP_STATE_COMPLETED: t.stepCompleted,
    STEP_STATE_SKIPPED: t.stepSkipped,
    STEP_STATE_FAILED: t.stepFailed,
    STEP_STATE_CANCELLED: t.stepCancelled,
  };
  const valid =
    (!step.checklist || step.checklist.items?.every((item) => !item.required || checked.includes(item.itemId ?? ""))) &&
    (!step.decision || Boolean(option));
  const manual = Boolean(step.instruction ?? step.checklist ?? step.decision);
  const active = execution.state === "STEP_STATE_ACTIVE";
  const result = { checkedItemIds: [...checked], decisionOptionId: option, note };
  return (
    <section className="grid gap-2" aria-label={step.title}>
      <h3>
        {step.title} {step.mandatory && <Badge>{t.mandatory}</Badge>}
      </h3>
      <p>{labels[execution.state ?? "STEP_STATE_UNSPECIFIED"]}</p>
      {step.instruction && <p>{step.instruction.text}</p>}
      {step.checklist?.items?.map((item) => (
        <label key={item.itemId}>
          <input
            type="checkbox"
            checked={checked.includes(item.itemId ?? "")}
            disabled={!writable || !active}
            onChange={(event) => {
              setChecked((previous) =>
                event.target.checked ? [...previous, item.itemId ?? ""] : previous.filter((id) => id !== item.itemId),
              );
            }}
          />
          {item.text}
          {item.required && ` (${t.mandatory})`}
        </label>
      ))}
      {step.decision && (
        <SelectField
          label={step.decision.question ?? t.decision}
          value={option}
          disabled={!writable || !active}
          options={[
            { value: "", label: t.decision },
            ...(step.decision.options ?? []).map((item) => ({ value: item.optionId ?? "", label: item.label ?? "" })),
          ]}
          onChange={(event) => {
            setOption(event.target.value);
          }}
        />
      )}
      <Field
        label={`${t.reason}: ${step.title ?? ""}`}
        value={note}
        onChange={(event) => {
          setNote(event.target.value);
        }}
      />
      <div className="flex flex-wrap gap-2">
        {manual && (
          <Button
            disabled={!writable || !active || !valid}
            onClick={() => {
              void actions.perform(() => api.step(run, step.stepId ?? "", "complete", result));
            }}
          >
            {t.completeStep}
          </Button>
        )}
        {!step.mandatory && (
          <Button
            variant="secondary"
            disabled={!writable || !active || !note.trim()}
            onClick={() => {
              void actions.perform(() => api.step(run, step.stepId ?? "", "skip", result));
            }}
          >
            {t.skipStep}
          </Button>
        )}
        {execution.state === "STEP_STATE_FAILED" && (
          <>
            <Button
              variant="secondary"
              disabled={!writable}
              onClick={() => {
                void actions.perform(() => api.step(run, step.stepId ?? "", "retry", result));
              }}
            >
              {t.retryStep}
            </Button>
            {supervisor && (
              <Button
                variant="secondary"
                disabled={!writable || !note.trim()}
                onClick={() => {
                  void actions.perform(() => api.step(run, step.stepId ?? "", "override", result));
                }}
              >
                {t.overrideStep}
              </Button>
            )}
          </>
        )}
      </div>
    </section>
  );
}
