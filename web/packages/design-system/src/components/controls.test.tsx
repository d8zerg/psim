import { cleanup, fireEvent, render, screen } from "@testing-library/react";
import { afterEach, expect, it, vi } from "vitest";
import { createRef } from "react";
import { Button } from "./button";
import { Icon, domainIcons } from "./icons";
import {
  Avatar,
  ButtonGroup,
  Checkbox,
  DescriptionList,
  IconButton,
  Kbd,
  NumberInput,
  Progress,
  RadioGroup,
  Skeleton,
  Slider,
  Spinner,
  Stat,
  Stepper,
  Tag,
  Textarea,
  Timeline,
} from "./controls";
afterEach(cleanup);
it("names icons and controls, associates validation, forwards ref and supports native keyboard inputs", () => {
  const change = vi.fn();
  const ref = createRef<HTMLTextAreaElement>();
  render(
    <>
      <Textarea label="Reason" error="Required" hint="Explain" ref={ref} />
      <Textarea label="Note" hint="Details" />
      <Textarea label="Other" />
      <Checkbox label="Include" defaultChecked />
      <NumberInput label="Limit" min={1} max={10} defaultValue={4} />
      <Slider label="Volume" min={0} max={100} defaultValue={40} />
      <RadioGroup
        label="Priority"
        options={[
          { value: "p1", label: "P1" },
          { value: "p2", label: "P2" },
        ]}
        defaultValue="p2"
        onValueChange={change}
      />
      <IconButton label="Open camera" icon={domainIcons.camera} />
      <Icon icon={domainIcons.alarm} label="Alarm" />
      <Icon icon={domainIcons.door} decorative />
    </>,
  );
  expect(ref.current).toBe(screen.getByLabelText("Reason"));
  expect(ref.current?.getAttribute("aria-invalid")).toBe("true");
  expect(document.getElementById(ref.current?.getAttribute("aria-describedby") ?? "")?.textContent).toBe("Required");
  expect(screen.getByRole("slider", { name: "Volume" })).toBeDefined();
  expect(screen.getByRole("spinbutton", { name: "Limit" })).toBeDefined();
  expect(screen.getByRole("checkbox", { name: "Include" })).toBeDefined();
  expect(screen.getByRole("button", { name: "Open camera" })).toBeDefined();
  expect(screen.getByRole("img", { name: "Alarm" })).toBeDefined();
  fireEvent.click(screen.getByRole("radio", { name: "P1" }));
  expect(change).toHaveBeenCalledWith("p1");
});
it("keeps controlled radio selection with the owner and renders indeterminate progress and summaries", () => {
  const { rerender } = render(
    <RadioGroup
      label="Mode"
      value="a"
      options={[
        { value: "a", label: "A" },
        { value: "b", label: "B" },
      ]}
    />,
  );
  fireEvent.click(screen.getByLabelText("B"));
  expect(screen.getByLabelText<HTMLInputElement>("A").checked).toBe(true);
  rerender(
    <>
      <Progress label="Import" value={40} />
      <Progress label="Preparing" />
      <Spinner label="Loading" />
      <Skeleton label="Rows" />
      <Skeleton label="Card" variant="card" />
      <Skeleton label="Text" variant="text" />
      <ButtonGroup label="Actions">
        <Button>Save</Button>
      </ButtonGroup>
      <Kbd>Ctrl K</Kbd>
      <Tag>Local</Tag>
      <Avatar name="Ada Lovelace" />
      <DescriptionList items={[{ label: "Owner", value: "Ada" }]} />
      <Stat label="Alarms" value={3} description="This hour" />
      <Stat label="Devices" value={8} />
      <Timeline label="History" items={[{ id: "a", time: "12:00", text: "Created" }]} />
      <Stepper label="Procedure" steps={["Check", "Approve", "Send"]} current={1} />
    </>,
  );
  expect(screen.getAllByRole("progressbar")).toHaveLength(2);
  expect(screen.getByRole("img", { name: "Ada Lovelace" }).textContent).toBe("AL");
  expect(screen.getByRole("list", { name: "Procedure" }).querySelector('[aria-current="step"]')?.textContent).toContain(
    "Approve",
  );
});
it("blocks repeated actions while loading, including slotted links", () => {
  const clicked = vi.fn();
  const capture = vi.fn();
  const { rerender } = render(
    <Button loading onClick={clicked}>
      Save
    </Button>,
  );
  fireEvent.click(screen.getByRole("button"));
  expect(clicked).not.toHaveBeenCalled();
  expect(screen.getByRole("button").getAttribute("aria-busy")).toBe("true");
  rerender(
    <Button loading asChild onClickCapture={capture}>
      <a href="/incidents" onClick={clicked}>
        Send
      </a>
    </Button>,
  );
  fireEvent.click(screen.getByRole("link"));
  expect(clicked).not.toHaveBeenCalled();
  expect(capture).not.toHaveBeenCalled();
  rerender(
    <Button asChild onClickCapture={capture}>
      <a href="/incidents" onClick={clicked}>
        Send
      </a>
    </Button>,
  );
  fireEvent.click(screen.getByRole("link"));
  expect(clicked).toHaveBeenCalledOnce();
  expect(capture).toHaveBeenCalledOnce();
});
// @ts-expect-error An unnamed semantic icon is forbidden.
const unnamedIcon = <Icon icon={domainIcons.camera} />;
// @ts-expect-error Icon-only actions require their own accessible name.
const unnamedButton = <IconButton icon={domainIcons.camera} />;
it("keeps compile-time accessible-name contracts", () => {
  expect(unnamedIcon).toBeDefined();
  expect(unnamedButton).toBeDefined();
});
