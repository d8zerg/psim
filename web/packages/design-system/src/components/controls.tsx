import { inputRecipe, fieldRecipe } from "./recipes";
import { useId, type ComponentProps, type ReactNode } from "react";
import { cva } from "class-variance-authority";
import type { LucideIcon } from "lucide-react";
import { Button, type NativeButtonProps } from "./button";
import { Icon } from "./icons";
import { cn } from "../cn";

const checkboxInput = cva("size-4 accent-primary");
const sliderInput = cva("min-h-[var(--control-height)] w-full accent-primary");
const control = cva("grid gap-1 text-sm", { variants: { invalid: { true: "text-danger", false: "text-foreground" } } });
interface Labelled {
  readonly label: string;
  readonly error?: string;
  readonly hint?: string;
}
/** Text area forwards ref and native props; description and validation stay associated. */
export function Textarea({
  label,
  error,
  hint,
  id,
  className,
  "aria-describedby": describedBy,
  ...props
}: ComponentProps<"textarea"> & Labelled) {
  const generated = useId();
  const inputId = id ?? generated;
  return (
    <div className={cn(control({ invalid: Boolean(error) }), className)}>
      <label htmlFor={inputId}>{label}</label>
      <textarea
        {...props}
        id={inputId}
        className={cn(inputRecipe(), "min-h-24 py-2")}
        aria-invalid={error ? true : undefined}
        aria-describedby={
          [describedBy, (error ?? hint) ? `${inputId}-help` : undefined].filter(Boolean).join(" ") || undefined
        }
      />
      {(error ?? hint) && <p id={`${inputId}-help`}>{error ?? hint}</p>}
    </div>
  );
}
export function Checkbox({
  label,
  className,
  ...props
}: Omit<ComponentProps<"input">, "type"> & { readonly label: string }) {
  return (
    <label className="inline-flex min-h-[var(--control-height)] items-center gap-2">
      <input {...props} type="checkbox" className={checkboxInput({ className })} />
      {label}
    </label>
  );
}
export function RadioGroup({
  label,
  options,
  name,
  value,
  defaultValue,
  onValueChange,
  disabled,
  ref,
  className,
}: {
  readonly ref?: ComponentProps<"fieldset">["ref"];
  readonly className?: string;
  readonly label: string;
  readonly options: readonly { readonly value: string; readonly label: string }[];
  readonly name?: string;
  readonly value?: string;
  readonly defaultValue?: string;
  readonly onValueChange?: (value: string) => void;
  readonly disabled?: boolean;
}) {
  const generated = useId();
  return (
    <fieldset ref={ref} disabled={disabled} className={control({ className })}>
      <legend>{label}</legend>
      {options.map((option) => (
        <label key={option.value} className="inline-flex min-h-[var(--control-height)] items-center gap-2">
          <input
            type="radio"
            name={name ?? generated}
            value={option.value}
            checked={value === undefined ? undefined : value === option.value}
            defaultChecked={value === undefined ? defaultValue === option.value : undefined}
            onChange={() => onValueChange?.(option.value)}
          />
          {option.label}
        </label>
      ))}
    </fieldset>
  );
}
export function NumberInput({
  label,
  className,
  ...props
}: Omit<ComponentProps<"input">, "type"> & { readonly label: string }) {
  const generated = useId();
  return (
    <div className={fieldRecipe()}>
      <label htmlFor={props.id ?? generated}>{label}</label>
      <input {...props} id={props.id ?? generated} type="number" className={inputRecipe({ className })} />
    </div>
  );
}
export function Slider({
  label,
  className,
  ...props
}: Omit<ComponentProps<"input">, "type"> & { readonly label: string }) {
  const generated = useId();
  return (
    <div className={fieldRecipe()}>
      <label htmlFor={props.id ?? generated}>{label}</label>
      <input {...props} id={props.id ?? generated} type="range" className={sliderInput({ className })} />
    </div>
  );
}
export function IconButton({
  label,
  icon,
  ...props
}: Omit<NativeButtonProps, "children" | "asChild"> & { readonly label: string; readonly icon: LucideIcon }) {
  return (
    <Button {...props} aria-label={label} title={label}>
      <Icon icon={icon} decorative />
    </Button>
  );
}
export function ButtonGroup({ label, children }: { readonly label: string; readonly children: ReactNode }) {
  return (
    <div role="group" aria-label={label} className="inline-flex flex-wrap gap-1">
      {children}
    </div>
  );
}
export function Progress({
  label,
  value,
  max = 100,
}: {
  readonly label: string;
  readonly value?: number | undefined;
  readonly max?: number;
}) {
  return (
    <label className={fieldRecipe()}>
      {label}
      <progress aria-label={label} value={value} max={max} className="w-full accent-primary" />
    </label>
  );
}
export function Spinner({ label }: { readonly label: string }) {
  return (
    <span role="status" className="inline-flex items-center gap-2">
      <span
        aria-hidden="true"
        className="inline-block size-4 animate-spin rounded-full border-2 border-input border-t-foreground"
      />
      {label}
    </span>
  );
}
export function Skeleton({
  variant = "rows",
  label,
}: {
  readonly variant?: "rows" | "card" | "text";
  readonly label: string;
}) {
  return (
    <div
      role="status"
      aria-label={label}
      data-state="loading"
      className={
        variant === "rows"
          ? "ds-skeleton"
          : variant === "card"
            ? "min-h-40 rounded-lg bg-muted"
            : "h-4 rounded-sm bg-muted"
      }
    />
  );
}
export function Kbd({ children }: { readonly children: ReactNode }) {
  return <kbd className="rounded-sm border border-input bg-muted px-1 text-sm">{children}</kbd>;
}
export function Tag({ children }: { readonly children: ReactNode }) {
  return <span className="rounded-sm border border-border bg-muted px-2 text-sm">{children}</span>;
}
export function Avatar({ name }: { readonly name: string }) {
  return (
    <span
      aria-label={name}
      role="img"
      className="inline-grid size-8 place-items-center rounded-full border border-border bg-muted"
    >
      {name
        .trim()
        .split(/\s+/u)
        .map((part) => part[0])
        .slice(0, 2)
        .join("")}
    </span>
  );
}
export function DescriptionList({
  items,
}: {
  readonly items: readonly { readonly label: string; readonly value: ReactNode }[];
}) {
  return (
    <dl className="grid gap-3">
      {items.map((item) => (
        <div key={item.label}>
          <dt className="text-sm text-muted-foreground">{item.label}</dt>
          <dd>{item.value}</dd>
        </div>
      ))}
    </dl>
  );
}
export function Stat({
  label,
  value,
  description,
}: {
  readonly label: string;
  readonly value: ReactNode;
  readonly description?: string;
}) {
  return (
    <dl>
      <dt className="text-sm text-muted-foreground">{label}</dt>
      <dd className="text-2xl tabular-nums">{value}</dd>
      {description && <dd className="text-sm text-muted-foreground">{description}</dd>}
    </dl>
  );
}
export function Timeline({
  label,
  items,
}: {
  readonly label: string;
  readonly items: readonly { readonly id: string; readonly time: string; readonly text: string }[];
}) {
  return (
    <ol aria-label={label} className="grid gap-3">
      {items.map((item) => (
        <li key={item.id}>
          <span className="font-mono text-sm text-muted-foreground">{item.time}</span> {item.text}
        </li>
      ))}
    </ol>
  );
}
export function Stepper({
  label,
  steps,
  current,
}: {
  readonly label: string;
  readonly steps: readonly string[];
  readonly current: number;
}) {
  return (
    <ol aria-label={label} className="grid gap-3">
      {steps.map((step, index) => (
        <li
          key={step}
          aria-current={index === current ? "step" : undefined}
          data-state={index < current ? "complete" : index === current ? "current" : "pending"}
        >
          {index + 1}. {step}
        </li>
      ))}
    </ol>
  );
}
