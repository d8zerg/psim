import { resolve } from "node:path";
import AxeBuilder from "@axe-core/playwright";
import { expect, test, type Page } from "@playwright/test";

const themes = ["dark", "light", "contrast", "night"];
const densities = ["comfortable", "compact", "wall"];
const languages = ["ru", "en"] as const;
const modes = ["normal", "crisis", "training", "wall"];
async function openDesignSystem(page: Page, preferences: Record<string, string>) {
  await page.addInitScript((preferences) => {
    localStorage.setItem("psim.ui", JSON.stringify(preferences));
  }, preferences);
  await page.goto("/design-system.html");
  await expect(page.getByTestId("preview-INC-1042")).toBeVisible();
}
for (const theme of themes) {
  for (const density of densities) {
    for (const language of languages) {
      test(`@gate @a11y accessible layouts: ${theme}, ${density}, ${language}`, async ({ page }) => {
        await openDesignSystem(page, { theme, density, language });
        await expect(page.locator("html")).toHaveAttribute("data-theme", theme);
        await expect(page.locator("html")).toHaveAttribute("data-density", density);
        await expect(page.locator("html")).toHaveAttribute("lang", language);
        for (const mode of modes) {
          await test.step(mode, async () => {
            await page
              .getByLabel(language === "ru" ? "Режим работы" : "Operating mode", { exact: true })
              .selectOption(mode);
            await expect(page.locator("html")).toHaveAttribute("data-mode", mode);
            const result = await new AxeBuilder({ page })
              .withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22a", "wcag22aa"])
              .analyze();
            expect(result.violations).toEqual([]);
            expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
          });
        }
      });
      test(`@gate @visual foundations: ${theme}, ${density}, ${language}`, async ({ page }) => {
        await openDesignSystem(page, { theme, density, language });
        await page.evaluate(() => document.fonts.ready);
        await expect(page.locator("#foundations")).toHaveScreenshot(`components-${theme}-${density}-${language}.png`, {
          animations: "disabled",
          stylePath: resolve(import.meta.dirname, "component-screenshot.css"),
        });
        if (density === "comfortable" && language === "ru") {
          await page.evaluate(() => {
            window.scrollTo(0, 0);
            if (document.activeElement instanceof HTMLElement) document.activeElement.blur();
          });
          await page.screenshot({ path: `../../../build/web-design-system/${theme}.png`, fullPage: true });
        }
      });
    }
  }
}
for (const mode of modes) {
  test(`@gate @visual operating mode: ${mode}`, async ({ page }) => {
    await openDesignSystem(page, { theme: "dark", density: "comfortable", language: "en", mode });
    await page.evaluate(() => document.fonts.ready);
    await expect(page.locator("html")).toHaveAttribute("data-mode", mode);
    await expect(page.getByLabel("Operating mode", { exact: true })).toHaveValue(mode);
    if (mode === "crisis" || mode === "training") await expect(page.locator("body")).toHaveCSS("box-shadow", /inset/u);
    else await expect(page.locator("body")).toHaveCSS("box-shadow", "none");
    if (mode === "wall") await expect(page.locator("body")).toHaveCSS("font-variant-numeric", "tabular-nums");
    const heading = await page.locator(".page-heading").boundingBox();
    const viewport = page.viewportSize();
    if (!heading || !viewport) throw new Error("Mode header and viewport are required");
    await expect(page).toHaveScreenshot(`mode-${mode}.png`, {
      animations: "disabled",
      clip: { x: 0, y: 0, width: viewport.width, height: Math.ceil(heading.y + heading.height) },
    });
  });
}
test("@gate loads saved presentation before the application JavaScript", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ theme: "light", density: "compact", language: "en" }));
  });
  await page.route("**/assets/*.js", (route) => route.abort());
  await page.goto("/design-system.html");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  await expect(page.locator("html")).toHaveAttribute("data-density", "compact");
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  expect(await page.evaluate(() => document.documentElement.style.backgroundColor)).toBe("rgb(242, 245, 249)");
});
test("@gate @a11y retains DOM nodes, focus and layout while refreshing and after an error", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/design-system.html");
  const row = page.getByTestId("preview-INC-1041");
  await row.getByRole("button").click();
  await row.evaluate((element) => {
    element.setAttribute("data-retained", "yes");
  });
  const before = await row.boundingBox();
  await page.getByRole("button", { name: "Refresh data", exact: true }).evaluate((button) => {
    if (button instanceof HTMLButtonElement) button.click();
  });
  await expect(page.getByText("Updating data. Current rows remain available.")).toBeVisible();
  await expect(row.getByRole("button")).toBeFocused();
  await expect(row).toHaveAttribute("data-retained", "yes");
  expect((await row.boundingBox())?.y).toBe(before?.y);
  await expect(page.getByRole("button", { name: "Refresh data", exact: true })).toBeEnabled();
  await page.getByRole("button", { name: "Simulate connection loss" }).click();
  await expect(page.getByText("Could not load incidents. Check your connection and try again.")).toBeVisible();
  await expect(row).toHaveAttribute("data-retained", "yes");
  await expect(row).toHaveAttribute("data-selected", "true");
  await expect(page.getByRole("button", { name: "Acknowledge incident" })).toBeDisabled();
});
test("@gate @a11y traps dialog focus, cancels with Escape and returns to the trigger", async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/design-system.html");
  await expect(page.getByTestId("preview-INC-1042")).toBeVisible();
  await page.getByRole("textbox", { name: "Reason", exact: true }).fill("Verify entrance");
  const trigger = page.getByRole("button", { name: "Send device command", exact: true });
  await trigger.click();
  const dialog = page.getByRole("alertdialog");
  await expect(dialog.getByRole("button", { name: "Cancel" })).toBeFocused();
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.keyboard.press("Shift+Tab");
  await expect(dialog.getByRole("button", { name: "Confirm command", exact: true })).toBeFocused();
  await page.keyboard.press("Escape");
  await expect(dialog).not.toBeVisible();
  await expect(trigger).toBeFocused();
});
for (const { width, density } of [
  { width: 390, density: "comfortable" },
  { width: 1280, density: "wall" },
]) {
  test(`@gate status layout stays stable: ${String(width)}px, ${density}`, async ({ page }, testInfo) => {
    await page.setViewportSize({ width, height: 900 });
    await page.addInitScript((density) => {
      localStorage.setItem("psim.ui", JSON.stringify({ language: "ru", density }));
    }, density);
    await page.goto("/design-system.html");
    const row = page.getByTestId("preview-INC-1041");
    await row.getByRole("button").click();
    const position = () => row.evaluate((node) => node.getBoundingClientRect().top + scrollY);
    const current = await position();
    await page.getByRole("button", { name: "Обновить данные", exact: true }).evaluate((node) => {
      if (node instanceof HTMLButtonElement) node.click();
    });
    await expect(page.getByText("Обновление данных. Текущие строки остаются доступны.")).toBeVisible();
    const refreshing = await position();
    await expect(page.getByRole("button", { name: "Обновить данные", exact: true })).toBeEnabled();
    const recovered = await position();
    await page.getByRole("button", { name: "Имитировать потерю связи" }).evaluate((node) => {
      if (node instanceof HTMLButtonElement) node.click();
    });
    await expect(
      page.getByText("Последние известные данные. Для действий необходимо актуальное соединение."),
    ).toBeVisible();
    const stale = await position();
    await testInfo.attach("status-layout.json", {
      body: JSON.stringify({ width, density, current, refreshing, recovered, stale }),
      contentType: "application/json",
    });
    expect({ refreshing, recovered, stale }).toEqual({ refreshing: current, recovered: current, stale: current });
    await expect(row.getByRole("button")).toBeFocused();
    await expect(row).toHaveAttribute("data-selected", "true");
  });
}
test("@gate @a11y supports narrow layouts, reduced motion and unavailable preference storage", async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await page.emulateMedia({ reducedMotion: "reduce" });
  await page.addInitScript(() => {
    Storage.prototype.getItem = () => {
      throw new Error("blocked");
    };
    Storage.prototype.setItem = () => {
      throw new Error("blocked");
    };
  });
  await page.goto("/design-system.html");
  await expect(page.getByTestId("preview-INC-1042")).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.getByLabel("Тема", { exact: true }).selectOption("light");
  await expect(page.locator("html")).toHaveAttribute("data-theme", "light");
  expect(
    (
      await new AxeBuilder({ page })
        .withTags(["wcag2a", "wcag2aa", "wcag21a", "wcag21aa", "wcag22a", "wcag22aa"])
        .analyze()
    ).violations,
  ).toEqual([]);
});

