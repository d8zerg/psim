import {
  switchRecipe,
  emptyRecipe,
  cardRecipe,
  cardHeaderRecipe,
  cardDescriptionRecipe,
  eyebrowRecipe,
  fieldRecipe,
  labelRecipe,
  requiredRecipe,
  helpRecipe,
  inputRecipe,
  badgeRecipe,
  noticeRecipe,
  noticeIconRecipe,
  noticeBodyRecipe,
  tableContainerRecipe,
} from "./recipes";
import { useId, useState, type ComponentProps, type ReactNode, type Ref } from "react";
import { cn } from "../cn";

export type Tone = "neutral" | "info" | "success" | "warning" | "danger";
type HeadingLevel = 2 | 3 | 4;

/** Status label: text carries the meaning, the tone repeats it. Solid emphasis is reserved for critical markers. */
export function Badge({
  tone = "neutral",
  emphasis = "subtle",
  className,
  children,
}: {
  readonly tone?: Tone | undefined;
  readonly emphasis?: "subtle" | "solid" | undefined;
  readonly className?: string | undefined;
  readonly children: ReactNode;
}) {
  return (
    <span className={cn(badgeRecipe({ emphasis }), className)} data-tone={tone} data-emphasis={emphasis}>
      {children}
    </span>
  );
}

export function Card({
  title,
  eyebrow,
  description,
  actions,
  headingLevel = 2,
  id,
  className,
  children,
}: {
  readonly title: ReactNode;
  readonly eyebrow?: ReactNode | undefined;
  readonly description?: ReactNode | undefined;
  readonly actions?: ReactNode | undefined;
  readonly headingLevel?: HeadingLevel | undefined;
  readonly id?: string | undefined;
  readonly className?: string | undefined;
  readonly children: ReactNode;
}) {
  const generated = useId();
  const headingId = `${id ?? generated}-title`;
  const Heading = `h${String(headingLevel)}` as "h2" | "h3" | "h4";
  return (
    <section id={id} aria-labelledby={headingId} className={cn(cardRecipe(), className)}>
      <div className={cardHeaderRecipe()}>
        <div className="min-w-0">
          {eyebrow && <p className={eyebrowRecipe()}>{eyebrow}</p>}
          <Heading id={headingId}>{title}</Heading>
          {description && <p className={cardDescriptionRecipe()}>{description}</p>}
        </div>
        {actions}
      </div>
      {children}
    </section>
  );
}

const icons: Record<Tone, string> = {
  neutral: "M12 8h.01M11 12h1v4h1",
  info: "M12 8h.01M11 12h1v4h1",
  success: "M8 12.5l2.5 2.5L16 9.5",
  warning: "M12 8v4.5M12 16h.01",
  danger: "M12 8v4.5M12 16h.01",
};

function ToneIcon({ tone }: { readonly tone: Tone }) {
  return (
    <svg className={noticeIconRecipe()} viewBox="0 0 24 24" fill="none" stroke="currentColor" aria-hidden="true">
      {tone === "warning" ? (
        <path d="M10.3 3.9 2.4 18a2 2 0 0 0 1.7 3h15.8a2 2 0 0 0 1.7-3L13.7 3.9a2 2 0 0 0-3.4 0Z" strokeWidth="1.8" />
      ) : (
        <circle cx="12" cy="12" r="9" strokeWidth="1.8" />
      )}
      <path d={icons[tone]} strokeWidth="2" strokeLinecap="round" strokeLinejoin="round" />
    </svg>
  );
}

/**
 * Inline message. Danger notices are announced as alerts and others as status updates;
 * `live={false}` keeps permanent page content out of the live regions.
 */
export function Notice({
  tone = "info",
  title,
  children,
  action,
  live = true,
}: {
  readonly tone?: Tone | undefined;
  readonly title?: ReactNode | undefined;
  readonly children?: ReactNode | undefined;
  readonly action?: ReactNode | undefined;
  readonly live?: boolean | undefined;
}) {
  return (
    <div className={noticeRecipe()} data-tone={tone} role={live ? (tone === "danger" ? "alert" : "status") : undefined}>
      <ToneIcon tone={tone} />
      <div className={noticeBodyRecipe()}>
        {title && <p className="ds-notice-title">{title}</p>}
        {children && <div>{children}</div>}
      </div>
      {action && <div className="ds-notice-action">{action}</div>}
    </div>
  );
}

interface FieldProps {
  readonly label: string;
  readonly hint?: string | undefined;
  readonly error?: string | undefined;
}

function FieldFrame({
  id,
  label,
  hint,
  error,
  required,
  className,
  children,
}: FieldProps & {
  readonly id: string;
  readonly required?: boolean | undefined;
  readonly className?: string | undefined;
  readonly children: ReactNode;
}) {
  const message = error ?? hint;
  return (
    <div className={cn(fieldRecipe(), className)}>
      <label htmlFor={id} className={labelRecipe()}>
        {label}
        {required && (
          <span className={requiredRecipe()} aria-hidden="true">
            *
          </span>
        )}
      </label>
      {children}
      {message && (
        <p id={`${id}-help`} className={helpRecipe()} data-error={error ? true : undefined}>
          {message}
        </p>
      )}
    </div>
  );
}

