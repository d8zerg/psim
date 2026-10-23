import { Slot } from "@radix-ui/react-slot";
import { cva, type VariantProps } from "class-variance-authority";
import type { ButtonHTMLAttributes, MouseEventHandler, Ref } from "react";

import { cn } from "../cn";

// Heights follow the density tokens, so sizes stay proportional in comfortable and compact modes.
export const buttonVariants = cva(
  "ds-button relative inline-flex shrink-0 items-center justify-center gap-2 whitespace-nowrap rounded-md border " +
    "font-medium transition-colors focus-visible:outline-2 focus-visible:outline-offset-2 " +
    "focus-visible:outline-ring disabled:pointer-events-none disabled:opacity-50",
  {
    variants: {
      variant: {
        primary:
          "border-[var(--button-border)] bg-[var(--button-background)] text-[var(--button-foreground)] hover:bg-primary/85",
        secondary: "border-input bg-card text-foreground hover:bg-muted",
        danger: "border-danger-solid bg-danger-solid text-danger-solid-foreground hover:bg-danger-solid/85",
        ghost: "border-transparent text-foreground hover:bg-muted",
      },
      size: {
        sm: "min-h-[calc(var(--control-height)-0.5rem)] px-2.5 text-[0.8125rem]",
        md: "min-h-[var(--control-height)] px-3.5 text-sm",
        lg: "min-h-[calc(var(--control-height)+0.5rem)] px-5 text-[0.9375rem]",
      },
    },
    defaultVariants: { variant: "primary", size: "md" },
  },
);

interface ButtonBaseProps
  extends Omit<ButtonHTMLAttributes<HTMLButtonElement>, "onClickCapture">, VariantProps<typeof buttonVariants> {
  readonly onClickCapture?: MouseEventHandler<HTMLElement>;
  /** Keep the action name and dimensions while preventing duplicate submission. */
  readonly loading?: boolean;
}
export type ButtonProps = ButtonBaseProps &
  (
    | { readonly asChild?: false; readonly ref?: Ref<HTMLButtonElement> }
    | { readonly asChild: true; readonly ref?: Ref<HTMLElement> }
  );

export type NativeButtonProps = Extract<ButtonProps, { readonly asChild?: false }>;

export function Button(allProps: ButtonProps) {
  const { className, variant, size, asChild, loading = false, type, ref, ...props } = allProps;
  const classes = cn(buttonVariants({ variant, size }), className);
  if (asChild) {
    return (
      <Slot
        className={classes}
        data-variant={variant ?? "primary"}
        {...props}
        ref={ref}
        aria-busy={loading || undefined}
        aria-disabled={loading || props.disabled ? true : undefined}
        onClickCapture={(event) => {
          if (loading || props.disabled) {
            event.preventDefault();
            event.stopPropagation();
            return;
          }
          props.onClickCapture?.(event);
        }}
      />
    );
  }
  return (
    <button
      type={type ?? "button"}
      className={classes}
      data-variant={variant ?? "primary"}
      {...props}
      ref={(node) => {
        if (typeof ref === "function") return ref(node);
        if (ref) ref.current = node;
      }}
      disabled={loading || props.disabled}
      aria-busy={loading || undefined}
    />
  );
}
