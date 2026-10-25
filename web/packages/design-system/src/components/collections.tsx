import { useCallback, useLayoutEffect, useId, useMemo, useRef, useState, type KeyboardEvent, Fragment } from "react";
import {
  flexRender,
  createRow,
  type Table,
  type Row,
  type RowModel,
  getFilteredRowModel,
  getSortedRowModel,
  useReactTable,
  type ColumnDef,
  type SortingState,
  type VisibilityState,
  type RowSelectionState,
  type ColumnPinningState,
} from "@tanstack/react-table";
import { useVirtualizer, defaultRangeExtractor } from "@tanstack/react-virtual";
import { Button } from "./button";
import { Field } from "./patterns";
import { cn } from "../cn";

export interface TableRecord {
  readonly id: string;
}
/** Flat immutable records reuse TanStack rows until the record reference or index changes. */
function stableCoreRows<T extends TableRecord>() {
  return (table: Table<T>) => {
    let previous: T[] | null = null;
    let accessors: readonly unknown[] = [];
    let model: RowModel<T> = { rows: [], flatRows: [], rowsById: {} };
    return () => {
      const data = table.options.data;
      const nextAccessors = table
        .getAllLeafColumns()
        .flatMap((column) => [
          column.id,
          "accessorFn" in column.columnDef ? column.columnDef.accessorFn : undefined,
          "accessorKey" in column.columnDef ? column.columnDef.accessorKey : undefined,
        ]);
      const sameAccessors =
        nextAccessors.length === accessors.length && nextAccessors.every((item, index) => item === accessors[index]);
      if (previous === data && sameAccessors) return model;
      // A null prototype makes arbitrary caller-provided IDs safe dictionary keys.
      const rowsById = Object.create(null) as Record<string, Row<T>>;
      const rows = data.map((record, index) => {
        const cached = Object.hasOwn(model.rowsById, record.id) ? model.rowsById[record.id] : undefined;
        const row =
          sameAccessors && cached?.original === record && cached.index === index
            ? cached
            : createRow(table, record.id, record, index, 0);
        if (Object.hasOwn(rowsById, row.id)) throw new Error(`Duplicate row ID: ${row.id}`);
        rowsById[row.id] = row;
        return row;
      });
      previous = data;
      accessors = nextAccessors;
      model = { rows, flatRows: rows, rowsById };
      return model;
    };
  };
}
/** Virtual grid. IDs must be stable across snapshots; selection and active cell are keyed by record ID. */
export function DataTable<T extends TableRecord>({
  label,
  data,
  columns,
  labels,
  onBulkAction,
  bulkDisabled = false,
  onActiveChange,
  className,
}: {
  readonly label: string;
  readonly data: T[];
  readonly columns: ColumnDef<T>[];
  readonly labels: {
    readonly search: string;
    readonly select: string;
    readonly columns: string;
    readonly pin: string;
    readonly bulk: string;
    readonly empty: string;
  };
  readonly onBulkAction?: (ids: readonly string[]) => void;
  readonly bulkDisabled?: boolean;
  readonly onActiveChange?: (id: string) => void;
  readonly className?: string;
}) {
  const [sorting, setSorting] = useState<SortingState>([]);
  const [globalFilter, setGlobalFilter] = useState("");
  const [columnVisibility, setColumnVisibility] = useState<VisibilityState>({});
  const [rowSelection, setRowSelection] = useState<RowSelectionState>({});
  const [columnPinning, setColumnPinning] = useState<ColumnPinningState>({ left: [], right: [] });
  const [active, setActive] = useState<{ id: string; column: number } | null>(null);
  const [gridFocused, setGridFocused] = useState(false);
  const scroll = useRef<HTMLDivElement>(null);
  const grid = useRef<HTMLTableElement>(null);
  const id = useId();
  const selection = useMemo<ColumnDef<T>>(
    () => ({
      id: "selection",
      header: labels.select,
      size: 64,
      enableSorting: false,
      enableHiding: false,
      cell: ({ row }) => (
        <input
          type="checkbox"
          aria-label={`${labels.select}: ${row.id}`}
          checked={row.getIsSelected()}
          onChange={row.getToggleSelectedHandler()}
          tabIndex={-1}
        />
      ),
    }),
    [labels.select],
  );
  // eslint-disable-next-line react-hooks/incompatible-library -- TanStack owns mutable row state; React Compiler is not enabled
  const table = useReactTable({
    data,
    columns: useMemo(() => [selection, ...columns], [selection, columns]),
    state: { sorting, globalFilter, columnVisibility, rowSelection, columnPinning },
    onSortingChange: setSorting,
    onGlobalFilterChange: setGlobalFilter,
    onColumnVisibilityChange: setColumnVisibility,
    onRowSelectionChange: setRowSelection,
    onColumnPinningChange: setColumnPinning,
    getRowId: (row) => row.id,
    getCoreRowModel: stableCoreRows(),
    getSortedRowModel: getSortedRowModel(),
    getFilteredRowModel: getFilteredRowModel(),
  });
  const rows = table.getRowModel().rows;
  const virtual = useVirtualizer({
    count: rows.length,
    rangeExtractor: (range) => {
      const visible = defaultRangeExtractor(range);
      const index = gridFocused ? rows.findIndex((row) => row.id === active?.id) : -1;
      return index < 0 ? visible : [...new Set([...visible, index])].sort((a, b) => a - b);
    },
    getScrollElement: () => scroll.current,
    getItemKey: (index) => rows[index]?.id ?? index,
    estimateSize: () => 64,
    overscan: 4,
    initialRect: { width: 800, height: 384 },
  });
  const anchor = useRef<{ id: string; delta: number; context: string } | null>(null);
  const context = JSON.stringify(sorting) + globalFilter;
  const rememberAnchor = useCallback(() => {
    const top = scroll.current?.scrollTop ?? 0;
    const first = virtual.getVirtualItems().find((item) => item.end > top);
    const row = first ? rows[first.index] : undefined;
    if (row && first) anchor.current = { id: row.id, delta: first.start - top, context };
  }, [context, rows, virtual]);
  useLayoutEffect(() => {
    const previous = anchor.current;
    if (previous?.context === context) {
      const index = rows.findIndex((row) => row.id === previous.id);
      const offset = index < 0 ? undefined : virtual.getOffsetForIndex(index, "start")?.[0];
      if (offset !== undefined) virtual.scrollToOffset(Math.max(0, offset - previous.delta));
    }
    rememberAnchor();
  }, [data, context, virtual, rememberAnchor, rows]);
  const focusId = rows.some((row) => row.id === active?.id) ? active?.id : rows[0]?.id;
  const focusColumn = Math.min(active?.column ?? 0, table.getVisibleLeafColumns().length - 1);
  const cellId = (rowId: string, column: number) => `${id}-${encodeURIComponent(rowId)}-${String(column)}`;
  function keyboard(event: KeyboardEvent<HTMLTableElement>) {
    const index = rows.findIndex((row) => row.id === (active?.id ?? rows[0]?.id));
    const column = active?.column ?? 0;
    let nextRow = index;
    let nextColumn = column;
    switch (event.key) {
      case "ArrowDown":
        nextRow++;
        break;
      case "ArrowUp":
        nextRow--;
        break;
      case "ArrowRight":
        nextColumn++;
        break;
      case "ArrowLeft":
        nextColumn--;
        break;
      case "Home":
        nextColumn = 0;
        if (event.ctrlKey) nextRow = 0;
        break;
      case "End":
        nextColumn = table.getVisibleLeafColumns().length - 1;
        if (event.ctrlKey) nextRow = rows.length - 1;
        break;
      case " ": {
        const row = rows[index];
        if (row) {
          event.preventDefault();
          row.toggleSelected();
        }
        return;
      }
      default:
        return;
    }
    event.preventDefault();
    nextRow = Math.max(0, Math.min(rows.length - 1, nextRow));
    nextColumn = Math.max(0, Math.min(table.getVisibleLeafColumns().length - 1, nextColumn));
    const row = rows[nextRow];
    if (!row) return;
    setActive({ id: row.id, column: nextColumn });
    virtual.scrollToIndex(nextRow);
    requestAnimationFrame(() => {
      document.getElementById(cellId(row.id, nextColumn))?.focus({ preventScroll: true });
    });
  }
  const items = virtual.getVirtualItems();
  return (
    <section className={cn("grid min-w-0 gap-3", className)} aria-label={label}>
      <Field
        label={labels.search}
        value={globalFilter}
        onChange={(event) => {
          setGlobalFilter(event.target.value);
          virtual.scrollToOffset(0);
        }}
      />
      <details>
        <summary>{labels.columns}</summary>
        {table
          .getAllLeafColumns()
          .filter((column) => column.id !== "selection")
          .map((column) => (
            <div key={column.id} className="flex items-center gap-2">
              <label>
                <input type="checkbox" checked={column.getIsVisible()} onChange={column.getToggleVisibilityHandler()} />
                {typeof column.columnDef.header === "string" ? column.columnDef.header : column.id}
              </label>
              <Button
                variant="ghost"
                aria-pressed={Boolean(column.getIsPinned())}
                onClick={() => {
                  column.pin(column.getIsPinned() ? false : "left");
                }}
              >
                {labels.pin}: {typeof column.columnDef.header === "string" ? column.columnDef.header : column.id}
              </Button>
            </div>
          ))}
      </details>
      <Button
        variant="secondary"
        disabled={bulkDisabled || !Object.values(rowSelection).some(Boolean)}
        onClick={() => {
          onBulkAction?.(table.getSelectedRowModel().rows.map((row) => row.id));
        }}
      >
        {labels.bulk} ({Object.values(rowSelection).filter(Boolean).length})
      </Button>
      <div ref={scroll} onScroll={rememberAnchor} className="max-h-96 overflow-auto rounded border border-border">
        <table
          ref={grid}
          role="grid"
          aria-label={label}
          aria-rowcount={rows.length + 1}
          aria-colcount={table.getVisibleLeafColumns().length}
          className="w-full border-collapse"
          onFocusCapture={() => {
            setGridFocused(true);
          }}
          onBlurCapture={(event) => {
            if (!event.currentTarget.contains(event.relatedTarget)) setGridFocused(false);
          }}
          onKeyDown={keyboard}
        >
          <thead className="sticky top-0 z-20 bg-card">
            {table.getHeaderGroups().map((group) => (
              <tr key={group.id} role="row">
                {group.headers.map((header, index) => (
                  <th
                    key={header.id}
                    role="columnheader"
                    aria-colindex={index + 1}
                    aria-sort={
                      header.column.getIsSorted() === "asc"
                        ? "ascending"
                        : header.column.getIsSorted() === "desc"
                          ? "descending"
                          : undefined
                    }
                    className="border-b border-border p-2 text-left"
                  >
                    {header.column.getCanSort() ? (
                      <button type="button" onClick={header.column.getToggleSortingHandler()}>
                        {flexRender(header.column.columnDef.header, header.getContext())}
                      </button>
                    ) : (
                      flexRender(header.column.columnDef.header, header.getContext())
                    )}
                  </th>
                ))}
              </tr>
            ))}
          </thead>
          <tbody>
            {items.map((item, itemIndex) => {
              const row = rows[item.index];
              const gap = item.start - (items[itemIndex - 1]?.end ?? 0);
              return row ? (
                <Fragment key={row.id}>
                  {gap > 0 && (
                    <tr aria-hidden="true">
                      <td colSpan={table.getVisibleLeafColumns().length} style={{ height: gap }} />
                    </tr>
                  )}
                  <tr
                    ref={virtual.measureElement}
                    data-index={item.index}
                    key={row.id}
                    role="row"
                    aria-rowindex={item.index + 2}
                    aria-selected={row.getIsSelected()}
                    data-id={row.id}
                    style={{ height: item.size }}
                    className="aria-selected:bg-selected"
                  >
                    {row.getVisibleCells().map((cell, index) => (
                      <td
                        key={cell.id}
                        id={cellId(row.id, index)}
                        role="gridcell"
                        aria-colindex={index + 1}
                        tabIndex={focusId === row.id && focusColumn === index ? 0 : -1}
                        onFocus={() => {
                          setActive({ id: row.id, column: index });
                          onActiveChange?.(row.id);
                        }}
                        className="border-b border-border bg-inherit p-2"
                        style={
                          cell.column.getIsPinned()
                            ? {
                                position: "sticky",
                                left: cell.column.getStart("left"),
                                background: "var(--card)",
                                minWidth: cell.column.getSize(),
                              }
                            : undefined
                        }
                      >
                        {flexRender(cell.column.columnDef.cell, cell.getContext())}
                      </td>
                    ))}
                  </tr>
                </Fragment>
              ) : null;
            })}
            {virtual.getTotalSize() - (items.at(-1)?.end ?? 0) > 0 && (
              <tr aria-hidden="true">
                <td
                  colSpan={table.getVisibleLeafColumns().length}
                  style={{ height: virtual.getTotalSize() - (items.at(-1)?.end ?? 0) }}
                />
              </tr>
            )}
          </tbody>
        </table>
        {!rows.length && <p>{labels.empty}</p>}
      </div>
    </section>
  );
}

