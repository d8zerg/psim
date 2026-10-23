import AxeBuilder from "@axe-core/playwright";
import { expect, test, type Page } from "@playwright/test";
const groups = ["actions", "forms", "status", "data"];
async function openStory(page: Page, group: string, settings: Record<string, string> = {}) {
  const query = new URLSearchParams({ story: `${group}--states`, mode: "preview", ...settings });
  await page.goto(`/?${query.toString()}`);
  await expect(page.locator("html")).toHaveAttribute("data-storyloaded");
  await expect(page.locator("html")).toHaveAttribute("data-mode", "preview");
  await expect(page.locator("html")).toHaveAttribute("data-theme", settings["psim-theme"] ?? "dark");
  await expect(page.locator("#catalogue")).toHaveAttribute("data-mode", settings["psim-mode"] ?? "normal");
  await expect(page.locator("#catalogue-content")).not.toBeEmpty();
  await page.evaluate(() => document.fonts.ready);
}
for (const group of groups) {
  for (const theme of ["dark", "light", "contrast", "night"]) {
    for (const density of ["comfortable", "compact", "wall"]) {
      for (const language of ["ru", "en"]) {
        const settings = { "psim-theme": theme, "psim-density": density, "psim-language": language };
        test(`@gate @a11y ${group}: ${theme}, ${density}, ${language}`, async ({ page }) => {
          await openStory(page, group, settings);
          for (const mode of ["normal", "crisis", "training", "wall"]) {
            await test.step(mode, async () => {
              await page.getByLabel("Mode", { exact: true }).selectOption(mode);
              await expect(page.locator("#catalogue")).toHaveAttribute("data-mode", mode);
              expect(
                (
                  await new AxeBuilder({ page })
                    .include("#catalogue")
                    .withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22a", "wcag22aa"])
                    .analyze()
                ).violations,
              ).toEqual([]);
              expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
            });
          }
        });
        test(`@gate @visual ${group}: ${theme}, ${density}, ${language}`, async ({ page }) => {
          await openStory(page, group, settings);
          await expect(page).toHaveScreenshot(`${group}-${theme}-${density}-${language}.png`, {
            animations: "disabled",
            fullPage: true,
          });
        });
      }
    }
  }
  for (const language of ["ru", "en"]) {
    test(`@gate @visual pseudolocalization ${group}, ${language}`, async ({ page }) => {
      await page.setViewportSize({ width: 600, height: 1000 });
      await openStory(page, group, { "psim-language": language, "psim-pseudo": "true" });
      await expect(page).toHaveScreenshot(`${group}-pseudo-${language}.png`, {
        animations: "disabled",
        fullPage: true,
      });
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
      if (group === "forms") await expect(page.getByRole("textbox").first()).toHaveValue("SENSOR-42");
    });
  }
}
test("@gate covers every story and its declared components", async ({ page, request }) => {
  const response = await request.get("/meta.json");
  expect(response.ok()).toBe(true);
  const meta = (await response.json()) as { stories: Record<string, { meta: { components: string[] } }> };
  expect(Object.keys(meta.stories).sort()).toEqual(groups.map((group) => `${group}--states`).sort());
  for (const group of groups) {
    await openStory(page, group);
    for (const name of meta.stories[`${group}--states`]?.meta.components ?? []) {
      await expect(page.locator(`[data-component="${name}"]`)).toHaveCount(1);
    }
  }
});
test("@gate @a11y requires confirmation and preserves dialog keyboard behavior", async ({ page }) => {
  await openStory(page, "actions");
  const trigger = page.getByRole("button", { name: "Send command", exact: true });
  await expect(trigger).toHaveCSS("display", "flex");
  expect(await trigger.evaluate((node) => Number.parseFloat(getComputedStyle(node).minHeight))).toBeGreaterThanOrEqual(
    40,
  );
  await expect(page.locator("#catalogue")).toHaveCSS("font-family", /IBM Plex Sans/u);
  await trigger.click();
  const dialog = page.getByRole("alertdialog");
  await expect(dialog.getByRole("button", { name: "Cancel", exact: true })).toBeFocused();
  await expect(page.getByTestId("command-result")).toHaveText("Not sent");
  await page.keyboard.press("Shift+Tab");
  await expect(dialog.getByRole("button", { name: "Confirm", exact: true })).toBeFocused();
  await page.keyboard.press("Escape");
  await expect(trigger).toBeFocused();
  await trigger.click();
  await dialog.getByRole("button", { name: "Confirm", exact: true }).click();
  await expect(page.getByTestId("command-result")).toHaveText("Sent");
  await page.getByRole("button", { name: "Acknowledge", exact: true }).click();
  await expect(page.getByTestId("action-count")).toHaveText("1");
});
test("@gate @a11y supports native form keyboard patterns and forced colors", async ({ page }) => {
  await openStory(page, "forms");
  const radio = page.getByRole("radio", { name: "Operator", exact: true });
  await radio.focus();
  await page.keyboard.press("ArrowDown");
  await expect(page.getByRole("radio", { name: "Supervisor", exact: true })).toBeChecked();
  const checkbox = page.getByRole("checkbox", { name: "Include resolved incidents", exact: true });
  await checkbox.focus();
  await page.keyboard.press("Space");
  await expect(checkbox).not.toBeChecked();
  const sound = page.getByRole("switch", { name: "Sound alerts", exact: true });
  await sound.focus();
  await page.keyboard.press("Space");
  await expect(sound).toBeChecked();
  await page.emulateMedia({ forcedColors: "active" });
  expect((await new AxeBuilder({ page }).include("#catalogue").analyze()).violations).toEqual([]);
});

