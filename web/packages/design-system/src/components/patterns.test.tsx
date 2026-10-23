import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { ConfirmAction } from "./confirm-action";
import { Badge, Card, DataRegion, EmptyState, Field, Notice, SelectField, Switch, TableContainer } from "./patterns";
afterEach(cleanup);
const labels = { loading: "Loading", refreshing: "Refreshing", stale: "Stale" };
it("keeps loaded content mounted during refresh and failure", () => {
  const view = render(
    <DataRegion loading hasData={false} labels={labels}>
      <button>Record</button>
    </DataRegion>,
  );
  expect(screen.queryByRole("button")).toBeNull();
  view.rerender(
    <DataRegion loading={false} hasData labels={labels}>
      <button>Record</button>
    </DataRegion>,
  );
  const row = screen.getByRole("button");
  row.focus();
  view.rerender(
    <DataRegion loading hasData labels={labels}>
      <button>Record</button>
    </DataRegion>,
  );
  expect(screen.getByRole("button")).toBe(row);
  expect(document.activeElement).toBe(row);
  expect(screen.getByRole("status").textContent).toBe("Refreshing");
  view.rerender(
    <DataRegion loading={false} hasData error="Unavailable" labels={labels}>
      <button>Record</button>
    </DataRegion>,
  );
  expect(screen.getByRole("button")).toBe(row);
  expect(screen.getByRole("alert").textContent).toBe("Unavailable");
  expect(screen.getByRole("status").textContent).toBe("Stale");
  expect(screen.getByRole("status").dataset.state).toBe("stale");
});
it("associates labels, hints and errors with inputs and keeps caller references", () => {
  const view = render(
    <>
      <span id="external">Shown in the summary</span>
      <Field label="Name" hint="Required" required aria-describedby="external" />
    </>,
  );
  const input = screen.getByLabelText(/Name/);
  expect(input.getAttribute("aria-describedby")).toBe(`external ${screen.getByText("Required").id}`);
  expect(input.hasAttribute("required")).toBe(true);
  view.rerender(<Field id="reason" label="Name" error="Invalid" />);
  expect(screen.getByLabelText("Name").id).toBe("reason");
  expect(screen.getByLabelText("Name").getAttribute("aria-invalid")).toBe("true");
  expect(screen.getByLabelText("Name").getAttribute("aria-describedby")).toBe("reason-help");
  view.rerender(<Field label="Name" />);
  expect(screen.getByLabelText("Name").hasAttribute("aria-describedby")).toBe(false);
  view.rerender(
    <SelectField label="State" error="Choose a state">
      <option>New</option>
    </SelectField>,
  );
  expect(screen.getByLabelText("State").getAttribute("aria-invalid")).toBe("true");
  expect(screen.getByText("Choose a state").dataset.error).toBe("true");
});
it("renders section structure, semantic states and static notices", () => {
  render(
    <Card id="feed" title="Feed" eyebrow="Operator" description="Active incidents" actions={<button>Export</button>}>
      <Badge tone="danger" emphasis="solid">
        P1
      </Badge>
      <Notice tone="warning" title="Stale" live={false} action={<button>Retry</button>}>
        Last known data
      </Notice>
      <Notice tone="success">Saved</Notice>
      <EmptyState title="Empty" description="Create a record" headingLevel={4} />
    </Card>,
  );
  expect(screen.getByRole("region", { name: "Feed" }).id).toBe("feed");
  expect(screen.getByRole("heading", { level: 4, name: "Empty" })).toBeDefined();
  expect(screen.getByText("P1").dataset.emphasis).toBe("solid");
  expect(screen.getAllByRole("status").map((node) => node.textContent)).toEqual(["Saved"]);
  expect(screen.getByText("Stale").closest("[data-tone]")?.getAttribute("role")).toBeNull();
});
it("toggles a switch with its label", () => {
  const change = vi.fn();
  const view = render(<Switch label="Sound" checked={false} onCheckedChange={change} />);
  fireEvent.click(screen.getByRole("switch", { name: "Sound" }));
  expect(change).toHaveBeenCalledWith(true);
  view.rerender(<Switch label="Sound" checked disabled onCheckedChange={change} />);
  expect(screen.getByRole("switch", { name: "Sound" }).hasAttribute("disabled")).toBe(true);
});
it("requires explicit confirmation and allows cancellation", () => {
  const confirm = vi.fn();
  const props = {
    trigger: "Send",
    title: "Confirm command",
    description: "Unlock door",
    details: <p>Main entrance</p>,
    confirm: "Confirm",
    cancel: "Cancel",
    onConfirm: confirm,
  };
  const view = render(<ConfirmAction {...props} disabled />);
  const trigger = screen.getByRole("button", { name: "Send" });
  expect(trigger.hasAttribute("disabled")).toBe(true);
  view.rerender(<ConfirmAction {...props} variant="primary" />);
  expect(screen.getByRole("button", { name: "Send" })).toBe(trigger);
  fireEvent.click(trigger);
  expect(screen.getByRole("alertdialog").textContent).toContain("Main entrance");
  fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
  expect(confirm).not.toHaveBeenCalled();
  fireEvent.click(trigger);
  fireEvent.click(screen.getByRole("button", { name: "Confirm" }));
  expect(confirm).toHaveBeenCalledOnce();
});
it("makes a scrollable table container reachable from the keyboard", () => {
  render(
    <TableContainer label="Incidents" className="mt-4" data-testid="wrap">
      <table />
    </TableContainer>,
  );
  const region = screen.getByRole("region", { name: "Incidents" });
  expect(region.tabIndex).toBe(0);
  expect(region.className).toContain("mt-4");
});
