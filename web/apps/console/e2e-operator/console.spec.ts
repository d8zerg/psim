import { createHmac } from "node:crypto";
import { access, readFile, writeFile } from "node:fs/promises";
import { expect, test, type Page } from "@playwright/test";
const directory = `/repo/build/operator/${process.env.PSIM_OPERATOR_PRESET ?? "debug"}`;
interface Fixture {
  username: string;
  password: string;
  incidentId: string;
  deviceId: string;
  floorId: string;
  siteId: string;
  recipientId: string;
  recipientUsername: string;
  originalResolveDue: string;
  planVersion: number;
}
async function login(page: Page, f: Fixture) {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/");
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await page.locator("#username").fill(f.username);
  await page.locator("#password").fill(f.password);
  await page.locator("#kc-login").click();
  await page.getByRole("link", { name: "Unable to scan?" }).click();
  const secret = ((await page.locator("#kc-totp-secret-key, #totp-secret").textContent()) ?? "").replaceAll(/\s/gu, "");
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  const bits = Array.from(secret.toUpperCase())
    .map((char) => alphabet.indexOf(char).toString(2).padStart(5, "0"))
    .join("");
  const key = Buffer.from((bits.match(/.{8}/gu) ?? []).map((byte) => Number.parseInt(byte, 2)));
  const counter = Buffer.alloc(8);
  counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
  const mac = createHmac("sha1", key).update(counter).digest();
  const offset = (mac.at(-1) ?? 0) & 15;
  const code = String((mac.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0");
  await page.locator("input[name=totp]").fill(code);
  await page.locator("input[name=userLabel]").fill("Operator browser fixture");
  await page.getByRole("button", { name: "Submit", exact: true }).click();
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open", { timeout: 30000 });
}
test("S6-S9 operator works through native services with real PKCE, Kafka, command execution and SLA notifications", async ({
  page,
}) => {
  const f = JSON.parse(await readFile(`${directory}/fixture.json`, "utf8")) as Fixture;
  const failures: string[] = [];
  page.on("pageerror", (error) => failures.push(error.message));
  await login(page, f);
  await writeFile(`${directory}/browser-ready`, "ready");
  const row = page.locator(`[data-incident-id="${f.incidentId}"]`);
  await row.getByRole("button").click();
  await page.getByRole("button", { name: "Acknowledge incident", exact: true }).click();
  await page.getByRole("button", { name: "Start work", exact: true }).click();
  await page.getByLabel("Safety verified (Mandatory step)").check();
  await page
    .getByRole("region", { name: "Verify safety", exact: true })
    .getByRole("button", { name: "Complete step", exact: true })
    .click();
  await writeFile(`${directory}/response-restart-ready`, "ready");
  await expect
    .poll(
      async () => {
        try {
          await access(`${directory}/response-restart-done`);
          return true;
        } catch {
          return false;
        }
      },
      { timeout: 90000 },
    )
    .toBe(true);
  await expect(page.getByText(`Response fixture · Version: ${String(f.planVersion)}`, { exact: true })).toBeVisible();
  await page.getByLabel("Observed condition").selectOption("confirmed");
  await page
    .getByRole("region", { name: "Choose outcome", exact: true })
    .getByRole("button", { name: "Complete step", exact: true })
    .click();
  await expect(
    page.getByRole("region", { name: "Choose outcome", exact: true }).getByText("Completed", { exact: true }),
  ).toBeVisible();
  await page.getByLabel("Device", { exact: true }).fill(f.deviceId);
  await page.getByLabel("Capability", { exact: true }).selectOption("arm");
  await page.getByRole("combobox", { name: "Response plan", exact: true }).selectOption("operate");
  await page.getByLabel("Reason: Send device command").fill("Verified area before arming");
  await page.getByRole("button", { name: "Preview command", exact: true }).click();
  await expect(page.getByText("Consequence: Consequence is unknown")).toBeVisible();
  await page.getByRole("button", { name: "Confirm command", exact: true }).click();
  await expect(page.getByText("Executed", { exact: true }).first()).toBeVisible({ timeout: 30000 });
  await expect(
    page.getByRole("region", { name: "Record outcome", exact: true }).getByText("Active", { exact: true }),
  ).toBeVisible({ timeout: 30000 });
  await page.getByRole("button", { name: "New command", exact: true }).click();
  const other = await page
    .context()
    .browser()
    ?.newContext({ baseURL: new URL(page.url()).origin });
  if (!other) throw new Error("Missing browser context");
  const recipient = await other.newPage();
  recipient.on("pageerror", (error) => failures.push(error.message));
  await login(recipient, { ...f, username: f.recipientUsername });
  await recipient.locator(`[data-incident-id="${f.incidentId}"]`).getByRole("button").click();
  await page.getByLabel("Capability", { exact: true }).selectOption("unlock");
  await page.getByLabel("Reason: Send device command").fill("Two person approval");
  await expect(async () => {
    await page.getByRole("button", { name: "Preview command", exact: true }).click();
    await expect(page.getByText("Approval by another supervisor is required", { exact: true })).toBeVisible();
    await expect(page.getByRole("button", { name: "Confirm command", exact: true })).toBeEnabled({ timeout: 1000 });
  }).toPass({ timeout: 10000 });
  await page.getByRole("button", { name: "Confirm command", exact: true }).click();
  await expect(page.getByRole("button", { name: "Approve command", exact: true })).toHaveCount(0);
  const commandList = recipient.getByRole("region", { name: "Device commands", exact: true });
  const pending = commandList
    .locator("li")
    .filter({ has: recipient.getByRole("button", { name: "Preview command", exact: true }) });
  const commandId = await pending.locator("code").first().textContent();
  if (!commandId) throw new Error("Missing command identifier");
  const commandRow = commandList.locator("li").filter({ hasText: commandId });
  await commandRow.getByRole("button", { name: "Preview command", exact: true }).click();
  await commandRow.getByRole("button", { name: "Approve command", exact: true }).click();
  await expect(commandRow.getByText("Executed", { exact: true })).toBeVisible({ timeout: 30000 });
  await expect(page.getByRole("button", { name: "New command", exact: true })).toBeEnabled({ timeout: 30000 });
  for (const [capability, status] of [
    ["reject", "Failed"],
    ["hold", "Timed out"],
  ] as const) {
    await page.getByRole("button", { name: "New command", exact: true }).click();
    await page.getByLabel("Capability", { exact: true }).selectOption(capability);
    await page.getByLabel("Reason: Send device command").fill(`Check ${capability}`);
    await page.getByRole("button", { name: "Preview command", exact: true }).click();
    await page.getByRole("button", { name: "Confirm command", exact: true }).click();
    await expect(page.getByText(status, { exact: true }).first()).toBeVisible({ timeout: 30000 });
  }
  await expect(page.getByRole("region", { name: "Notified", exact: true }).locator("li").first()).toBeVisible({
    timeout: 60000,
  });
  await page.getByRole("button", { name: "Mark as read", exact: true }).first().click();
  // Find a floor by expanding native accessible tree branches.
  const tree = page.getByRole("tree", { name: "Object structure" });
  await tree.getByRole("button", { name: "SITE", exact: true }).click();
  await tree.getByRole("button", { name: "BUILDING", exact: true }).click();
  await tree.getByRole("treeitem", { name: "FLOOR", exact: false }).last().click();
  await expect(page.getByRole("img", { name: "Plan: FLOOR" })).toBeVisible();
  await expect(page.getByTestId("device-status").first()).toBeVisible();
  const sla = page.getByText("Resolution target", { exact: true }).locator("+ dd");
  await expect(sla).toHaveText(f.originalResolveDue);
  await page.getByLabel("Reason", { exact: true }).fill("Shift handover");
  await page.getByLabel("Recipient", { exact: true }).fill(f.recipientId);
  await page.getByRole("button", { name: "Request transfer", exact: true }).click();
  await expect(page.getByText(new RegExp(f.recipientId, "u")).first()).toBeVisible();
  await expect(sla).toHaveText(f.originalResolveDue);
  await recipient.getByLabel("I reviewed this version of the incident and response run", { exact: true }).check();
  await recipient.getByRole("button", { name: "Accept responsibility", exact: true }).click();
  await expect(
    recipient.getByText(`Response fixture · Version: ${String(f.planVersion)}`, { exact: true }),
  ).toBeVisible();
  await expect(recipient.getByText("Resolution target", { exact: true }).locator("+ dd")).toHaveText(
    f.originalResolveDue,
  );
  await recipient.getByLabel("Reason", { exact: true }).fill("Mandatory response check");
  await expect(recipient.getByRole("button", { name: "Resolve incident", exact: true })).toBeDisabled();
  await recipient
    .getByRole("region", { name: "Record outcome", exact: true })
    .getByRole("button", { name: "Complete step", exact: true })
    .click();
  await expect(recipient.getByRole("button", { name: "Resolve incident", exact: true })).toBeEnabled();
  await recipient.getByRole("button", { name: "Resolve incident", exact: true }).click();
  await expect(recipient.getByRole("button", { name: "Close incident", exact: true })).toBeEnabled();
  await recipient.getByRole("button", { name: "Close incident", exact: true }).click();
  await expect(recipient.getByRole("button", { name: "Close incident", exact: true })).toBeDisabled();
  await recipient.screenshot({ path: `${directory}/console.png`, fullPage: true });
  await other.close();
  expect(failures).toEqual([]);
});
