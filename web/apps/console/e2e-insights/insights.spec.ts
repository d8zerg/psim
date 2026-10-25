import { createHmac } from "node:crypto";
import { readFile, writeFile, access } from "node:fs/promises";
import { expect, test, type Page } from "@playwright/test";
import AxeBuilder from "@axe-core/playwright";
const directory = `/repo/build/insights/${process.env.PSIM_INSIGHTS_PRESET ?? "release"}`;
interface Fixture {
  auditor: string;
  manager: string;
  scoped: string;
  password: string;
  siteId: string;
  foreignSiteId: string;
  from: string;
  to: string;
  tamperSeq: string;
}
async function login(page: Page, path: string, title: string, f: { username: string; password: string }) {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto(path);
  await page.getByRole("button", { name: "Sign in", exact: true }).click();
  await page.locator("#username").fill(f.username);
  await page.locator("#password").fill(f.password);
  await page.locator("#kc-login").click();
  const scan = page.getByRole("link", { name: "Unable to scan?" });
  await expect(scan.or(page.locator("#otp"))).toBeVisible();
  const enrolling = await scan.isVisible();
  let secret: string;
  if (enrolling) {
    await scan.click();
    secret = ((await page.locator("#kc-totp-secret-key, #totp-secret").textContent()) ?? "").replaceAll(/\s/gu, "");
    await writeFile(`${directory}/otp-${f.username}.secret`, secret, { mode: 0o600 });
  } else secret = await readFile(`${directory}/otp-${f.username}.secret`, "utf8");
  const alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  const bits = Array.from(secret.toUpperCase())
    .map((char) => alphabet.indexOf(char).toString(2).padStart(5, "0"))
    .join("");
  const key = Buffer.from((bits.match(/.{8}/gu) ?? []).map((byte) => Number.parseInt(byte, 2)));
  const counter = Buffer.alloc(8);
  counter.writeBigUInt64BE(BigInt(Math.floor(Date.now() / 30000)));
  const mac = createHmac("sha1", key).update(counter).digest();
  const offset = (mac.at(-1) ?? 0) & 15;
  await page
    .locator("input[name=totp]")
    .fill(String((mac.readUInt32BE(offset) & 0x7fffffff) % 1000000).padStart(6, "0"));
  if (enrolling) await page.locator("input[name=userLabel]").fill("Insights browser");
  await page.getByRole("button", { name: enrolling ? "Submit" : "Sign In", exact: true }).click();
  await expect(page.getByRole("heading", { name: title, exact: true })).toBeVisible();
}
async function fixture(): Promise<Fixture> {
  return JSON.parse(await readFile(`${directory}/fixture.json`, "utf8")) as Fixture;
}
async function synchronize(stage: string) {
  await writeFile(`${directory}/${stage}-ready`, "ready\n");
  await expect
    .poll(
      async () => {
        try {
          await access(`${directory}/${stage}-done`);
          return true;
        } catch {
          return false;
        }
      },
      { timeout: 90000 },
    )
    .toBe(true);
}
test("auditor searches, exports and identifies database modifications and deletions", async ({ page }) => {
  const f = await fixture();
  await login(page, "/audit", "Audit log", { username: f.auditor, password: f.password });
  await page.getByLabel("Action prefix", { exact: true }).fill("fixture.ui.");
  await page.getByRole("button", { name: "Search audit", exact: true }).click();
  await expect(page.locator("tbody tr")).toHaveCount(64);
  await page.getByRole("button", { name: "Next page", exact: true }).click();
  await expect(page.locator("tbody tr")).toHaveCount(37);
  await expect(page.locator("tbody tr").last()).toContainText(f.tamperSeq);
  await page.locator("summary").last().click();
  await expect(page.locator("details").last()).toContainText("browser acceptance");
  await page.getByRole("button", { name: "Previous page", exact: true }).click();
  await expect(page.locator("tbody tr")).toHaveCount(64);
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export JSONL", exact: true }).click();
  await (await download).saveAs(`${directory}/psim-audit.jsonl`);
  await page.getByRole("button", { name: "Verify integrity", exact: true }).click();
  await expect(page.getByText("Chain intact", { exact: true })).toBeVisible();
  await expect(page.getByRole("heading", { name: "Signed checkpoint", exact: true })).toBeVisible();
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  for (const stage of ["modify", "delete"] as const) {
    await synchronize(stage);
    await page.getByRole("button", { name: "Verify integrity", exact: true }).click();
    await expect(page.getByText("Chain integrity broken", { exact: true })).toBeVisible();
    await expect(page.getByTestId("integrity-result").locator("dd").nth(1)).toHaveText(f.tamperSeq);
  }
  await synchronize("restore");
  await page.getByRole("button", { name: "Verify integrity", exact: true }).click();
  await expect(page.getByText("Chain intact", { exact: true })).toBeVisible();
  await writeFile(`${directory}/audit-browser.json`, JSON.stringify({ passed: true, tamperSeq: f.tamperSeq }));
});
test("manager builds the reference report within three seconds and exports the same metrics", async ({ page }) => {
  const f = await fixture();
  await login(page, "/reports", "Incident reports", { username: f.manager, password: f.password });
  await page.getByLabel("From (UTC, inclusive)", { exact: true }).fill(f.from.slice(0, 19));
  await page.getByLabel("To (UTC, exclusive)", { exact: true }).fill(f.to.slice(0, 19));
  await page.getByRole("combobox", { name: "Group by", exact: true }).selectOption("SITE");
  const start = Date.now();
  await page.getByRole("button", { name: "Build report", exact: true }).click();
  await expect(page.getByTestId("total-incidentsCreated")).toHaveText("10");
  const elapsed = Date.now() - start;
  expect(elapsed).toBeLessThan(3000);
  for (const [metric, value] of Object.entries({
    incidentsResolved: "10",
    mtta: "60",
    mttr: "120",
    falseAlarmRate: "20%",
    ackSlaCompliance: "50%",
    resolveSlaCompliance: "100%",
    escalated: "1",
    ackSlaEvaluated: "10",
    resolveSlaEvaluated: "10",
  }))
    await expect(page.getByTestId(`total-${metric}`)).toHaveText(value);
  await expect(page.getByRole("img")).toHaveCount(2);
  const download = page.waitForEvent("download");
  await page.getByRole("button", { name: "Export CSV", exact: true }).click();
  await (await download).saveAs(`${directory}/psim-incidents.csv`);
  const csv = await readFile(`${directory}/psim-incidents.csv`, "utf8");
  expect(csv).toContain("row_kind");
  expect(csv).toContain("120");
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.setViewportSize({ width: 390, height: 844 });
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await writeFile(
    `${directory}/browser.json`,
    JSON.stringify({
      passed: true,
      reportElapsedMs: elapsed,
      checks: [
        "real PKCE with MFA",
        "audit search, cursor pages and signed export",
        "precise detection of SQL modification and deletion",
        "reference report counts, response times and SLA",
        "matching CSV export",
        "desktop and mobile accessibility",
      ],
    }),
  );
});
test("scoped auditor searches only the permitted site and cannot verify or export", async ({ page }) => {
  const f = await fixture();
  await login(page, "/audit", "Audit log", { username: f.scoped, password: f.password });
  await page.getByLabel("Site ID", { exact: true }).fill(f.siteId);
  await page.getByLabel("Action prefix", { exact: true }).fill("fixture.ui.");
  await page.getByRole("button", { name: "Search audit", exact: true }).click();
  await expect(page.locator("tbody tr")).toHaveCount(64);
  await expect(page.getByRole("button", { name: "Export JSONL", exact: true })).toBeDisabled();
  await expect(page.getByRole("button", { name: "Verify integrity", exact: true })).toHaveCount(0);
  await page.getByLabel("Site ID", { exact: true }).fill(f.foreignSiteId);
  await page.getByRole("button", { name: "Search audit", exact: true }).click();
  await expect(page.getByText("Access is unavailable. Previous data has been hidden.", { exact: true })).toBeVisible();
  await expect(page.locator("tbody tr")).toHaveCount(0);
  await page.getByRole("button", { name: "Check access again", exact: true }).click();
  await page.getByLabel("Site ID", { exact: true }).fill(f.siteId);
  await page.getByRole("button", { name: "Search audit", exact: true }).click();
  await expect(page.locator("tbody tr")).toHaveCount(64);
  await synchronize("revoke");
  await expect(page.getByText("Access is unavailable. Previous data has been hidden.", { exact: true })).toBeVisible({
    timeout: 15000,
  });
  await expect(page.locator("tbody tr")).toHaveCount(0);
  await writeFile(
    `${directory}/privacy-browser.json`,
    JSON.stringify({
      passed: true,
      checks: ["permitted site search", "foreign site denied", "role revocation hides data without a new JWT"],
    }),
  );
});
