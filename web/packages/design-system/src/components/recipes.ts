import { cva } from "class-variance-authority";
/** Component recipes own geometry; semantic variables and functional CSS own theme and browser modes. */
export const cardRecipe = cva(
  "min-w-0 rounded-lg border border-[var(--panel-border)] bg-[var(--panel-background)] p-[var(--card-padding)]",
);
export const cardHeaderRecipe = cva(
  "mb-[var(--stack-space)] flex flex-wrap items-start justify-between gap-x-4 gap-y-3",
);
export const cardDescriptionRecipe = cva("mt-1 text-sm text-muted-foreground");
export const eyebrowRecipe = cva("mb-1 text-xs font-semibold tracking-[0.02em] text-muted-foreground");
export const fieldRecipe = cva("grid min-w-0 gap-1.5 text-sm");
export const labelRecipe = cva("font-[550]");
export const requiredRecipe = cva("ml-0.5 text-danger");
export const helpRecipe = cva("text-[0.8125rem] text-muted-foreground data-[error=true]:text-danger");
export const inputRecipe = cva(
  "ds-input min-h-[var(--control-height)] w-full min-w-0 rounded-md border border-[var(--field-border)] bg-[var(--field-background)] px-3 py-0 text-[var(--field-foreground)] transition-[border-color,box-shadow] duration-[var(--motion-fast)] placeholder:text-muted-foreground hover:enabled:border-muted-foreground focus-visible:border-ring focus-visible:shadow-[0_0_0_1px_var(--ring)] focus-visible:outline-none disabled:cursor-not-allowed disabled:opacity-55 aria-invalid:border-danger aria-invalid:focus-visible:shadow-[0_0_0_1px_var(--danger)]",
);
export const badgeRecipe = cva(
  "ds-badge inline-flex min-h-[1.375rem] items-center gap-1.5 whitespace-nowrap rounded-sm border border-[color-mix(in_srgb,var(--tone)_28%,transparent)] bg-[color-mix(in_srgb,var(--tone)_14%,var(--card))] px-2 py-0 text-xs leading-none font-semibold text-[var(--tone)] before:size-1.5 before:flex-none before:rounded-full before:bg-current",
  {
    variants: {
      emphasis: {
        subtle: "",
        solid: "border-[var(--tone-solid)] bg-[var(--tone-solid)] text-[var(--tone-solid-foreground)]",
      },
    },
    defaultVariants: { emphasis: "subtle" },
  },
);
export const noticeRecipe = cva(
  "ds-notice flex items-start gap-3 rounded-md border border-[color-mix(in_srgb,var(--tone)_35%,var(--border))] border-l-[3px] border-l-[var(--tone)] bg-[color-mix(in_srgb,var(--tone)_8%,var(--card))] px-3.5 py-3 text-sm text-foreground",
);
export const noticeIconRecipe = cva("mt-[0.0625rem] size-[1.125rem] flex-none text-[var(--tone)]");
export const noticeBodyRecipe = cva("min-w-0 flex-1");
export const tableContainerRecipe = cva("ds-table-wrap max-w-full overflow-x-auto rounded-md border border-border");
export const emptyRecipe = cva("grid justify-items-center gap-2 px-4 py-10 text-center");
export const overlayRecipe = cva("ds-overlay fixed inset-0 z-[var(--z-overlay)] bg-[var(--overlay)]");
export const dialogRecipe = cva(
  "fixed top-1/2 left-1/2 z-[var(--z-dialog)] max-h-[calc(100dvh-2rem)] w-[min(30rem,calc(100%_-_2rem))] -translate-x-1/2 -translate-y-1/2 overflow-auto rounded-xl border border-border bg-elevated p-6 shadow-[var(--shadow-overlay)]",
);
export const dialogTitleRecipe = cva("text-[1.0625rem] font-[620]");
export const dialogDescriptionRecipe = cva("mt-2 text-sm text-muted-foreground");
export const actionsRecipe = cva("mt-6 flex flex-wrap justify-end gap-2");
export const switchRecipe = cva(
  "ds-switch inline-flex min-h-[var(--control-height)] cursor-pointer items-center gap-2 text-sm",
);
