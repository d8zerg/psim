import { inputRecipe } from "./recipes";
import { useEffect, useId, useRef, useState, type ComponentProps } from "react";
import { useVirtualizer } from "@tanstack/react-virtual";
import { useForm } from "react-hook-form";
import { zodResolver } from "@hookform/resolvers/zod";
import { z } from "zod";
import { Button } from "./button";
import { Field, SelectField } from "./patterns";
import { Progress } from "./controls";
import { cn } from "../cn";

export interface Choice {
  readonly value: string;
  readonly label: string;
}
interface ComboProps {
  readonly label: string;
  readonly options?: readonly Choice[];
  readonly loadOptions?: (query: string, signal: AbortSignal) => Promise<readonly Choice[]>;
  readonly labels: { readonly loading: string; readonly empty: string; readonly error: string };
  readonly value?: string;
  readonly defaultValue?: string;
  readonly onValueChange?: (value: string) => void;
  readonly disabled?: boolean;
  readonly className?: string;
  readonly ref?: ComponentProps<"input">["ref"];
}
/** Editable single choice. The input retains DOM focus while an active option moves in a virtual list. */
export function Combobox({
  label,
  options = [],
  loadOptions,
  labels,
  value,
  defaultValue = "",
  onValueChange,
  disabled,
  className,
  ref,
}: ComboProps) {
  const id = useId();
  const [internal, setInternal] = useState(defaultValue);
  const selected = value ?? internal;
  const [query, setQuery] = useState("");
  const [open, setOpen] = useState(false);
  const [active, setActive] = useState(0);
  const [remote, setRemote] = useState<readonly Choice[]>([]);
  const [status, setStatus] = useState<"ready" | "loading" | "error">("ready");
  const scroll = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (!loadOptions || !open) return;
    const abort = new AbortController();
    setStatus("loading");
    void loadOptions(query, abort.signal).then(
      (result) => {
        if (!abort.signal.aborted) {
          setRemote(result);
          setStatus("ready");
        }
      },
      () => {
        if (!abort.signal.aborted) setStatus("error");
      },
    );
    return () => {
      abort.abort();
    };
  }, [query, loadOptions, open]);
  const choices = loadOptions
    ? remote
    : options.filter((option) => option.label.toLocaleLowerCase().includes(query.toLocaleLowerCase()));
  // eslint-disable-next-line react-hooks/incompatible-library -- this component is not compiled or memoized by React Compiler
  const virtual = useVirtualizer({
    count: choices.length,
    getScrollElement: () => scroll.current,
    estimateSize: () => 48,
    overscan: 3,
    initialRect: { width: 320, height: 240 },
  });
  useEffect(() => {
    virtual.scrollToOffset(0);
  }, [query, virtual]);
  function choose(option: Choice) {
    setInternal(option.value);
    onValueChange?.(option.value);
    setQuery("");
    setOpen(false);
  }
  function move(index: number) {
    const next = Math.max(0, Math.min(choices.length - 1, index));
    setActive(next);
    virtual.scrollToIndex(next);
  }
  const chosen = [...options, ...remote].find((option) => option.value === selected);
  return (
    <div className={cn("relative grid gap-1", className)}>
      <label htmlFor={id}>{label}</label>
      <input
        ref={ref}
        id={id}
        role="combobox"
        className={inputRecipe()}
        disabled={disabled}
        value={open ? query : (chosen?.label ?? selected)}
        aria-expanded={open}
        aria-controls={`${id}-list`}
        aria-autocomplete="list"
        aria-activedescendant={
          open && virtual.getVirtualItems().some((item) => item.index === active)
            ? `${id}-option-${String(active)}`
            : undefined
        }
        onFocus={() => {
          setOpen(true);
        }}
        onBlur={() => {
          setOpen(false);
        }}
        onChange={(event) => {
          setQuery(event.target.value);
          setActive(0);
          setOpen(true);
        }}
        onKeyDown={(event) => {
          if (event.key === "Escape") {
            event.preventDefault();
            setOpen(false);
          }
          if (event.key === "ArrowDown" || event.key === "ArrowUp") {
            event.preventDefault();
            setOpen(true);
            move(active + (event.key === "ArrowDown" ? 1 : -1));
          }
          if (open && (event.key === "Home" || event.key === "End")) {
            event.preventDefault();
            move(event.key === "Home" ? 0 : choices.length - 1);
          }
          if (open && event.key === "Enter") {
            event.preventDefault();
            const option = choices[active];
            if (option) choose(option);
          }
        }}
      />
      {open && (
        <div className="absolute top-full z-[var(--z-overlay)] w-full rounded border border-input bg-card p-1">
          <p role="status">
            {status === "loading"
              ? labels.loading
              : status === "error"
                ? labels.error
                : !choices.length
                  ? labels.empty
                  : ""}
          </p>
          <div
            ref={scroll}
            role="listbox"
            id={`${id}-list`}
            aria-label={label}
            aria-busy={status === "loading"}
            className="max-h-60 overflow-auto"
          >
            <div role="presentation" style={{ height: virtual.getTotalSize(), position: "relative" }}>
              {virtual.getVirtualItems().map((item) => {
                const option = choices[item.index];
                return option ? (
                  <div
                    key={option.value}
                    role="option"
                    tabIndex={-1}
                    id={`${id}-option-${String(item.index)}`}
                    aria-selected={option.value === selected}
                    aria-posinset={item.index + 1}
                    aria-setsize={choices.length}
                    data-active={active === item.index}
                    className="absolute top-0 left-0 flex w-full items-center px-3 data-[active=true]:bg-selected"
                    style={{ height: item.size, transform: `translateY(${String(item.start)}px)` }}
                    onMouseDown={(event) => {
                      event.preventDefault();
                      choose(option);
                    }}
                  >
                    {option.label}
                  </div>
                ) : null;
              })}
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
/** Multiple native checkboxes retain browser keyboard behavior and expose every selected item. */
export function MultiSelect({
  label,
  options,
  value,
  defaultValue = [],
  onValueChange,
  disabled,
  className,
}: {
  readonly label: string;
  readonly options: readonly Choice[];
  readonly value?: readonly string[];
  readonly defaultValue?: readonly string[];
  readonly onValueChange?: (values: readonly string[]) => void;
  readonly disabled?: boolean;
  readonly className?: string;
}) {
  const [internal, setInternal] = useState(defaultValue);
  const selected = value ?? internal;
  return (
    <fieldset disabled={disabled} className={cn("grid max-h-60 gap-2 overflow-auto", className)}>
      <legend>{label}</legend>
      {options.map((option) => (
        <label key={option.value} className="flex min-h-[var(--control-height)] items-center gap-2">
          <input
            type="checkbox"
            checked={selected.includes(option.value)}
            onChange={(event) => {
              const next = event.target.checked
                ? [...selected, option.value]
                : selected.filter((item) => item !== option.value);
              setInternal(next);
              onValueChange?.(next);
            }}
          />
          {option.label}
        </label>
      ))}
    </fieldset>
  );
}
export interface DatePeriod {
  readonly start: string;
  readonly end: string;
  readonly timeZone: string;
}
/** Native local date/time entry with an explicit IANA zone. Values are wall times, never silently converted to UTC. */
export function DatePeriodPicker({
  labels,
  value,
  onValueChange,
  zones,
}: {
  readonly labels: {
    readonly start: string;
    readonly end: string;
    readonly timeZone: string;
    readonly invalid: string;
  };
  readonly value: DatePeriod;
  readonly onValueChange: (value: DatePeriod) => void;
  readonly zones: readonly string[];
}) {
  const error = value.end && value.end < value.start ? labels.invalid : undefined;
  return (
    <div className="grid gap-3">
      <Field
        type="datetime-local"
        label={labels.start}
        value={value.start}
        onChange={(event) => {
          onValueChange({ ...value, start: event.target.value });
        }}
      />
      <Field
        type="datetime-local"
        label={labels.end}
        min={value.start}
        value={value.end}
        error={error}
        onChange={(event) => {
          onValueChange({ ...value, end: event.target.value });
        }}
      />
      <SelectField
        label={labels.timeZone}
        value={value.timeZone}
        onChange={(event) => {
          onValueChange({ ...value, timeZone: event.target.value });
        }}
      >
        {zones.map((zone) => (
          <option key={zone}>{zone}</option>
        ))}
      </SelectField>
    </div>
  );
}
export interface RowIssue {
  readonly row: number;
  readonly message: string;
}
/** Upload transport is injected. Cancellation invalidates progress and completion from the old request. */
export function FileUpload({
  label,
  cancelLabel,
  errorLabel,
  reportLabel,
  upload,
}: {
  readonly label: string;
  readonly cancelLabel: string;
  readonly errorLabel: string;
  readonly reportLabel: string;
  readonly upload: (file: File, progress: (value: number) => void, signal: AbortSignal) => Promise<readonly RowIssue[]>;
}) {
  const [progress, setProgress] = useState(0);
  const [issues, setIssues] = useState<readonly RowIssue[]>([]);
  const [error, setError] = useState(false);
  const [busy, setBusy] = useState(false);
  const request = useRef<AbortController | null>(null);
  useEffect(
    () => () => {
      request.current?.abort();
    },
    [],
  );
  async function start(file: File) {
    request.current?.abort();
    const controller = new AbortController();
    request.current = controller;
    setBusy(true);
    setProgress(0);
    setIssues([]);
    setError(false);
    try {
      const result = await upload(
        file,
        (value) => {
          if (!controller.signal.aborted) setProgress(Math.max(0, Math.min(100, value)));
        },
        controller.signal,
      );
      if (!controller.signal.aborted) {
        setIssues(result);
        setProgress(100);
      }
    } catch {
      if (!controller.signal.aborted) setError(true);
    } finally {
      if (!controller.signal.aborted) setBusy(false);
    }
  }
  return (
    <div className="grid gap-2">
      <Field
        label={label}
        type="file"
        onChange={(event) => {
          const file = event.target.files?.[0];
          if (file) void start(file);
        }}
      />
      <Progress label={label} value={progress} />
      {busy && (
        <Button
          variant="secondary"
          onClick={() => {
            request.current?.abort();
            setBusy(false);
          }}
        >
          {cancelLabel}
        </Button>
      )}
      {error && <p role="alert">{errorLabel}</p>}
      <table className="w-full">
        <caption>{reportLabel}</caption>
        <tbody>
          {issues.map((issue) => (
            <tr key={`${String(issue.row)}-${issue.message}`}>
              <th scope="row">{issue.row}</th>
              <td>{issue.message}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </div>
  );
}
export interface ValidatedField {
  readonly name: string;
  readonly label: string;
  readonly requiredMessage: string;
  readonly minLength?: number;
}
/** Flat string form with a Zod schema, an error summary and focus on the first invalid control. */
export function ValidatedForm({
  fields,
  submitLabel,
  errorLabel,
  onSubmit,
}: {
  readonly fields: readonly ValidatedField[];
  readonly submitLabel: string;
  readonly errorLabel: string;
  readonly onSubmit: (value: Record<string, string>) => Promise<void> | void;
}) {
  const shape = Object.fromEntries(
    fields.map((field) => [
      field.name,
      z
        .string()
        .trim()
        .min(field.minLength ?? 1, field.requiredMessage),
    ]),
  );
  const {
    register,
    handleSubmit,
    setFocus,
    formState: { errors, isSubmitting },
  } = useForm<Record<string, string>>({ resolver: zodResolver(z.object(shape)), shouldFocusError: true });
  const [submitError, setSubmitError] = useState(false);
  const id = useId();
  return (
    <form
      noValidate
      onSubmit={(event) => {
        void handleSubmit(async (value) => {
          setSubmitError(false);
          try {
            await onSubmit(value);
          } catch {
            setSubmitError(true);
          }
        })(event);
      }}
      className="grid gap-3"
    >
      {Object.keys(errors).length > 0 && (
        <div role="alert">
          <h3>{errorLabel}</h3>
          <ul>
            {fields
              .filter((field) => errors[field.name])
              .map((field) => (
                <li key={field.name}>
                  <a
                    href={`#${id}-${field.name}`}
                    onClick={(event) => {
                      event.preventDefault();
                      setFocus(field.name);
                    }}
                  >
                    {field.label}: {errors[field.name]?.message}
                  </a>
                </li>
              ))}
          </ul>
        </div>
      )}
      {submitError && <p role="alert">{errorLabel}</p>}
      {fields.map((field) => (
        <Field
          key={field.name}
          id={`${id}-${field.name}`}
          label={field.label}
          required
          error={errors[field.name]?.message}
          {...register(field.name)}
        />
      ))}
      <Button type="submit" loading={isSubmitting}>
        {submitLabel}
      </Button>
    </form>
  );
}
