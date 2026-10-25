import { useEffect, useState } from "react";
import { useQuery, useQueryClient } from "@tanstack/react-query";
import { Button, Field, Notice, SelectField } from "@psim/design-system";
import { AdminFailure, status, write, saveRequest, type AdminApi } from "./api";
import { FormFields } from "./form";
import { WriteNotice, Result, useWrites } from "./actions";
import { editable, list, record, resources, string, type RecordValue, type ResourceKey, type Resource } from "./model";
import { ConfirmAction } from "@psim/design-system/confirm-action";
import { Textarea } from "@psim/design-system/controls";
import { enumLabel, useAdminStrings } from "./strings";
export interface Permissions {
  readonly writable: boolean;
  readonly global: boolean;
  readonly denied: () => void;
}
const metadata = [
  "version",
  "updatedAt",
  "createdAt",
  "createdBy",
  "publishedAt",
  "publishedBy",
  "activatedAt",
  "completedAt",
  "certificates",
  "floorPlan",
];
export function GuardButton({
  dirty,
  text,
  action,
  disabled = false,
}: {
  readonly dirty: boolean;
  readonly text: string;
  readonly action: () => void;
  readonly disabled?: boolean;
}) {
  const { t } = useAdminStrings();
  return dirty ? (
    <ConfirmAction
      trigger={text}
      title={t.discarded}
      description={t.discardHint}
      confirm={t.confirm}
      cancel={t.cancel}
      onConfirm={action}
      disabled={disabled}
    />
  ) : (
    <Button disabled={disabled} onClick={action}>
      {text}
    </Button>
  );
}
export function Editor({
  resourceKey,
  value,
  existing,
  api,
  permissions,
  onDirty,
  reload,
}: {
  readonly resourceKey: ResourceKey;
  readonly value: RecordValue;
  readonly existing: boolean;
  readonly api: AdminApi;
  readonly permissions: Permissions;
  readonly onDirty: (dirty: boolean) => void;
  readonly reload: () => void;
}) {
  const { t, language } = useAdminStrings();
  const resource: Resource = resources[resourceKey];
  const [saved, setSaved] = useState(existing ? value : undefined);
  const [draft, setDraft] = useState(() => editable(resourceKey, value));
  const [certificate, setCertificate] = useState("");
  const [reason, setReason] = useState("");
  const [checked, setChecked] = useState("");
  const [runId, setRunId] = useState("");
  const [range, setRange] = useState({ from: "", to: "", source: "DRY_RUN_SOURCE_KAFKA" });
  const actions = useWrites(api, permissions.denied);
  const client = useQueryClient();
  const baseline = editable(resourceKey, saved ?? value);
  const dirty = JSON.stringify(draft) !== JSON.stringify(baseline);
  const id = string(draft[resource.id]);
  const savedVersion = string(saved?.version);
  const writable = permissions.writable && (!resource.global || permissions.global);
  const ready = writable && !actions.pending && !actions.uncertain;
  const stored = !!saved && !dirty;
  const run = useQuery({
    queryKey: ["admin", "run", runId],
    queryFn: ({ signal }) => api.get(`/v1/dry-runs/${encodeURIComponent(runId)}`, signal),
    enabled: !!runId,
    retry: false,
    refetchInterval: (query) => (record(query.state.data?.dryRun).state === "DRY_RUN_STATE_RUNNING" ? 1000 : false),
  });
  useEffect(() => {
    if ([401, 403].includes(status(run.error))) permissions.denied();
  }, [run.error, permissions]);
  useEffect(() => {
    onDirty(dirty || actions.pending || !!actions.uncertain);
    return () => {
      onDirty(false);
    };
  }, [dirty, actions.pending, actions.uncertain, onDirty]);
  useEffect(() => {
    if (!dirty) return;
    const prevent = (event: BeforeUnloadEvent) => {
      event.preventDefault();
    };
    window.addEventListener("beforeunload", prevent);
    return () => {
      window.removeEventListener("beforeunload", prevent);
    };
  }, [dirty]);
  const refresh = () => {
    void client.invalidateQueries({ queryKey: ["admin", resourceKey] });
  };
  const accept = (response: RecordValue) => {
    const next = record(response[resource.item]);
    if (!next.version) throw new AdminFailure(502);
    setSaved(next);
    setDraft(editable(resourceKey, next));
    setCertificate("");
    setChecked("");
    actions.setResult({ status: t.saved, version: next.version });
    refresh();
  };
  const operation = (suffix: string, body: RecordValue = {}, done?: (result: RecordValue) => void) => {
    void actions.perform(
      write(`${resource.path}/${encodeURIComponent(id)}:${suffix}`, body, "POST", savedVersion),
      done ?? accept,
    );
  };
  const confirm = (text: string, action: () => void, disabled = false) => (
    <ConfirmAction
      trigger={text}
      title={text}
      description={t.confirmHint}
      details={
        <p>
          {string(saved?.name) || id} / {t.version}: {savedVersion}
        </p>
      }
      confirm={t.confirm}
      cancel={t.cancel}
      disabled={!ready || !stored || disabled}
      onConfirm={action}
    />
  );
  return (
    <section aria-label={t[resourceKey]} className="admin-editor">
      <h2>
        {t[resourceKey]}: {string(draft.name) || id}
      </h2>
      {saved && (
        <p>
          {t.version}: {savedVersion} {enumLabel(string(saved.state), language)}
        </p>
      )}
      {dirty && <p role="status">{t.dirty}</p>}
      {resource.global && !permissions.global && <Notice>{t.global}</Notice>}
      <WriteNotice actions={actions} writable={writable} />
      <form
        onSubmit={(event) => {
          event.preventDefault();
          if (ready) void actions.perform(saveRequest(resourceKey, draft, saved, certificate), accept);
        }}
      >
        <fieldset disabled={!ready || (resourceKey === "connectors" && !!saved)}>
          <FormFields
            node={{ ref: resource.schema }}
            value={draft}
            name={t[resourceKey]}
            change={(next) => {
              setDraft(record(next));
              setChecked("");
            }}
            omit={[...metadata, ...(resourceKey === "maintenance" ? ["state"] : [])]}
            locked={[
              ...(saved && resource.id && resourceKey !== "mappings" ? [resource.id] : []),
              ...(saved && resourceKey === "mappings" ? ["connectorType", "mappingRuleSetId"] : []),
              ...(resource.immutable ? ["state"] : []),
            ]}
          />
          {resourceKey === "connectors" && !saved && (
            <Textarea
              label={t.certificate}
              required
              value={certificate}
              onChange={(event) => {
                setCertificate(event.target.value);
              }}
            />
          )}
          <Button type="submit" disabled={!ready || (resourceKey === "connectors" && !!saved)}>
            {t.save}
          </Button>
        </fieldset>
      </form>
      <div className="admin-actions">
        <GuardButton dirty={dirty} text={t.reload} action={reload} disabled={actions.pending || !!actions.uncertain} />
      </div>
      {saved && (
        <div className="admin-actions">
          {(resourceKey === "rules" || resourceKey === "plans") && (
            <>
              <Button
                disabled={!ready || !stored}
                onClick={() => {
                  const request =
                    resourceKey === "rules"
                      ? write(
                          `/v1/rules/${encodeURIComponent(id)}:validate`,
                          { version: Number(savedVersion) },
                          "POST",
                          savedVersion,
                        )
                      : write("/v1/plans:validate", editable(resourceKey, saved));
                  void actions.perform(request, (result) => {
                    setChecked(result.valid === true ? savedVersion : "");
                    if (resourceKey === "rules" && result.valid === true && saved.state === "RULE_STATE_DRAFT") {
                      const next = { ...saved, state: "RULE_STATE_TESTING" };
                      setSaved(next);
                      setDraft(editable(resourceKey, next));
                    }
                    actions.setResult(result);
                    refresh();
                  });
                }}
              >
                {t.validate}
              </Button>
              {resourceKey === "plans" &&
                confirm(
                  t.publish,
                  () => {
                    operation("publish", { version: Number(savedVersion) });
                  },
                  checked !== savedVersion,
                )}
              {confirm(t.archive, () => {
                operation("archive", { version: Number(savedVersion) });
              })}
            </>
          )}
          {resourceKey === "locations" &&
            confirm(t.archive, () => {
              operation("archive");
            })}
          {resourceKey === "maintenance" && (
            <>
              <Field
                label={`${t.reason}: ${t.end}`}
                value={reason}
                onChange={(event) => {
                  setReason(event.target.value);
                }}
              />
              {confirm(
                t.end,
                () => {
                  operation("cancel", { reason });
                },
                !reason.trim(),
              )}
            </>
          )}
          {resourceKey === "connectors" && (
            <>
              <Textarea
                label={t.certificate}
                value={certificate}
                onChange={(event) => {
                  setCertificate(event.target.value);
                }}
              />
              {confirm(
                t.rotate,
                () => {
                  operation("rotate-certificate", { certificatePem: certificate });
                },
                !certificate.trim(),
              )}
              <Field
                label={`${t.reason}: ${t.revoke}`}
                value={reason}
                onChange={(event) => {
                  setReason(event.target.value);
                }}
              />
              {confirm(
                t.revoke,
                () => {
                  operation("revoke", { reason });
                },
                !reason.trim(),
              )}
              <Result value={{ certificates: saved.certificates ?? [], state: saved.state ?? "" }} />
            </>
          )}
        </div>
      )}
      {resourceKey === "rules" && saved && (
        <section className="admin-fields" aria-label={t.dryRun}>
          <h3>{t.dryRun}</h3>
          <Field
            label={t.from}
            value={range.from}
            onChange={(event) => {
              setRange({ ...range, from: event.target.value });
            }}
          />
          <Field
            label={t.to}
            value={range.to}
            onChange={(event) => {
              setRange({ ...range, to: event.target.value });
            }}
          />
          <SelectField
            label={t.source}
            value={range.source}
            onChange={(event) => {
              setRange({ ...range, source: event.target.value });
            }}
          >
            <option value="DRY_RUN_SOURCE_KAFKA">Kafka</option>
            <option value="DRY_RUN_SOURCE_CLICKHOUSE">ClickHouse</option>
          </SelectField>
          <Button
            disabled={
              !ready ||
              !stored ||
              checked !== savedVersion ||
              !Number.isFinite(Date.parse(range.from)) ||
              Date.parse(range.to) <= Date.parse(range.from) ||
              !Number.isFinite(Date.parse(range.to))
            }
            onClick={() => {
              operation("dry-run", { version: Number(savedVersion), ...range }, (response) => {
                setRunId(string(record(response.dryRun).dryRunId));
                actions.setResult(response);
              });
            }}
          >
            {t.dryRun}
          </Button>
          {run.data && !run.error && <Result value={run.data} />}
          {confirm(
            t.activate,
            () => {
              // Read the complete active set immediately before changing one member.
              void api
                .get("/v1/rule-sets/active")
                .then((response) => {
                  const active = record(response.ruleSet);
                  const rules = list(active.rules)
                    .map(record)
                    .filter((rule) => rule.ruleId !== id)
                    .map((rule) => ({ ruleId: rule.ruleId ?? "", version: rule.version ?? 0 }));
                  return actions.perform(
                    write(
                      "/v1/rule-sets:activate",
                      {
                        rules: [...rules, { ruleId: id, version: Number(savedVersion) }],
                        expectedRuleSetVersion: string(active.version) || "0",
                      },
                      "POST",
                      string(active.version) || "0",
                    ),
                    (result) => {
                      const activeRule = list(record(result.ruleSet).rules)
                        .map(record)
                        .find((rule) => rule.ruleId === id);
                      accept({ rule: activeRule ?? { ...saved, state: "RULE_STATE_ACTIVE" } });
                      actions.setResult(result);
                      refresh();
                    },
                  );
                })
                .catch((error: unknown) => {
                  if ([401, 403].includes(status(error))) permissions.denied();
                  else actions.setResult({ status: t.failed });
                });
            },
            checked !== savedVersion,
          )}
        </section>
      )}
    </section>
  );
}
