import { afterEach, beforeEach, describe, expect, it, vi } from "vitest";
import { act, cleanup, fireEvent, render, screen, waitFor, within } from "@testing-library/react";
import { useState } from "react";
import { Dialog, Sheet, Popover, Tooltip, DropdownMenu, ContextMenu, NotificationCenter, Toast } from "./overlays";
import { Tabs, SideNav, Breadcrumbs, Pagination, CommandPalette } from "./navigation";
import { Combobox, MultiSelect, DatePeriodPicker, FileUpload, ValidatedForm } from "./forms";
import { DataTable, Tree, FloorPlan, TrendChart } from "./collections";
import { Button } from "./button";

beforeEach(() => {
  vi.stubGlobal(
    "ResizeObserver",
    class {
      observe() {
        /* Layout is supplied by the dimensions below. */
      }
      unobserve() {
        /* Layout is supplied by the dimensions below. */
      }
      disconnect() {
        /* No browser observer is registered. */
      }
    },
  );
  vi.spyOn(HTMLElement.prototype, "getBoundingClientRect").mockReturnValue({
    x: 0,
    y: 0,
    width: 800,
    height: 384,
    top: 0,
    left: 0,
    right: 800,
    bottom: 384,
    toJSON: () => ({}),
  });
  vi.spyOn(HTMLElement.prototype, "offsetHeight", "get").mockReturnValue(384);
  vi.spyOn(HTMLElement.prototype, "offsetWidth", "get").mockReturnValue(800);
  vi.spyOn(HTMLElement.prototype, "clientHeight", "get").mockReturnValue(384);
  vi.spyOn(HTMLElement.prototype, "clientWidth", "get").mockReturnValue(800);
  HTMLElement.prototype.scrollTo = vi.fn();
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  vi.unstubAllGlobals();
});
const comboLabels = { loading: "Loading", empty: "Empty", error: "Search failed" };
const options = [
  { value: "one", label: "First" },
  { value: "two", label: "Second" },
];
const actions = [
  { id: "inspect", label: "Inspect", onSelect: vi.fn() },
  { id: "disabled", label: "Disabled", disabled: true, onSelect: vi.fn() },
];

