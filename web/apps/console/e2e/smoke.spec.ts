import { expect, test } from "@playwright/test";

test("@gate the built console starts and shows its shell", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));

  await page.goto("/");

  await expect(page).toHaveTitle("PSIM");
  await expect(page.getByRole("heading", { name: "Open-Source PSIM Platform" })).toBeVisible();
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "offline");
  expect(errors).toEqual([]);
});