test("@gate @a11y composite overlay keyboard contracts", async ({ page }) => {
  await openStory(page, "actions");
  for (const label of ["Open dialog", "Open sheet"]) {
    const trigger = page.getByRole("button", { name: label, exact: true });
    await trigger.click();
    const dialog = page.getByRole("dialog");
    await expect(dialog).toBeVisible();
    expect((await new AxeBuilder({ page }).include('[role="dialog"]').analyze()).violations).toEqual([]);
    await page.keyboard.press("Tab");
    await expect(dialog.getByRole("button", { name: "Close", exact: true })).toBeFocused();
    await page.keyboard.press("Escape");
    await expect(trigger).toBeFocused();
  }
  await page.getByRole("button", { name: "Source details", exact: true }).click();
  await expect(page.getByRole("dialog")).toContainText("Kafka");
  await page.keyboard.press("Escape");
  await expect(page.getByRole("button", { name: "Source details", exact: true })).toBeFocused();
  const menu = page.getByRole("button", { name: "Open menu", exact: true });
  await menu.focus();
  await page.keyboard.press("ArrowDown");
  await expect(page.getByRole("menuitem", { name: "Inspect", exact: true })).toBeFocused();
  await page.keyboard.press("End");
  await expect(page.getByRole("menuitem", { name: "Inspect", exact: true })).toBeFocused();
  await page.keyboard.press("Enter");
  await expect(page.getByTestId("menu-result")).toHaveText("inspect");
  await expect(menu).toBeFocused();
  const context = page.getByLabel("Incident context", { exact: true });
  await context.focus();
  await page.keyboard.press("Shift+F10");
  await page.keyboard.press("ArrowDown");
  await expect(page.getByRole("menuitem", { name: "Inspect", exact: true })).toBeFocused();
  await page.keyboard.press("Escape");
  const summary = page.getByRole("tab", { name: "Summary", exact: true });
  await summary.focus();
  await page.keyboard.press("ArrowRight");
  await expect(page.getByRole("tab", { name: "History", exact: true })).toBeFocused();
  await expect(page.getByRole("tabpanel")).toHaveText("SENSOR-42");
  await page.keyboard.press("Control+k");
  await expect(page.getByRole("dialog")).toBeVisible();
  await expect(page.getByRole("textbox", { name: "Find action", exact: true })).toBeFocused();
  await page.keyboard.press("Escape");
});