describe("overlays and navigation", () => {
  it.each([Dialog, Sheet])("opens and closes a modal with accessible title and description", async (Component) => {
    render(
      <Component trigger={<Button>Open</Button>} title="Details" description="Source evidence" closeLabel="Close">
        <p>SENSOR-42</p>
      </Component>,
    );
    fireEvent.click(screen.getByText("Open"));
    expect(screen.getByRole("dialog").getAttribute("aria-describedby")).toBeTruthy();
    fireEvent.click(screen.getByText("Close"));
    await waitFor(() => {
      expect(screen.queryByRole("dialog")).toBeNull();
    });
  });
  it("supports anchored evidence and optional tooltips", () => {
    render(
      <>
        <Popover label="Evidence" trigger={<Button>Why</Button>}>
          <p>Source chain</p>
        </Popover>
        <Tooltip text="Explain">
          <Button>Help</Button>
        </Tooltip>
        <DropdownMenu label="Actions" trigger={<Button>Menu</Button>} actions={actions} />
        <ContextMenu label="Context" actions={actions}>
          Record
        </ContextMenu>
      </>,
    );
    fireEvent.click(screen.getByText("Why"));
    expect(screen.getByText("Source chain")).toBeTruthy();
    fireEvent.keyDown(screen.getByRole("dialog"), { key: "Escape" });
  });
  it("dismisses notices and toasts explicitly", () => {
    const dismiss = vi.fn();
    render(
      <>
        <NotificationCenter
          label="Notices"
          dismissLabel="Dismiss"
          items={[{ id: "one", text: "Recovered", read: true }]}
          onDismiss={dismiss}
        />
        <Toast message="Saved" dismissLabel="Close" />
      </>,
    );
    fireEvent.click(screen.getByText("Dismiss"));
    expect(dismiss).toHaveBeenCalledWith("one");
    fireEvent.click(screen.getByText("Close"));
    expect(screen.queryByText("Saved")).toBeNull();
    cleanup();
    render(<NotificationCenter label="Notices" dismissLabel="Dismiss" items={[]} onDismiss={dismiss} />);
    expect(screen.getByRole("status").textContent).toBe("");
  });
  it("filters modules by role and clamps navigation through disabled actions", () => {
    const change = vi.fn();
    render(
      <>
        <SideNav
          label="Modules"
          userRole="operator"
          items={[
            { id: "alarms", href: "#alarms", label: "Alarms", roles: ["operator"], current: true },
            { id: "admin", href: "#admin", label: "Admin", roles: ["admin"] },
          ]}
        />
        <Breadcrumbs
          label="Path"
          items={[
            { href: "#site", label: "Site" },
            { href: "#floor", label: "Floor" },
          ]}
        />
        <Pagination label="Pages" previousLabel="Previous" nextLabel="Next" page={2} pages={3} onPageChange={change} />
        <Tabs
          label="Sections"
          items={[
            { id: "one", label: "Summary", content: "Summary content" },
            { id: "two", label: "History", content: "History content" },
          ]}
        />
      </>,
    );
    expect(screen.queryByText("Admin")).toBeNull();
    expect(screen.getByText("Floor").getAttribute("aria-current")).toBe("page");
    fireEvent.click(screen.getByText("Previous"));
    fireEvent.click(screen.getByText("Next"));
    expect(change.mock.calls).toEqual([[1], [3]]);
    expect(screen.getByRole("tabpanel").textContent).toBe("Summary content");
  });
  it("opens the command palette with Ctrl+K and executes an explicit enabled action", async () => {
    const select = vi.fn();
    render(
      <CommandPalette
        label="Palette"
        searchLabel="Find"
        closeLabel="Close"
        actions={[{ id: "inspect", label: "Inspect", onSelect: select }]}
      />,
    );
    fireEvent.keyDown(window, { key: "k", ctrlKey: true });
    fireEvent.change(screen.getByLabelText("Find"), { target: { value: "Ins" } });
    fireEvent.click(screen.getByText("Inspect"));
    expect(select).toHaveBeenCalledOnce();
    await waitFor(() => {
      expect(screen.queryByRole("dialog")).toBeNull();
    });
    fireEvent.keyDown(window, { key: "x" });
    expect(screen.queryByRole("dialog")).toBeNull();
  });
});
describe("advanced forms", () => {
  it("selects a virtualized option with keyboard, filters, and dismisses without replacing the input", () => {
    const change = vi.fn();
    render(<Combobox label="Sensor" options={options} labels={comboLabels} onValueChange={change} />);
    const input = screen.getByRole("combobox");
    fireEvent.focus(input);
    expect(screen.getAllByRole("option")).toHaveLength(2);
    fireEvent.keyDown(input, { key: "End" });
    fireEvent.keyDown(input, { key: "Enter" });
    expect(change).toHaveBeenCalledWith("two");
    expect(input.getAttribute("value")).toBe("Second");
    fireEvent.focus(input);
    fireEvent.keyDown(input, { key: "Home" });
    fireEvent.keyDown(input, { key: "ArrowDown" });
    fireEvent.keyDown(input, { key: "ArrowUp" });
    fireEvent.change(input, { target: { value: "zzz" } });
    expect(screen.getByText("Empty")).toBeTruthy();
    fireEvent.keyDown(input, { key: "Escape" });
    expect(screen.queryByRole("listbox")).toBeNull();
    fireEvent.focus(input);
    fireEvent.change(input, { target: { value: "Fir" } });
    fireEvent.mouseDown(screen.getByRole("option"));
    expect(change).toHaveBeenLastCalledWith("one");
  });
  it("aborts stale remote requests and preserves the newest result", async () => {
    const pending: { signal: AbortSignal; resolve: (value: readonly { value: string; label: string }[]) => void }[] =
      [];
    const load = vi.fn(
      (_query: string, signal: AbortSignal) =>
        new Promise<readonly { value: string; label: string }[]>((resolve) => {
          pending.push({ signal, resolve });
        }),
    );
    render(<Combobox label="Remote" loadOptions={load} labels={comboLabels} />);
    const input = screen.getByRole("combobox");
    fireEvent.focus(input);
    expect(screen.getByText("Loading")).toBeTruthy();
    fireEvent.change(input, { target: { value: "new" } });
    expect(pending[0]?.signal.aborted).toBe(true);
    await act(async () => {
      pending[1]?.resolve([{ value: "new", label: "New source" }]);
      pending[0]?.resolve([{ value: "old", label: "Old source" }]);
      await Promise.resolve();
    });
    expect(screen.getByText("New source")).toBeTruthy();
    expect(screen.queryByText("Old source")).toBeNull();
    fireEvent.blur(input);
    cleanup();
    render(<Combobox label="Broken" loadOptions={() => Promise.reject(new Error("offline"))} labels={comboLabels} />);
    fireEvent.focus(screen.getByRole("combobox"));
    await screen.findByText("Search failed");
  });
  it("updates an uncontrolled multi selection", () => {
    const change = vi.fn();
    render(<MultiSelect label="Priorities" options={options} defaultValue={["one"]} onValueChange={change} />);
    fireEvent.click(screen.getByLabelText("Second"));
    expect(change).toHaveBeenLastCalledWith(["one", "two"]);
    fireEvent.click(screen.getByLabelText("First"));
    expect(change).toHaveBeenLastCalledWith(["two"]);
  });
  it("preserves wall times and validates period ordering with an explicit timezone", () => {
    const change = vi.fn();
    render(
      <DatePeriodPicker
        labels={{ start: "Start", end: "End", timeZone: "Zone", invalid: "Wrong order" }}
        value={{ start: "2024-01-02T12:00", end: "2024-01-01T12:00", timeZone: "UTC" }}
        zones={["UTC", "Europe/Moscow"]}
        onValueChange={change}
      />,
    );
    expect(screen.getByText("Wrong order")).toBeTruthy();
    fireEvent.change(screen.getByLabelText("Zone"), { target: { value: "Europe/Moscow" } });
    expect(change).toHaveBeenCalledWith(expect.objectContaining({ timeZone: "Europe/Moscow" }));
    fireEvent.change(screen.getByLabelText("Start"), { target: { value: "2024-01-03T12:00" } });
    fireEvent.change(screen.getByLabelText("End"), { target: { value: "2024-01-04T12:00" } });
    expect(change).toHaveBeenCalledTimes(3);
  });
  it("focuses the first form error and handles successful and failed submissions", async () => {
    const submit = vi.fn<(value: Record<string, string>) => Promise<void>>().mockResolvedValue(undefined);
    render(
      <ValidatedForm
        fields={[{ name: "reason", label: "Reason", requiredMessage: "Enter reason" }]}
        submitLabel="Save"
        errorLabel="Errors"
        onSubmit={submit}
      />,
    );
    fireEvent.click(screen.getByText("Save"));
    await screen.findByRole("alert");
    expect(document.activeElement).toBe(screen.getByRole("textbox", { name: "Reason" }));
    expect(submit).not.toHaveBeenCalled();
    fireEvent.click(screen.getByRole("link"));
    fireEvent.change(screen.getByRole("textbox", { name: "Reason" }), { target: { value: "Checked source" } });
    fireEvent.click(screen.getByText("Save"));
    await waitFor(() => {
      expect(submit).toHaveBeenCalledWith({ reason: "Checked source" });
    });
    submit.mockRejectedValueOnce(new Error("offline"));
    fireEvent.click(screen.getByText("Save"));
    await screen.findByRole("alert");
  });
  it("reports upload progress and row errors, and exposes transport failures", async () => {
    const upload = vi.fn((_file: File, progress: (value: number) => void) => {
      progress(150);
      return Promise.resolve([{ row: 2, message: "Unknown zone" }]);
    });
    render(<FileUpload label="File" cancelLabel="Cancel" errorLabel="Failed" reportLabel="Rows" upload={upload} />);
    expect(screen.getByRole("progressbar").getAttribute("value")).toBe("0");
    fireEvent.change(screen.getByLabelText("File", { selector: "input" }), {
      target: { files: [new File(["row"], "catalogue.csv")] },
    });
    await screen.findByText("Unknown zone");
    expect(screen.getByRole("progressbar").getAttribute("value")).toBe("100");
    cleanup();
    render(
      <FileUpload
        label="File"
        cancelLabel="Cancel"
        errorLabel="Failed"
        reportLabel="Rows"
        upload={() => Promise.reject(new Error("offline"))}
      />,
    );
    fireEvent.change(screen.getByLabelText("File", { selector: "input" }), {
      target: { files: [new File([], "file.csv")] },
    });
    await screen.findByRole("alert");
  });
  it("aborts an upload without applying late progress or row reports", async () => {
    let finish: ((value: readonly { row: number; message: string }[]) => void) | undefined;
    let signal: AbortSignal | undefined;
    const upload = (_file: File, _progress: (value: number) => void, controller: AbortSignal) => {
      signal = controller;
      return new Promise<readonly { row: number; message: string }[]>((resolve) => {
        finish = resolve;
      });
    };
    render(<FileUpload label="File" cancelLabel="Cancel" errorLabel="Failed" reportLabel="Rows" upload={upload} />);
    fireEvent.change(screen.getByLabelText("File", { selector: "input" }), {
      target: { files: [new File([], "file.csv")] },
    });
    fireEvent.click(screen.getByText("Cancel"));
    expect(signal?.aborted).toBe(true);
    await act(async () => {
      finish?.([{ row: 1, message: "Late report" }]);
      await Promise.resolve();
    });
    expect(screen.queryByText("Late report")).toBeNull();
  });
});
describe("collections", () => {
  it("virtualizes records and retains selected IDs after insertion", () => {
    const bulk = vi.fn();
    function Fixture() {
      const [data, setData] = useState(
        Array.from({ length: 500 }, (_, index) => ({ id: `record-${String(index)}`, name: `Sensor ${String(index)}` })),
      );
      return (
        <>
          <button
            type="button"
            onClick={() => {
              setData((previous) => [{ id: "new", name: "New" }, ...previous]);
            }}
          >
            Insert
          </button>
          <DataTable
            label="Records"
            data={data}
            columns={[
              { accessorKey: "id", header: "ID" },
              { accessorKey: "name", header: "Name" },
            ]}
            labels={{
              search: "Search",
              select: "Select",
              columns: "Columns",
              pin: "Pin",
              bulk: "Apply",
              empty: "Empty",
            }}
            onBulkAction={bulk}
          />
        </>
      );
    }
    render(<Fixture />);
    expect(screen.getAllByRole("row").length).toBeLessThan(20);
    fireEvent.click(screen.getByRole("checkbox", { name: "Select: record-1" }));
    fireEvent.click(screen.getByText("Insert"));
    fireEvent.click(screen.getByRole("button", { name: "Apply (1)" }));
    expect(bulk).toHaveBeenCalledWith(["record-1"]);
    fireEvent.click(screen.getByRole("button", { name: "Pin: Name" }));
    fireEvent.click(screen.getByRole("button", { name: "Pin: Name" }));
    fireEvent.click(screen.getByText("Columns", { selector: "summary" }));
    fireEvent.click(screen.getByLabelText("Name", { selector: "input" }));
    fireEvent.click(screen.getByLabelText("Name", { selector: "input" }));
    fireEvent.click(screen.getByText("ID", { selector: "button" }));
    fireEvent.click(screen.getByText("ID", { selector: "button" }));
    fireEvent.keyDown(screen.getByRole("grid"), { key: "ArrowRight" });
    fireEvent.keyDown(screen.getByRole("grid"), { key: " " });
    fireEvent.change(screen.getByLabelText("Search"), { target: { value: "no match" } });
    expect(screen.getByText("Empty")).toBeTruthy();
  });
  it("loads and navigates a resource tree without selecting on expansion", async () => {
    const select = vi.fn();
    render(
      <Tree
        label="Resources"
        nodes={[{ id: "site", label: "Site", hasChildren: true }]}
        loadingLabel="Loading"
        errorLabel="Failed"
        loadChildren={() =>
          Promise.resolve([{ id: "floor", label: "Floor", children: [{ id: "sensor", label: "Sensor" }] }])
        }
        onSelect={select}
      />,
    );
    const tree = screen.getByRole("tree");
    fireEvent.keyDown(tree, { key: "ArrowRight" });
    await screen.findByText("Floor");
    expect(select).not.toHaveBeenCalled();
    fireEvent.keyDown(tree, { key: "ArrowRight" });
    fireEvent.keyDown(tree, { key: "ArrowRight" });
    fireEvent.keyDown(tree, { key: "End" });
    fireEvent.keyDown(tree, { key: "Enter" });
    expect(select).toHaveBeenCalledWith("sensor");
    fireEvent.keyDown(tree, { key: "ArrowLeft" });
    fireEvent.keyDown(tree, { key: "ArrowLeft" });
    fireEvent.keyDown(tree, { key: "Home" });
    fireEvent.keyDown(tree, { key: " " });
    expect(select).toHaveBeenLastCalledWith("site");
    fireEvent.click(screen.getByRole("button", { name: "Site" }));
    fireEvent.click(screen.getByText("Site", { selector: "div" }));
  });
  it("exposes a tree loading failure and can retry the same node", async () => {
    const load = vi.fn().mockRejectedValueOnce(new Error("offline")).mockResolvedValueOnce([]);
    render(
      <Tree
        label="Resources"
        nodes={[{ id: "site", label: "Site", hasChildren: true }]}
        loadingLabel="Loading"
        errorLabel="Failed"
        loadChildren={load}
      />,
    );
    fireEvent.keyDown(screen.getByRole("tree"), { key: "ArrowRight" });
    await screen.findByText("Failed");
    fireEvent.keyDown(screen.getByRole("tree"), { key: "ArrowLeft" });
    fireEvent.keyDown(screen.getByRole("tree"), { key: "ArrowRight" });
    await waitFor(() => {
      expect(screen.queryByText("Failed")).toBeNull();
    });
    expect(load).toHaveBeenCalledTimes(2);
  });
  it("provides named markers, zoom controls, a legend and table alternatives to charts", () => {
    const select = vi.fn();
    render(
      <>
        <FloorPlan
          label="Plan"
          markers={[
            { id: "one", label: "Sensor", x: 20, y: 30, state: "alarm" },
            { id: "two", label: "Door", x: 70, y: 60, state: "offline" },
            { id: "three", label: "Camera", x: 50, y: 40, state: "normal" },
          ]}
          selected="one"
          onSelect={select}
          labels={{
            zoomIn: "Zoom in",
            zoomOut: "Zoom out",
            reset: "Reset",
            legend: "Legend",
            normal: "Normal",
            alarm: "Alarm",
            offline: "Offline",
          }}
        />
        <TrendChart label="Trend" valueLabel="Count" values={[{ label: "00-10", value: 3 }]} />
      </>,
    );
    fireEvent.click(screen.getAllByRole("button", { name: "Sensor: Alarm" })[0] ?? document.body);
    expect(select).toHaveBeenCalledWith("one");
    fireEvent.click(screen.getByText("Zoom in"));
    expect(screen.getByText("125%")).toBeTruthy();
    fireEvent.click(screen.getByText("Zoom out"));
    fireEvent.click(screen.getByText("Reset"));
    expect(within(screen.getByRole("table")).getByText("3")).toBeTruthy();
  });
});

