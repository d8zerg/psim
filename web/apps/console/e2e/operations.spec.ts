import AxeBuilder from "@axe-core/playwright";
import { expect, test } from "@playwright/test";
test("@gate @a11y operational flood, history and recovery preserve selected records", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem(
      "psim.ui",
      JSON.stringify({ language: "en", theme: "dark", density: "comfortable", mode: "normal" }),
    );
  });
  await page.goto("/design-system.html");
  await page.getByRole("button", { name: "Open operational simulation", exact: true }).click();
  const operations = page.getByTestId("operations");
  await expect(operations).toBeVisible();
  const grid = operations.getByRole("grid");
  const selected = grid.locator('[data-id="SIM-1-0"]');
  const retained = await selected.elementHandle();
  await selected.getByRole("gridcell").nth(1).focus();
  await page.keyboard.press("Space");
  await operations.getByRole("button", { name: "Generate 500 alarms", exact: true }).click();
  await expect(grid).toHaveAttribute("aria-rowcount", "502");
  expect(await grid.getByRole("row").count()).toBeLessThan(20);
  expect(await retained.evaluate((node) => node === document.querySelector('[data-id="SIM-1-0"]'))).toBe(true);
  await expect(selected).toHaveAttribute("aria-selected", "true");
  await operations.getByLabel("History position", { exact: true }).fill("1");
  await expect(operations.getByRole("button", { name: "Acknowledge", exact: true })).toBeDisabled();
  await operations.getByRole("button", { name: "Return to live state", exact: true }).click();
  await operations.getByRole("button", { name: "Disconnect channel", exact: true }).click();
  await operations.getByRole("button", { name: "Next minute", exact: true }).click();
  await operations.getByRole("button", { name: "Recover channel", exact: true }).click();
  await expect(operations.getByText(/Recovery briefing: events received: 1/u)).toBeVisible();
  expect((await new AxeBuilder({ page }).include('[data-testid="operations"]').analyze()).violations).toEqual([]);
  await page.screenshot({ path: "../../../build/web-design-system/operations.png", fullPage: true });
});

test("@gate @a11y mobile approval prevents self-review and restores logical focus", async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.addInitScript(() => {
    localStorage.setItem(
      "psim.ui",
      JSON.stringify({ language: "ru", theme: "contrast", density: "compact", mode: "normal" }),
    );
  });
  await page.goto("/design-system.html");
  await page.getByRole("button", { name: "Открыть оперативную симуляцию", exact: true }).click();
  const operations = page.getByTestId("operations");
  await operations.getByRole("textbox", { name: "Основание аудита", exact: true }).fill("Проверить вход");
  await operations.getByRole("button", { name: "Запросить подтверждение команды", exact: true }).click();
  const trigger = operations.getByRole("button", { name: "Подтвердить вторым старшим смены", exact: true });
  await trigger.click();
  const panel = page.getByRole("dialog");
  await expect(panel).toBeVisible();
  await panel.getByRole("textbox", { name: "Идентификатор подтверждающего", exact: true }).fill("operator-a");
  const approve = panel.getByRole("button", { name: "Подтвердить вторым старшим смены", exact: true });
  await expect(approve).toBeDisabled();
  await panel.getByRole("textbox", { name: "Идентификатор подтверждающего", exact: true }).fill("supervisor-b");
  await approve.click();
  expect((await new AxeBuilder({ page }).include('[role="dialog"]').analyze()).violations).toEqual([]);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await panel.screenshot({ path: "../../../build/web-design-system/approval-mobile.png" });
  await page.keyboard.press("Escape");
  await expect(trigger).toBeFocused();
  await operations.getByRole("button", { name: "Отправить команду в симуляции", exact: true }).click();
  const confirmation = page.getByRole("alertdialog");
  await confirmation.getByRole("button", { name: "Подтвердить команду", exact: true }).click();
  await expect(operations.getByRole("button", { name: "Сообщить об исполнении", exact: true })).toBeFocused();
});
