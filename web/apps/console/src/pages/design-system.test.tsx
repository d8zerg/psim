import { act, cleanup, fireEvent, render, screen, within } from "@testing-library/react";
import { afterEach, beforeEach, expect, it, vi } from "vitest";
import { DesignSystemPage } from "./design-system";
import { setPreferences, defaults } from "../ui/preferences";
import { DisplaySettings } from "../ui/settings";
import { playAlert } from "../ui/alert-sound";
vi.mock("../ui/alert-sound", () => ({ playAlert: vi.fn().mockResolvedValue(undefined) }));
beforeEach(() => {
  setPreferences({ ...defaults, language: "en" });
});
afterEach(() => {
  cleanup();
  vi.restoreAllMocks();
  setPreferences(defaults);
});
it("preserves row identity, selection and search through refresh, errors and recovery", async () => {
  render(<DesignSystemPage />);
  const row = await screen.findByTestId("preview-INC-1041");
  fireEvent.click(within(row).getByRole("button"));
  expect(row.getAttribute("data-selected")).toBe("true");
  expect(within(row).getByRole("button").getAttribute("aria-current")).toBe("true");
  fireEvent.change(screen.getByLabelText("Search incidents"), { target: { value: "1041" } });
  expect(screen.queryByTestId("preview-INC-1042")).toBeNull();
  fireEvent.click(screen.getByRole("button", { name: "Refresh data" }));
  expect(screen.getByTestId("preview-INC-1041")).toBe(row);
  await screen.findByText("Updating data. Current rows remain available.");
  await vi.waitFor(() => {
    expect(screen.getByRole("button", { name: "Refresh data" }).hasAttribute("disabled")).toBe(false);
  });
  fireEvent.click(screen.getByRole("button", { name: "Simulate connection loss" }));
  await screen.findByText("Could not load incidents. Check your connection and try again.");
  expect(screen.getByTestId("preview-INC-1041")).toBe(row);
  fireEvent.click(screen.getByRole("button", { name: "Restore connection" }));
  await vi.waitFor(() => {
    expect(screen.queryByText("Could not load incidents. Check your connection and try again.")).toBeNull();
  });
  fireEvent.change(screen.getByLabelText("Search incidents"), { target: { value: "" } });
  fireEvent.change(screen.getByLabelText("Priority"), { target: { value: "P3" } });
  expect(screen.getByTestId("preview-INC-1038")).toBeDefined();
  fireEvent.change(screen.getByLabelText("Search incidents"), { target: { value: "missing" } });
  expect(await screen.findByText("No matching incidents")).toBeDefined();
});
it("supports response confirmation, responsibility transfer and report export", async () => {
  render(<DesignSystemPage />);
  await screen.findByTestId("preview-INC-1042");
  fireEvent.click(screen.getByRole("button", { name: "Acknowledge incident" }));
  expect(screen.getByText("Incident acknowledged in the layout.")).toBeDefined();
  expect(screen.getByRole("button", { name: "Send device command" }).hasAttribute("disabled")).toBe(true);
  fireEvent.change(screen.getByLabelText(/^Reason/), { target: { value: "Check entrance" } });
  fireEvent.click(screen.getByRole("button", { name: "Send device command" }));
  expect(screen.getByRole("alertdialog").textContent).toContain("Check entrance");
  fireEvent.click(screen.getByRole("button", { name: "Cancel" }));
  fireEvent.click(screen.getByRole("button", { name: "Send device command" }));
  fireEvent.click(screen.getByRole("button", { name: "Confirm command" }));
  expect(screen.getAllByText("Approval requested from another supervisor. Command has not been sent.").length).toBe(2);
  fireEvent.change(screen.getByLabelText("Zone name"), { target: { value: "Loading bay" } });
  fireEvent.click(screen.getByRole("button", { name: "Create zone" }));
  fireEvent.click(screen.getByRole("button", { name: "Preview import report" }));
  expect(screen.getByText(/50,000 rows/)).toBeDefined();
  expect(screen.getByText(/Zone added to the layout/)).toBeDefined();
  fireEvent.click(screen.getByRole("button", { name: "Request transfer" }));
  expect(screen.getByText(/Waiting for the receiving/)).toBeDefined();
  const create = vi.fn().mockReturnValue("blob:test");
  const revoke = vi.fn();
  vi.stubGlobal("URL", { createObjectURL: create, revokeObjectURL: revoke });
  vi.spyOn(HTMLAnchorElement.prototype, "click").mockImplementation(() => undefined);
  fireEvent.click(screen.getByRole("button", { name: "Export sample CSV" }));
  await vi.waitFor(() => {
    expect(revoke).toHaveBeenCalledWith("blob:test");
  });
  vi.unstubAllGlobals();
});
it("localizes display controls and uses sound only after explicit opt-in", async () => {
  render(
    <>
      <DisplaySettings design />
      <DesignSystemPage />
    </>,
  );
  await screen.findByTestId("preview-INC-1042");
  fireEvent.click(screen.getByRole("button", { name: "Preview" }));
  expect(playAlert).not.toHaveBeenCalled();
  fireEvent.click(screen.getByRole("switch", { name: "Sound alerts" }));
  await act(async () => {
    await Promise.resolve();
    fireEvent.click(screen.getByRole("button", { name: "Preview" }));
  });
  expect(playAlert).toHaveBeenCalled();
  expect(screen.getByRole("alert").textContent).toContain("New P1 incident");
  fireEvent.click(screen.getByRole("link", { name: "Open incident" }));
  vi.mocked(playAlert).mockRejectedValue(new Error("blocked"));
  await act(async () => {
    await Promise.resolve();
    fireEvent.click(screen.getByRole("button", { name: "Test alert sound" }));
  });
  expect(screen.getByText(/Sound is unavailable/)).toBeDefined();
  fireEvent.change(screen.getByLabelText("Theme"), { target: { value: "light" } });
  fireEvent.change(screen.getByLabelText("Density"), { target: { value: "compact" } });
  fireEvent.change(screen.getByLabelText("Language"), { target: { value: "ru" } });
  expect(await screen.findByRole("heading", { name: "Дизайн-система" })).toBeDefined();
  expect(document.documentElement.dataset.density).toBe("compact");
  fireEvent.change(screen.getByLabelText("Тема"), { target: { value: "dark" } });
  fireEvent.change(screen.getByLabelText("Плотность"), { target: { value: "comfortable" } });
  fireEvent.change(screen.getByLabelText("Язык"), { target: { value: "en" } });
  await screen.findByRole("heading", { name: "Design system" });
  fireEvent.click(screen.getByRole("switch", { name: "Sound alerts" }));
  expect(screen.getByRole<HTMLInputElement>("switch", { name: "Sound alerts" }).checked).toBe(false);
});
