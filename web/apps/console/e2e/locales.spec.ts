import { expect, test } from "@playwright/test";

test("@gate loads only the selected product dictionary on the console", async ({ page }) => {
  const dictionaries: string[] = [];
  page.on("request", (request) => {
    if (/\/(?:product|demo)-(?:en|ru)-[^/]+\.js$/u.test(request.url())) dictionaries.push(request.url());
  });
  await page.goto("/");
  await expect(page.getByRole("button", { name: "Войти", exact: true })).toBeVisible();
  expect(dictionaries).toHaveLength(1);
  expect(dictionaries[0]).toMatch(/\/product-ru-/u);
});

test("@gate keeps the mounted screen and focus while another language is loading", async ({ page }) => {
  await page.goto("/design-system.html");
  const row = page.getByTestId("preview-INC-1041");
  await row.getByRole("button").click();
  await row.evaluate((node) => {
    node.dataset.retained = "yes";
  });
  let release: (() => void) | undefined;
  const ready = new Promise<void>((resolve) => {
    release = resolve;
  });
  await page.route(/\/(?:product|demo)-en-[^/]+\.js$/u, async (route) => {
    await ready;
    await route.continue();
  });
  const language = page.getByRole("combobox", { name: "Язык", exact: true });
  await language.focus();
  await language.selectOption("en");
  await expect(language).toHaveAttribute("aria-busy", "true");
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(row).toHaveAttribute("data-retained", "yes");
  await expect(row).toHaveAttribute("data-selected", "true");
  await expect(language).toBeFocused();
  release?.();
  await expect(page.locator("html")).toHaveAttribute("lang", "en");
  await expect(page.getByRole("heading", { name: "Design system", exact: true })).toBeVisible();
  await expect(row).toHaveAttribute("data-retained", "yes");
  await expect(page.getByRole("combobox", { name: "Language", exact: true })).toBeFocused();
});

test("@gate keeps the current locale after a translation network failure", async ({ page }) => {
  await page.goto("/design-system.html");
  const row = page.getByTestId("preview-INC-1041");
  await row.getByRole("button").click();
  await page.route("**/product-en-*.js", (route) => route.abort());
  await page.getByRole("combobox", { name: "Язык", exact: true }).selectOption("en");
  await expect(page.getByRole("alert")).toContainText("Не удалось загрузить язык");
  await expect(page.locator("html")).toHaveAttribute("lang", "ru");
  await expect(row).toHaveAttribute("data-selected", "true");
  await expect(row.getByRole("button")).toBeEnabled();
});

test("@gate offers reload when the initial locale cannot load", async ({ page }) => {
  await page.route("**/product-ru-*.js", (route) => route.abort());
  await page.goto("/");
  await expect(page.getByRole("alert")).toContainText("Could not load the interface");
  await expect(page.getByRole("button", { name: "Повторить / Retry", exact: true })).toBeVisible();
});
