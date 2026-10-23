import { useState, type ReactNode, type ComponentProps } from "react";
import * as DialogPrimitive from "@radix-ui/react-dialog";
import * as PopoverPrimitive from "@radix-ui/react-popover";
import * as TooltipPrimitive from "@radix-ui/react-tooltip";
import * as MenuPrimitive from "@radix-ui/react-dropdown-menu";
import * as ContextPrimitive from "@radix-ui/react-context-menu";
import { cva } from "class-variance-authority";
import { Button } from "./button";
import { cn } from "../cn";

const surface = cva("rounded-lg border border-border bg-card p-4 text-foreground shadow-[var(--shadow-overlay)]", {
  variants: {
    placement: {
      dialog:
        "fixed top-1/2 left-1/2 z-[var(--z-dialog)] max-h-[85vh] w-[min(36rem,92vw)] -translate-x-1/2 -translate-y-1/2 overflow-auto",
      sheet: "fixed inset-y-0 right-0 z-[var(--z-dialog)] w-[min(30rem,94vw)] overflow-auto",
      popup:
        "z-[var(--z-dialog)] max-h-[var(--radix-popover-content-available-height,80vh)] max-w-[min(30rem,92vw)] overflow-auto",
    },
  },
});
interface DialogProps extends Pick<
  ComponentProps<typeof DialogPrimitive.Root>,
  "open" | "defaultOpen" | "onOpenChange"
> {
  readonly trigger: ReactNode;
  readonly title: string;
  readonly description: string;
  readonly closeLabel: string;
  readonly children: ReactNode;
  readonly className?: string;
}
/** Modal with focus trap, Escape dismissal and focus restoration. `className` styles the panel. */
export function Dialog({ trigger, title, description, closeLabel, children, className, ...root }: DialogProps) {
  return (
    <DialogPrimitive.Root {...root}>
      <DialogPrimitive.Trigger asChild>{trigger}</DialogPrimitive.Trigger>
      <DialogPrimitive.Portal>
        <DialogPrimitive.Overlay className="fixed inset-0 z-[var(--z-overlay)] bg-[var(--shadow-color)]" />
        <DialogPrimitive.Content className={cn(surface({ placement: "dialog" }), className)}>
          <DialogPrimitive.Title className="text-xl font-semibold">{title}</DialogPrimitive.Title>
          <DialogPrimitive.Description className="mb-4 text-muted-foreground">
            {description}
          </DialogPrimitive.Description>
          {children}
          <DialogPrimitive.Close asChild>
            <Button variant="secondary">{closeLabel}</Button>
          </DialogPrimitive.Close>
        </DialogPrimitive.Content>
      </DialogPrimitive.Portal>
    </DialogPrimitive.Root>
  );
}
/** Side panel uses the same modal keyboard contract as Dialog. */
export function Sheet({ trigger, title, description, closeLabel, children, className, ...root }: DialogProps) {
  return (
    <DialogPrimitive.Root {...root}>
      <DialogPrimitive.Trigger asChild>{trigger}</DialogPrimitive.Trigger>
      <DialogPrimitive.Portal>
        <DialogPrimitive.Overlay className="fixed inset-0 z-[var(--z-overlay)] bg-[var(--shadow-color)]" />
        <DialogPrimitive.Content className={cn(surface({ placement: "sheet" }), className)}>
          <DialogPrimitive.Title className="text-xl font-semibold">{title}</DialogPrimitive.Title>
          <DialogPrimitive.Description className="mb-4 text-muted-foreground">
            {description}
          </DialogPrimitive.Description>
          {children}
          <DialogPrimitive.Close asChild>
            <Button variant="secondary">{closeLabel}</Button>
          </DialogPrimitive.Close>
        </DialogPrimitive.Content>
      </DialogPrimitive.Portal>
    </DialogPrimitive.Root>
  );
}
/** Non-modal anchored details. The trigger must accept Radix's ref and event props. */
export function Popover({
  trigger,
  children,
  label,
  className,
  ...root
}: Pick<DialogProps, "trigger" | "children" | "className" | "open" | "defaultOpen" | "onOpenChange"> & {
  readonly label: string;
}) {
  return (
    <PopoverPrimitive.Root {...root}>
      <PopoverPrimitive.Trigger asChild>{trigger}</PopoverPrimitive.Trigger>
      <PopoverPrimitive.Portal>
        <PopoverPrimitive.Content
          aria-label={label}
          sideOffset={8}
          className={cn(surface({ placement: "popup" }), className)}
        >
          {children}
        </PopoverPrimitive.Content>
      </PopoverPrimitive.Portal>
    </PopoverPrimitive.Root>
  );
}
/** Additional information, never the only accessible name of an action. */
export function Tooltip({ children, text }: { readonly children: ReactNode; readonly text: string }) {
  return (
    <TooltipPrimitive.Provider delayDuration={300}>
      <TooltipPrimitive.Root>
        <TooltipPrimitive.Trigger asChild>{children}</TooltipPrimitive.Trigger>
        <TooltipPrimitive.Portal>
          <TooltipPrimitive.Content sideOffset={8} className={surface({ placement: "popup" })}>
            {text}
          </TooltipPrimitive.Content>
        </TooltipPrimitive.Portal>
      </TooltipPrimitive.Root>
    </TooltipPrimitive.Provider>
  );
}
export interface MenuAction {
  readonly id: string;
  readonly label: string;
  readonly disabled?: boolean;
  readonly onSelect: () => void;
}
const item =
  "cursor-default rounded px-3 py-2 outline-none data-[highlighted]:bg-selected data-[disabled]:text-muted-foreground";
