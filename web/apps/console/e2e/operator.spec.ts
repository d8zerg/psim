import { expect, test } from "@playwright/test";
import AxeBuilder from "@axe-core/playwright";
import { operatorFixture } from "./operator-fixture";
test("@gate operator accepts an incident, completes checklist, sends previewed command and receives SLA escalation", async ({
  page,
}) => {
  const f = await operatorFixture(page);
  await page.getByRole("button", { name: "Open incident: Operational incident" }).click();
  await page.getByRole("button", { name: "Acknowledge incident", exact: true }).click();
  await page.getByRole("button", { name: "Start work", exact: true }).click();
  await page.getByLabel("Verify safety (Mandatory step)").check();
  await page.getByRole("button", { name: "Complete step", exact: true }).click();
  await page.getByLabel("Device", { exact: true }).fill("00000000-0000-4000-8000-000000000002");
  await page.getByLabel("Capability", { exact: true }).selectOption("arm");
  await page.getByLabel("Reason: Send device command").fill("Area verified");
  await page.getByRole("button", { name: "Preview command", exact: true }).click();
  await expect(page.getByText("Consequence: System arms")).toBeVisible();
  await page.getByRole("button", { name: "Confirm command", exact: true }).click();
  await expect(page.getByText("Executed", { exact: true })).toBeVisible();
  const dispatch = f.writes.find((write) => write.path === "/v1/commands");
  expect(dispatch?.key).toBe(dispatch?.body.commandId);
  expect(dispatch?.body.previewFingerprint).toBe("preview");
  f.escalate();
  await expect(page.getByTestId("escalation-level")).toHaveText("2");
  await expect(page.getByText(/SLA escalation/u)).toBeVisible();
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: "/repo/build/operator-desktop.png", fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByLabel("Language").selectOption("ru");
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: "/repo/build/operator-mobile.png", fullPage: true });
});
test("@gate operator freezes writes during connection loss and restores without replacing the selected card", async ({
  page,
}) => {
  const f = await operatorFixture(page);
  await page.getByRole("button", { name: "Open incident: Operational incident" }).click();
  await expect(page.getByRole("button", { name: "Acknowledge incident", exact: true })).toBeEnabled();
  f.disconnect();
  await expect(page.getByTestId("connection-status")).not.toHaveAttribute("data-status", "open");
  await expect(page.getByRole("button", { name: "Acknowledge incident", exact: true })).toBeDisabled();
  f.resume();
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open");
  await page.getByLabel("Operating mode").selectOption("training");
  await expect(page.getByRole("button", { name: "Acknowledge incident", exact: true })).toBeDisabled();
  await page.getByLabel("Operating mode").selectOption("normal");
  await expect(page.getByRole("button", { name: "Acknowledge incident", exact: true })).toBeEnabled();
});
