import {
  useEffect,
  useCallback,
  useLayoutEffect,
  useId,
  useMemo,
  useRef,
  useState,
  type KeyboardEvent,
  Fragment,
} from "react";
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

export interface TreeNode {
  readonly id: string;
  readonly label: string;
  readonly children?: readonly TreeNode[];
  readonly hasChildren?: boolean;
}
interface FlatNode {
  readonly node: TreeNode;
  readonly level: number;
  readonly parent?: string | undefined;
  readonly position: number;
  readonly size: number;
}
/** Lazy virtual tree. Arrow keys navigate; Enter/Space select; expanding never moves selection implicitly. */
export function Tree({
  label,
  nodes,
  loadChildren,
  loadingLabel,
  errorLabel,
  onSelect,
}: {
  readonly label: string;
  readonly nodes: readonly TreeNode[];
  readonly loadChildren?: (id: string, signal: AbortSignal) => Promise<readonly TreeNode[]>;
  readonly loadingLabel: string;
  readonly errorLabel: string;
  readonly onSelect?: (id: string) => void;
}) {
  const [expanded, setExpanded] = useState<ReadonlySet<string>>(new Set());
  const [loaded, setLoaded] = useState<ReadonlyMap<string, readonly TreeNode[]>>(new Map());
  const [active, setActive] = useState(nodes[0]?.id ?? "");
  const [selected, setSelected] = useState("");
  const [pending, setPending] = useState<ReadonlySet<string>>(new Set());
  const [errors, setErrors] = useState<ReadonlySet<string>>(new Set());
  const requests = useRef(new Map<string, AbortController>());
  useEffect(() => {
    const current = requests.current;
    return () => {
      for (const controller of current.values()) controller.abort();
    };
  }, []);
  const flat: FlatNode[] = [];
  function flatten(children: readonly TreeNode[], level: number, parent?: string) {
    children.forEach((node, index) => {
      flat.push({ node, level, parent, position: index + 1, size: children.length });
      if (expanded.has(node.id)) flatten(node.children ?? loaded.get(node.id) ?? [], level + 1, node.id);
    });
  }
  flatten(nodes, 1);
  const scroll = useRef<HTMLDivElement>(null);
  // eslint-disable-next-line react-hooks/incompatible-library -- TanStack owns mutable measurement state; React Compiler is not enabled
  const virtual = useVirtualizer({
    count: flat.length,
    rangeExtractor: (range) => {
      const visible = defaultRangeExtractor(range);
      const index = flat.findIndex((entry) => entry.node.id === active);
      return index < 0 ? visible : [...new Set([...visible, index])].sort((a, b) => a - b);
    },
    getScrollElement: () => scroll.current,
    estimateSize: () => 48,
    getItemKey: (index) => flat[index]?.node.id ?? index,
    initialRect: { width: 320, height: 288 },
    overscan: 3,
  });
  const id = useId();
  async function expand(node: TreeNode) {
    setExpanded((previous) => new Set(previous).add(node.id));
    if (!node.hasChildren || node.children || loaded.has(node.id) || requests.current.has(node.id) || !loadChildren)
      return;
    const controller = new AbortController();
    requests.current.set(node.id, controller);
    setPending((previous) => new Set(previous).add(node.id));
    try {
      const result = await loadChildren(node.id, controller.signal);
      if (!controller.signal.aborted) {
        setLoaded((previous) => new Map(previous).set(node.id, result));
        setErrors((previous) => {
          const next = new Set(previous);
          next.delete(node.id);
          return next;
        });
      }
    } catch {
      if (!controller.signal.aborted) setErrors((previous) => new Set(previous).add(node.id));
    } finally {
      requests.current.delete(node.id);
      if (!controller.signal.aborted)
        setPending((previous) => {
          const next = new Set(previous);
          next.delete(node.id);
          return next;
        });
    }
  }
  function collapse(nodeId: string) {
    let parent = flat.find((entry) => entry.node.id === active)?.parent;
    while (parent) {
      if (parent === nodeId) {
        setActive(nodeId);
        break;
      }
      parent = flat.find((entry) => entry.node.id === parent)?.parent;
    }
    setExpanded((previous) => {
      const next = new Set(previous);
      next.delete(nodeId);
      return next;
    });
  }
  function move(index: number) {
    const item = flat[Math.max(0, Math.min(flat.length - 1, index))];
    if (!item) return;
    setActive(item.node.id);
    virtual.scrollToIndex(Math.max(0, index));
  }
  function keyboard(event: KeyboardEvent<HTMLDivElement>) {
    const index = flat.findIndex((item) => item.node.id === active);
    const item = flat[index];
    if (!item) return;
    switch (event.key) {
      case "ArrowDown":
        move(index + 1);
        break;
      case "ArrowUp":
        move(index - 1);
        break;
      case "Home":
        move(0);
        break;
      case "End":
        move(flat.length - 1);
        break;
      case "ArrowRight":
        if (!expanded.has(active)) {
          if (item.node.hasChildren || item.node.children?.length) void expand(item.node);
        } else if (flat[index + 1]?.parent === active) move(index + 1);
        break;
      case "ArrowLeft":
        if (expanded.has(active)) collapse(active);
        else if (item.parent) move(flat.findIndex((candidate) => candidate.node.id === item.parent));
        break;
      case "Enter":
      case " ":
        setSelected(active);
        onSelect?.(active);
        break;
      case "*":
        for (const sibling of flat.filter((candidate) => candidate.parent === item.parent))
          if (sibling.node.hasChildren || sibling.node.children?.length) void expand(sibling.node);
        break;
      default: {
        if (event.key.length !== 1) return;
        const match = flat.findIndex(
          (candidate, candidateIndex) =>
            candidateIndex > index &&
            candidate.node.label.toLocaleLowerCase().startsWith(event.key.toLocaleLowerCase()),
        );
        if (match >= 0) move(match);
        else return;
      }
    }
    event.preventDefault();
  }
  return (
    <div>
      <div
        ref={scroll}
        role="tree"
        aria-label={label}
        tabIndex={0}
        aria-activedescendant={active ? `${id}-${active}` : undefined}
        className="max-h-72 overflow-auto rounded border border-border"
        onKeyDown={keyboard}
      >
        <div role="presentation" style={{ height: virtual.getTotalSize(), position: "relative" }}>
          {virtual.getVirtualItems().map((item) => {
            const entry = flat[item.index];
            if (!entry) return null;
            const branch = Boolean(entry.node.hasChildren ?? entry.node.children?.length);
            return (
              <div
                key={entry.node.id}
                id={`${id}-${entry.node.id}`}
                role="treeitem"
                tabIndex={-1}
                onKeyDown={(event) => {
                  event.stopPropagation();
                  keyboard(event);
                }}
                aria-level={entry.level}
                aria-setsize={entry.size}
                aria-posinset={entry.position}
                aria-selected={selected === entry.node.id}
                aria-expanded={branch ? expanded.has(entry.node.id) : undefined}
                aria-busy={pending.has(entry.node.id)}
                data-active={active === entry.node.id}
                className="absolute top-0 left-0 flex w-full items-center gap-2 px-2 data-[active=true]:bg-selected"
                style={{
                  height: item.size,
                  transform: `translateY(${String(item.start)}px)`,
                  paddingInlineStart: entry.level * 16,
                }}
                onClick={() => {
                  setActive(entry.node.id);
                  setSelected(entry.node.id);
                  onSelect?.(entry.node.id);
                  scroll.current?.focus();
                }}
              >
                {branch && (
                  <button
                    type="button"
                    tabIndex={-1}
                    aria-label={entry.node.label}
                    aria-expanded={expanded.has(entry.node.id)}
                    onClick={(event) => {
                      event.stopPropagation();
                      if (expanded.has(entry.node.id)) collapse(entry.node.id);
                      else void expand(entry.node);
                    }}
                  >
                    {expanded.has(entry.node.id) ? "-" : "+"}
                  </button>
                )}
                {entry.node.label}
              </div>
            );
          })}
        </div>
      </div>
      <p role="status">{pending.size ? loadingLabel : errors.size ? errorLabel : ""}</p>
    </div>
  );
}

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