/** Arrow keys, Home/End, typeahead and Escape follow the menu APG through Radix. */
export function DropdownMenu({
  trigger,
  actions,
  label,
}: {
  readonly trigger: ReactNode;
  readonly actions: readonly MenuAction[];
  readonly label: string;
}) {
  return (
    <MenuPrimitive.Root>
      <MenuPrimitive.Trigger asChild>{trigger}</MenuPrimitive.Trigger>
      <MenuPrimitive.Portal>
        <MenuPrimitive.Content aria-label={label} className={surface({ placement: "popup" })} sideOffset={8}>
          {actions.map((action) => (
            <MenuPrimitive.Item key={action.id} disabled={action.disabled} onSelect={action.onSelect} className={item}>
              {action.label}
            </MenuPrimitive.Item>
          ))}
        </MenuPrimitive.Content>
      </MenuPrimitive.Portal>
    </MenuPrimitive.Root>
  );
}
/** Context menu also opens with Shift+F10 on the focusable trigger region. */
export function ContextMenu({
  children,
  actions,
  label,
}: {
  readonly children: ReactNode;
  readonly actions: readonly MenuAction[];
  readonly label: string;
}) {
  return (
    <ContextPrimitive.Root>
      <ContextPrimitive.Trigger tabIndex={0} aria-label={label} className="block rounded border border-border p-3">
        {children}
      </ContextPrimitive.Trigger>
      <ContextPrimitive.Portal>
        <ContextPrimitive.Content aria-label={label} className={surface({ placement: "popup" })}>
          {actions.map((action) => (
            <ContextPrimitive.Item
              key={action.id}
              disabled={action.disabled}
              onSelect={action.onSelect}
              className={item}
            >
              {action.label}
            </ContextPrimitive.Item>
          ))}
        </ContextPrimitive.Content>
      </ContextPrimitive.Portal>
    </ContextPrimitive.Root>
  );
}
/** User-dismissed notices remain visible until explicitly closed; only the newest update is announced. */
export function NotificationCenter({
  label,
  dismissLabel,
  items,
  onDismiss,
}: {
  readonly label: string;
  readonly dismissLabel: string;
  readonly items: readonly { readonly id: string; readonly text: string; readonly read?: boolean }[];
  readonly onDismiss: (id: string) => void;
}) {
  return (
    <section aria-label={label}>
      <p role="status" aria-atomic="true">
        {items[0]?.text ?? ""}
      </p>
      <ul>
        {items.map((item) => (
          <li
            key={item.id}
            data-state={item.read ? "read" : "unread"}
            className="flex items-center justify-between gap-2 border-b border-border py-2"
          >
            <span>{item.text}</span>
            <Button
              variant="ghost"
              onClick={() => {
                onDismiss(item.id);
              }}
              aria-label={`${dismissLabel}: ${item.text}`}
            >
              {dismissLabel}
            </Button>
          </li>
        ))}
      </ul>
    </section>
  );
}
/** A dismissible toast. No automatic disappearance while the user is reading it. */
export function Toast({ message, dismissLabel }: { readonly message: string; readonly dismissLabel: string }) {
  const [visible, setVisible] = useState(true);
  return visible ? (
    <div role="status" className={surface({ placement: "popup" })}>
      <span>{message}</span>
      <Button
        variant="ghost"
        onClick={() => {
          setVisible(false);
        }}
      >
        {dismissLabel}
      </Button>
    </div>
  ) : null;
}
