import { useRef, useState } from "react";
import { Button, Notice } from "@psim/design-system";
import { ApiProblemError } from "@psim/api-client";
import { status, type AdminApi, type Write } from "./api";
import { record, list, string, type Value, type RecordValue } from "./model";
import { enumLabel, label, useAdminStrings } from "./strings";
export function useWrites(api: AdminApi, denied: () => void) {
  const busy = useRef(false);
  const [pending, setPending] = useState(false);
  const [error, setError] = useState<"" | "conflict" | "failed" | "unknown">("");
  const [problem, setProblem] = useState<string[]>([]);
  const [uncertain, setUncertain] = useState<{ request: Write; done: (result: RecordValue) => void }>();
  const [result, setResult] = useState<RecordValue>();
  const perform = async (request: Write, done: (result: RecordValue) => void = setResult, exact = false) => {
    if (busy.current || (uncertain && !exact)) return;
    busy.current = true;
    setPending(true);
    setError("");
    setProblem([]);
    setResult(undefined);
    try {
      const body = await api.send(request);
      setUncertain(undefined);
      done(body);
    } catch (error) {
      const code = status(error);
      if (code === 401 || code === 403) denied();
      else if (code === 409 || code === 412) {
        setError("conflict");
        setUncertain(undefined);
      } else if (code === 0 || code >= 500) {
        setError("unknown");
        setUncertain({ request, done });
      } else {
        setError("failed");
        setUncertain(undefined);
      }
      if (error instanceof ApiProblemError)
        setProblem(
          error.problem.errors?.map((item) => `${item.field ?? ""}: ${item.message ?? ""}`) ?? [error.problem.code],
        );
    } finally {
      busy.current = false;
      setPending(false);
    }
  };
  return { pending, error, problem, uncertain, result, perform, setResult };
}
export type Writes = ReturnType<typeof useWrites>;
export function WriteNotice({ actions, writable }: { readonly actions: Writes; readonly writable: boolean }) {
  const { t } = useAdminStrings();
  return (
    <>
      {actions.error && (
        <Notice tone="warning">
          <p>{t[actions.error]}</p>
          {actions.problem.map((text) => (
            <p key={text}>{text}</p>
          ))}
          {actions.uncertain && (
            <Button
              disabled={!writable || actions.pending}
              onClick={() => {
                const last = actions.uncertain;
                if (last) void actions.perform(last.request, last.done, true);
              }}
            >
              {t.retry}
            </Button>
          )}
        </Notice>
      )}
      {actions.result && <Result value={actions.result} />}
    </>
  );
}
function ResultFields({ value }: { readonly value: Value }) {
  const { t, language } = useAdminStrings();
  if (Array.isArray(value))
    return value.length ? (
      <ul>
        {value.map((item, index) => (
          <li key={index}>
            <ResultFields value={item} />
          </li>
        ))}
      </ul>
    ) : (
      <span>{t.empty}</span>
    );
  if (typeof value === "object" && value !== null)
    return (
      <dl className="admin-result-fields">
        {Object.entries(value).map(([key, item]) => (
          <div key={key}>
            <dt>{label(key, language)}</dt>
            <dd data-field={key}>
              <ResultFields value={item} />
            </dd>
          </div>
        ))}
      </dl>
    );
  return <span>{typeof value === "boolean" ? (value ? t.yes : t.no) : enumLabel(string(value) || "-", language)}</span>;
}
export function Result({ value }: { readonly value: RecordValue }) {
  const { t } = useAdminStrings();
  let summary = value;
  if (value.ruleSet)
    summary = { version: record(value.ruleSet).version ?? "0", ruleCount: list(record(value.ruleSet).rules).length };
  else if (value.dryRun)
    summary = Object.fromEntries(
      Object.entries(record(value.dryRun)).filter(([key]) =>
        ["state", "eventsProcessed", "signalsProduced", "failure"].includes(key),
      ),
    );
  else if (value.importJob)
    summary = Object.fromEntries(
      Object.entries(record(value.importJob)).filter(([key]) =>
        ["state", "rowsTotal", "rowsCreated", "rowsUpdated", "rowsFailed", "errors"].includes(key),
      ),
    );
  else if (typeof value.valid === "boolean")
    summary = { status: value.valid ? t.valid : t.invalid, violations: value.violations ?? [] };
  return (
    <div className="admin-result" data-testid="admin-result">
      <h3>{t.result}</h3>
      <ResultFields value={summary} />
    </div>
  );
}