it("invalidates cached values when immutable records or accessor definitions change", () => {
  const labels = { search: "Search", select: "Select", columns: "Columns", pin: "Pin", bulk: "Apply", empty: "Empty" };
  const records = [
    { id: "__proto__", value: 7 },
    { id: "other", value: 9 },
  ];
  const { rerender } = render(
    <DataTable label="Cached" data={records} columns={[{ accessorKey: "value", header: "Value" }]} labels={labels} />,
  );
  expect(screen.getByRole("gridcell", { name: "7" })).toBeTruthy();
  rerender(
    <DataTable
      label="Cached"
      data={[{ id: "__proto__", value: 11 }, records[1] ?? records[0] ?? { id: "fallback", value: 0 }]}
      columns={[{ accessorKey: "value", header: "Value" }]}
      labels={labels}
    />,
  );
  expect(screen.getByRole("gridcell", { name: "11" })).toBeTruthy();
  rerender(
    <DataTable
      label="Cached"
      data={records}
      columns={[{ id: "value", accessorFn: (record) => record.value * 3, header: "Value" }]}
      labels={labels}
    />,
  );
  expect(screen.getByRole("gridcell", { name: "21" })).toBeTruthy();
  rerender(
    <DataTable
      label="Cached"
      data={records}
      columns={[{ id: "value", accessorFn: (record) => record.value * 4, header: "Value" }]}
      labels={labels}
    />,
  );
  expect(screen.getByRole("gridcell", { name: "28" })).toBeTruthy();
});