export { Tree, type TreeNode } from "./tree";

export interface PlanMarker {
  readonly id: string;
  readonly label: string;
  readonly x: number;
  readonly y: number;
  readonly state: "normal" | "alarm" | "offline";
}
/** Pan is native scrolling; zoom preserves text sizes. Markers have a synchronized textual alternative. */
export function FloorPlan({
  label,
  markers,
  selected,
  onSelect,
  labels,
}: {
  readonly label: string;
  readonly markers: readonly PlanMarker[];
  readonly selected?: string;
  readonly onSelect: (id: string) => void;
  readonly labels: {
    readonly zoomIn: string;
    readonly zoomOut: string;
    readonly reset: string;
    readonly legend: string;
    readonly normal: string;
    readonly alarm: string;
    readonly offline: string;
  };
}) {
  const [zoom, setZoom] = useState(1);
  return (
    <div className="grid min-w-0 gap-2">
      <div className="flex flex-wrap gap-2">
        <Button
          variant="secondary"
          disabled={zoom >= 3}
          onClick={() => {
            setZoom((value) => Math.min(3, value + 0.25));
          }}
        >
          {labels.zoomIn}
        </Button>
        <Button
          variant="secondary"
          disabled={zoom <= 0.5}
          onClick={() => {
            setZoom((value) => Math.max(0.5, value - 0.25));
          }}
        >
          {labels.zoomOut}
        </Button>
        <Button
          variant="ghost"
          onClick={() => {
            setZoom(1);
          }}
        >
          {labels.reset}
        </Button>
        <output>{Math.round(zoom * 100)}%</output>
      </div>
      <div
        role="group"
        aria-label={label}
        // eslint-disable-next-line jsx-a11y/no-noninteractive-tabindex -- keyboard scrolling of the plan is required
        tabIndex={0}
        className="max-h-96 overflow-auto rounded border border-border"
      >
        <div className="relative min-w-full bg-muted" style={{ width: 800 * zoom, height: 360 * zoom }}>
          <svg aria-hidden="true" className="absolute inset-0 h-full w-full" viewBox="0 0 800 360">
            <path
              d="M24 24H776V336H24ZM24 180H776M260 24V336M530 24V336"
              fill="none"
              stroke="var(--input)"
              strokeWidth="4"
            />
          </svg>
          {markers.map((marker) => (
            <button
              type="button"
              key={marker.id}
              aria-label={`${marker.label}: ${labels[marker.state]}`}
              aria-pressed={selected === marker.id}
              className="absolute -translate-x-1/2 -translate-y-1/2 rounded border-2 border-input bg-card px-2 py-1 aria-pressed:outline-2 aria-pressed:outline-ring"
              style={{
                left: `${String(marker.x)}%`,
                top: `${String(marker.y)}%`,
                color:
                  marker.state === "alarm"
                    ? "var(--danger)"
                    : marker.state === "offline"
                      ? "var(--warning)"
                      : "var(--foreground)",
              }}
              onClick={() => {
                onSelect(marker.id);
              }}
            >
              {marker.state === "alarm" ? "! " : marker.state === "offline" ? "? " : ""}
              {marker.label}
            </button>
          ))}
        </div>
      </div>
      <details>
        <summary>{labels.legend}</summary>
        <ul>
          {markers.map((marker) => (
            <li key={marker.id}>
              <button
                type="button"
                onClick={() => {
                  onSelect(marker.id);
                }}
                aria-pressed={selected === marker.id}
              >
                {marker.label}: {labels[marker.state]}
              </button>
            </li>
          ))}
        </ul>
      </details>
    </div>
  );
}
/** Visual bars repeat their values in a native table; no information depends on a color distinction. */
export function TrendChart({
  label,
  values,
  valueLabel,
}: {
  readonly label: string;
  readonly values: readonly { readonly label: string; readonly value: number }[];
  readonly valueLabel: string;
}) {
  const max = Math.max(1, ...values.map((item) => item.value));
  return (
    <figure aria-label={label}>
      <figcaption>{label}</figcaption>
      <div aria-hidden="true" className="flex h-32 items-end gap-2">
        {values.map((item) => (
          <div
            key={item.label}
            className="min-w-4 flex-1 bg-[var(--chart-series)]"
            style={{ height: `${String(Math.max(2, (item.value / max) * 100))}%` }}
          />
        ))}
      </div>
      <table className="w-full">
        <caption className="sr-only">{label}</caption>
        <thead>
          <tr>
            <th scope="col">{label}</th>
            <th scope="col">{valueLabel}</th>
          </tr>
        </thead>
        <tbody>
          {values.map((item) => (
            <tr key={item.label}>
              <th scope="row">{item.label}</th>
              <td>{item.value}</td>
            </tr>
          ))}
        </tbody>
      </table>
    </figure>
  );
}
