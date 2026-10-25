import { useEffect, useId, useRef, useState, type KeyboardEvent } from "react";
import { useVirtualizer, defaultRangeExtractor } from "@tanstack/react-virtual";
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
