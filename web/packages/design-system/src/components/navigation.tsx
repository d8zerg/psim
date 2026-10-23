import { useEffect, useState, type ReactNode } from "react";
import * as TabsPrimitive from "@radix-ui/react-tabs";
import { Button } from "./button";
import { Dialog } from "./overlays";
import { Field } from "./patterns";
import type { MenuAction } from "./overlays";

/** Roving tab stop and automatic arrow activation are provided by Radix. */
export function Tabs({
  label,
  items,
  value,
  defaultValue,
  onValueChange,
}: {
  readonly label: string;
  readonly items: readonly {
    readonly id: string;
    readonly label: string;
    readonly content: ReactNode;
    readonly disabled?: boolean;
  }[];
  readonly value?: string;
  readonly defaultValue?: string;
  readonly onValueChange?: (value: string) => void;
}) {
  return (
    <TabsPrimitive.Root value={value} defaultValue={defaultValue ?? items[0]?.id} onValueChange={onValueChange}>
      <TabsPrimitive.List aria-label={label} className="flex gap-2 border-b border-border">
        {items.map((item) => (
          <TabsPrimitive.Trigger
            key={item.id}
            value={item.id}
            disabled={item.disabled}
            className="rounded-t px-3 py-2 data-[state=active]:bg-selected"
          >
            {item.label}
          </TabsPrimitive.Trigger>
        ))}
      </TabsPrimitive.List>
      {items.map((item) => (
        <TabsPrimitive.Content key={item.id} value={item.id} className="py-3">
          {item.content}
        </TabsPrimitive.Content>
      ))}
    </TabsPrimitive.Root>
  );
}
export function Breadcrumbs({
  label,
  items,
}: {
  readonly label: string;
  readonly items: readonly { readonly label: string; readonly href: string }[];
}) {
  return (
    <nav aria-label={label}>
      <ol className="flex flex-wrap gap-2">
        {items.map((item, index) => (
          <li key={item.href}>
            <a href={item.href} aria-current={index === items.length - 1 ? "page" : undefined}>
              {item.label}
            </a>
          </li>
        ))}
      </ol>
    </nav>
  );
}
/** Visibility follows role metadata; server authorization remains authoritative. */
export function SideNav({
  label,
  userRole,
  items,
}: {
  readonly label: string;
  readonly userRole: string;
  readonly items: readonly {
    readonly id: string;
    readonly label: string;
    readonly href: string;
    readonly roles: readonly string[];
    readonly current?: boolean;
  }[];
}) {
  return (
    <nav aria-label={label}>
      <ul>
        {items
          .filter((item) => item.roles.includes(userRole))
          .map((item) => (
            <li key={item.id}>
              <a
                className="block rounded px-3 py-2 aria-[current=page]:bg-selected"
                href={item.href}
                aria-current={item.current ? "page" : undefined}
              >
                {item.label}
              </a>
            </li>
          ))}
      </ul>
    </nav>
  );
}
export function Pagination({
  label,
  previousLabel,
  nextLabel,
  page,
  pages,
  onPageChange,
}: {
  readonly label: string;
  readonly previousLabel: string;
  readonly nextLabel: string;
  readonly page: number;
  readonly pages: number;
  readonly onPageChange: (page: number) => void;
}) {
  return (
    <nav aria-label={label} className="flex items-center gap-3">
      <Button
        variant="secondary"
        disabled={page <= 1}
        onClick={() => {
          onPageChange(page - 1);
        }}
      >
        {previousLabel}
      </Button>
      <output>
        {page} / {pages}
      </output>
      <Button
        variant="secondary"
        disabled={page >= pages}
        onClick={() => {
          onPageChange(page + 1);
        }}
      >
        {nextLabel}
      </Button>
    </nav>
  );
}
/** Ctrl/Cmd+K opens a filtered action list. Action selection closes the modal before calling the handler. */
export function CommandPalette({
  label,
  searchLabel,
  closeLabel,
  actions,
}: {
  readonly label: string;
  readonly searchLabel: string;
  readonly closeLabel: string;
  readonly actions: readonly MenuAction[];
}) {
  const [open, setOpen] = useState(false);
  const [query, setQuery] = useState("");
  useEffect(() => {
    const handler = (event: KeyboardEvent) => {
      if ((event.ctrlKey || event.metaKey) && event.key.toLowerCase() === "k") {
        event.preventDefault();
        setOpen((previous) => !previous);
      }
    };
    window.addEventListener("keydown", handler);
    return () => {
      window.removeEventListener("keydown", handler);
    };
  }, []);
  return (
    <Dialog
      open={open}
      onOpenChange={setOpen}
      trigger={
        <Button variant="secondary">
          {label} <kbd>Ctrl+K</kbd>
        </Button>
      }
      title={label}
      description={searchLabel}
      closeLabel={closeLabel}
    >
      <Field
        label={searchLabel}
        value={query}
        onChange={(event) => {
          setQuery(event.target.value);
        }}
      />
      <ul>
        {actions
          .filter((action) => action.label.toLocaleLowerCase().includes(query.toLocaleLowerCase()))
          .map((action) => (
            <li key={action.id}>
              <Button
                variant="ghost"
                disabled={action.disabled}
                onClick={() => {
                  setOpen(false);
                  action.onSelect();
                }}
              >
                {action.label}
              </Button>
            </li>
          ))}
      </ul>
    </Dialog>
  );
}
