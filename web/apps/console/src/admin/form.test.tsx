import { useState } from "react";
import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, beforeEach, expect, it } from "vitest";
import { FormFields } from "./form";
import { type Node, type Value, type RecordValue } from "./model";
import { setPreferences } from "../ui/preferences";
import { enumLabel, label } from "./strings";
afterEach(cleanup);
beforeEach(() => {
  setPreferences({ language: "en" });
});
function Harness({ schema, initial = {} }: { readonly schema: Node; readonly initial?: Value }) {
  const [value, setValue] = useState<Value | undefined>(initial);
  return (
    <>
      <FormFields node={schema} value={value} change={setValue} name="Form" />
      <output data-testid="value">{JSON.stringify(value)}</output>
    </>
  );
}
function current() {
  return JSON.parse(screen.getByTestId("value").textContent) as RecordValue;
}
it("replaces mutually exclusive rule specifications without leaking the old variant", () => {
  render(<Harness schema={{ ref: "psim.processing.v1.CorrelationRule" }} initial={{ single: {}, name: "Rule" }} />);
  fireEvent.change(screen.getByLabelText("Form: Type"), { target: { value: "threshold" } });
  fireEvent.change(screen.getByLabelText("Count"), { target: { value: "3" } });
  expect(current()).toMatchObject({ threshold: { count: 3 }, name: "Rule" });
  expect(current().single).toBeUndefined();
  fireEvent.change(screen.getByLabelText("Count"), { target: { value: "" } });
  expect(current().threshold).toEqual({});
  fireEvent.change(screen.getByLabelText("Form: Type"), { target: { value: "" } });
  expect(current().threshold).toBeUndefined();
});
it("edits nested response checklists and removes only the selected entry", () => {
  render(<Harness schema={{ ref: "psim.response.v1.Step" }} initial={{ instruction: { text: "Read" } }} />);
  fireEvent.change(screen.getByLabelText("Form: Type"), { target: { value: "checklist" } });
  fireEvent.click(screen.getByRole("button", { name: "Configure: Items" }));
  fireEvent.click(screen.getByRole("button", { name: "Add: Items" }));
  fireEvent.change(screen.getByLabelText("Item identifier"), { target: { value: "verify" } });
  fireEvent.change(screen.getByLabelText("Text"), { target: { value: "Verify zone" } });
  fireEvent.click(screen.getByLabelText("Required"));
  expect(current().checklist).toEqual({ items: [{ itemId: "verify", text: "Verify zone", required: true }] });
  expect(current().instruction).toBeUndefined();
  fireEvent.click(screen.getByRole("button", { name: "Remove: Items 1" }));
  expect(current().checklist).toEqual({ items: [] });
  fireEvent.click(screen.getByRole("button", { name: "Remove: Items" }));
  expect(current().checklist).toEqual({});
});
it("edits typed lists and dictionaries, preserving unique keys", () => {
  render(
    <Harness
      schema={{
        type: "object",
        properties: {
          types: { type: "array", items: { type: "string" } },
          attributeMap: { type: "object", additionalProperties: { type: "string" } },
        },
      }}
      initial={{ types: ["alarm"], attributeMap: { vendor: "normalized", other: "saved" } }}
    />,
  );
  fireEvent.change(screen.getByLabelText("Taxonomy types 1"), { target: { value: "intrusion.*" } });
  const key = screen.getByLabelText("Attribute mapping: Identifier 1");
  fireEvent.blur(key, { target: { value: "other" } });
  expect(current().attributeMap).toEqual({ vendor: "normalized", other: "saved" });
  fireEvent.blur(key, { target: { value: "raw" } });
  fireEvent.change(screen.getByLabelText("Attribute mapping 1"), { target: { value: "state" } });
  expect(current()).toMatchObject({ types: ["intrusion.*"], attributeMap: { raw: "state", other: "saved" } });
  fireEvent.click(screen.getByRole("button", { name: "Add: Attribute mapping" }));
  fireEvent.click(screen.getByRole("button", { name: "Add: Attribute mapping" }));
  expect(current().attributeMap).toMatchObject({ key: "", key_: "" });
  fireEvent.click(screen.getByRole("button", { name: "Remove: Attribute mapping 1" }));
  expect(current().attributeMap).not.toHaveProperty("raw");
});
it("selects recipients and enums, supports optional numeric values and localized labels", () => {
  render(<Harness schema={{ ref: "psim.response.v1.Recipient" }} />);
  fireEvent.change(screen.getByLabelText("Form: Type"), { target: { value: "currentDuty" } });
  expect(current()).toEqual({ currentDuty: true });
  cleanup();
  render(<Harness schema={{ ref: "psim.catalog.v1.Location" }} />);
  fireEvent.change(screen.getByLabelText("Kind"), { target: { value: "LOCATION_KIND_FLOOR" } });
  expect(current().kind).toBe("LOCATION_KIND_FLOOR");
  fireEvent.change(screen.getByLabelText("Kind"), { target: { value: "" } });
  expect(current().kind).toBeUndefined();
  expect(enumLabel("LOCATION_KIND_SITE", "ru")).toBe("Площадка");
  expect(enumLabel("PRIORITY_P2", "en")).toBe("P2");
  expect(label("unknownField", "en")).toBe("unknown Field");
});