test("@gate @a11y serves local font subsets and keeps meaning without color", async ({ page }) => {
  const failedFonts: string[] = [];
  page.on("response", (response) => {
    if (response.url().endsWith(".woff2") && response.status() !== 200) failedFonts.push(response.url());
  });
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/design-system.html");
  await expect(page.getByTestId("preview-INC-1042")).toBeVisible();
  const fonts = await page.evaluate(async () => {
    await Promise.all([
      document.fonts.load('16px "IBM Plex Sans Variable"', "Alarm Тревога"),
      document.fonts.load('16px "IBM Plex Mono"', "A1 У1"),
    ]);
    return (
      document.fonts.check('16px "IBM Plex Sans Variable"', "Alarm Тревога") &&
      document.fonts.check('16px "IBM Plex Mono"', "A1 У1")
    );
  });
  expect(fonts).toBe(true);
  expect(failedFonts).toEqual([]);
  await page.addStyleTag({ content: "html { filter: grayscale(1); }" });
  for (const priority of ["P1", "P2", "P3", "P4"])
    await expect(page.getByTestId("preview-table").getByText(new RegExp(priority))).toBeVisible();
  const names = await page
    .locator("button")
    .evaluateAll((buttons) =>
      buttons.map((button) => (button.getAttribute("aria-label") ?? button.textContent).trim()),
    );
  expect(names.every(Boolean)).toBe(true);
  await page.emulateMedia({ forcedColors: "active" });
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
});