function controlProps(id: string, { hint, error }: Omit<FieldProps, "label">, describedBy: string | undefined) {
  const help = (error ?? hint) ? `${id}-help` : undefined;
  return {
    id,
    "aria-invalid": error ? true : undefined,
    "aria-describedby": [describedBy, help].filter(Boolean).join(" ") || undefined,
  } as const;
}

/** Text input with an associated label, hint and error. `className` applies to the field wrapper. */
export function Field({
  label,
  hint,
  error,
  id,
  className,
  "aria-describedby": describedBy,
  ...props
}: ComponentProps<"input"> & FieldProps) {
  const generated = useId();
  const inputId = id ?? generated;
  return (
    <FieldFrame id={inputId} label={label} hint={hint} error={error} required={props.required} className={className}>
      <input {...props} {...controlProps(inputId, { hint, error }, describedBy)} className={inputRecipe()} />
    </FieldFrame>
  );
}

/** Native select for single choice: accessible on every platform and keyboard friendly. */
export function SelectField({
  label,
  hint,
  error,
  id,
  className,
  children,
  "aria-describedby": describedBy,
  ...props
}: ComponentProps<"select"> & FieldProps) {
  const generated = useId();
  const selectId = id ?? generated;
  return (
    <FieldFrame id={selectId} label={label} hint={hint} error={error} required={props.required} className={className}>
      <select {...props} {...controlProps(selectId, { hint, error }, describedBy)} className={inputRecipe()}>
        {children}
      </select>
    </FieldFrame>
  );
}

/** On/off preference that applies immediately. */
export function Switch({
  label,
  checked,
  defaultChecked,
  onCheckedChange,
  disabled,
  className,
  ref,
}: {
  readonly label: string;
  readonly checked?: boolean;
  readonly defaultChecked?: boolean;
  readonly className?: string;
  readonly ref?: Ref<HTMLInputElement>;
  readonly onCheckedChange?: (checked: boolean) => void;
  readonly disabled?: boolean | undefined;
}) {
  const [internal, setInternal] = useState(defaultChecked ?? false);
  const value = checked ?? internal;
  return (
    <label className={cn(switchRecipe(), className)}>
      <input
        ref={ref}
        type="checkbox"
        role="switch"
        checked={value}
        data-state={value ? "checked" : "unchecked"}
        disabled={disabled}
        onChange={(event) => {
          setInternal(event.target.checked);
          onCheckedChange?.(event.target.checked);
        }}
      />
      {label}
    </label>
  );
}

/**
 * Data area with stable layout: a placeholder only before the first result, a status line while
 * refreshing, and the last result kept and marked as stale after an error.
 */
export function DataRegion({
  loading,
  hasData,
  error,
  labels,
  children,
}: {
  readonly loading: boolean;
  readonly hasData: boolean;
  readonly error?: string | undefined;
  readonly labels: { readonly loading: string; readonly refreshing: string; readonly stale: string };
  readonly children: ReactNode;
}) {
  const state = loading ? "loading" : error ? "stale" : "current";
  return (
    <div>
      <div className="ds-region-status-slot">
        {(["loading", "refreshing", "stale"] as const).map((key) => (
          <div key={key} className="ds-region-status ds-region-reserve" data-label={labels[key]} aria-hidden="true" />
        ))}
        <div className="ds-region-status" role="status" aria-live="polite" data-state={state}>
          {loading ? (hasData ? labels.refreshing : labels.loading) : error ? labels.stale : ""}
        </div>
      </div>
      <div aria-busy={loading}>
        {hasData || !loading ? children : <div className="ds-skeleton" aria-hidden="true" />}
      </div>
      {error && (
        <div className="ds-region-error">
          <Notice tone="danger">{error}</Notice>
        </div>
      )}
    </div>
  );
}

/**
 * Scroll container of a table. On narrow screens it scrolls horizontally, so it is a named region
 * that receives keyboard focus and can be scrolled with arrow keys.
 */
export function TableContainer({
  label,
  className,
  children,
  ...props
}: ComponentProps<"div"> & { readonly label: string }) {
  return (
    // eslint-disable-next-line jsx-a11y/no-noninteractive-tabindex -- a scrollable region must take keyboard focus
    <div {...props} role="region" aria-label={label} tabIndex={0} className={cn(tableContainerRecipe(), className)}>
      {children}
    </div>
  );
}

export function EmptyState({
  title,
  description,
  action,
  headingLevel = 3,
}: {
  readonly title: string;
  readonly description: string;
  readonly action?: ReactNode | undefined;
  readonly headingLevel?: HeadingLevel | undefined;
}) {
  const Heading = `h${String(headingLevel)}` as "h2" | "h3" | "h4";
  return (
    <div className={emptyRecipe()}>
      <Heading>{title}</Heading>
      <p className="max-w-lg text-sm text-muted-foreground">{description}</p>
      {action && <div className="mt-2">{action}</div>}
    </div>
  );
}
