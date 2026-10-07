import { expect, test } from "@playwright/test";

test("the built console starts and shows its shell", async ({ page }) => {
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));

  await page.goto("/");

  await expect(page).toHaveTitle("PSIM");
  await expect(page.getByRole("heading", { name: "PSIM Platform" })).toBeVisible();
  await expect(page.getByTestId("connection-status")).toHaveText("offline");
  expect(errors).toEqual([]);
});