test("@gate @a11y virtual combobox and validated form keyboard contracts", async ({ page }) => {
  await openStory(page, "forms");
  const combo = page.getByRole("combobox", { name: "Search sensor", exact: true });
  await combo.focus();
  await expect(page.getByRole("listbox").getByRole("option").first()).toBeVisible();
  expect(await page.getByRole("listbox").getByRole("option").count()).toBeLessThan(20);
  await page.keyboard.press("End");
  await expect(page.getByRole("option", { name: "SENSOR-999", exact: true })).toBeVisible();
  await page.keyboard.press("Enter");
  await expect(combo).toHaveValue("SENSOR-999");
  await expect(combo).toBeFocused();
  await combo.fill("SENSOR-42");
  expect((await new AxeBuilder({ page }).include("#catalogue").analyze()).violations).toEqual([]);
  await page.keyboard.press("Escape");
  await page.getByRole("button", { name: "Save reason", exact: true }).click();
  const reason = page.getByRole("textbox", { name: "Audit reason", exact: true });
  await expect(reason).toBeFocused();
  await expect(reason).toHaveAttribute("aria-invalid", "true");
  await reason.fill("Verified by operator");
  await page.getByRole("button", { name: "Save reason", exact: true }).click();
  await expect(page.getByTestId("form-result")).toHaveText("Verified by operator");
  await page
    .getByLabel("Import catalogue", { exact: true })
    .and(page.locator("input"))
    .setInputFiles({ name: "devices.csv", mimeType: "text/csv", buffer: Buffer.from("id,zone\nSENSOR-42,unknown") });
  await expect(page.getByRole("cell", { name: "Unknown zone", exact: true })).toBeVisible();
});

test("@gate @a11y virtual grid and lazy tree retain identity and keyboard selection", async ({ page }) => {
  await openStory(page, "data");
  const grid = page.getByRole("grid", { name: "Virtual incidents", exact: true });
  await expect(grid).toHaveAttribute("aria-rowcount", "501");
  expect(await grid.getByRole("row").count()).toBeLessThan(20);
  const row = grid.locator('[data-id="INC-0001"]');
  await row.getByRole("gridcell").nth(1).focus();
  await page.keyboard.press("Space");
  await expect(row).toHaveAttribute("aria-selected", "true");
  const retained = await row.elementHandle();
  await page.getByRole("button", { name: "Insert row", exact: true }).click();
  expect(await retained.evaluate((node) => node === document.querySelector('[data-id="INC-0001"]'))).toBe(true);
  await page.getByRole("button", { name: "Bulk action (1)", exact: true }).click();
  await expect(page.getByTestId("bulk-result")).toHaveText("INC-0001");
  await row.getByRole("gridcell").nth(1).focus();
  await page.keyboard.press("ArrowDown");
  await expect(grid.locator('[data-id="INC-0002"]').getByRole("gridcell").nth(1)).toBeFocused();
  await page.keyboard.press("Control+End");
  await expect(grid.locator('[data-id="INC-0499"]').getByRole("gridcell").last()).toBeFocused();
  const tree = page.getByRole("tree", { name: "Resource tree", exact: true });
  await tree.focus();
  await page.keyboard.press("ArrowRight");
  await expect(page.getByRole("treeitem", { name: /Floor/u })).toBeVisible();
  await page.keyboard.press("ArrowRight");
  await page.keyboard.press("ArrowRight");
  await page.keyboard.press("ArrowRight");
  await page.keyboard.press("Enter");
  await expect(page.getByRole("treeitem", { name: "SENSOR-42", exact: true })).toHaveAttribute("aria-selected", "true");
  await expect(tree).toBeFocused();
  expect((await new AxeBuilder({ page }).include("#catalogue").analyze()).violations).toEqual([]);
});