test("@gate rejects malformed stored theme values before modules load", async ({ page }) => {
  await page.route("**/assets/*.js", (route) => route.abort());
  for (const theme of ["__proto__", { toString: null }, 17, null]) {
    await page.addInitScript((value) => {
      localStorage.setItem("psim.ui", JSON.stringify({ theme: value }));
    }, theme);
    await page.goto("/design-system.html");
    await expect(page.locator("html")).toHaveAttribute("data-theme", "dark");
    expect(await page.evaluate(() => document.documentElement.style.colorScheme)).toBe("dark");
  }
});

test("@gate @a11y keeps anchor headings and keyboard focus below the sticky navigation", async ({ page }) => {
  await page.setViewportSize({ width: 900, height: 900 });
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/design-system.html");
  await expect(page.getByTestId("preview-INC-1042")).toBeVisible();
  for (const density of ["comfortable", "compact", "wall"]) {
    await page.getByLabel("Density", { exact: true }).selectOption(density);
    await page.locator('.section-nav a[href="#foundations"]').click();
    const heading = await page.locator("#foundations-heading").boundingBox();
    await expect(page.locator(".section-nav")).toHaveCSS("position", "sticky");
    const navigation = await page.locator(".section-nav").boundingBox();
    expect(navigation?.y).toBeCloseTo(0);
    expect(heading).not.toBeNull();
    expect(navigation).not.toBeNull();
    expect(heading?.y).toBeGreaterThanOrEqual((navigation?.y ?? 0) + (navigation?.height ?? 0));
    const field = page.getByRole("textbox", { name: "Pass number", exact: true });
    await field.focus();
    await expect(field).toBeFocused();
    const bounds = await field.boundingBox();
    const bar = await page.locator(".section-nav").boundingBox();
    expect(bounds?.y).toBeGreaterThanOrEqual((bar?.y ?? 0) + (bar?.height ?? 0));
  }
});
