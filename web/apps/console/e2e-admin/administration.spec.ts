import { createHmac } from "node:crypto";
import { readFile, writeFile } from "node:fs/promises";
import { expect, test, type Locator, type Page } from "@playwright/test";
import AxeBuilder from "@axe-core/playwright";
const directory = `/repo/build/admin/${process.env.PSIM_ADMIN_PRESET ?? "release"}`;
interface Fixture {
  username: string;
  userId: string;
  scopedUsername: string;
  foreignFloorId: string;
  renewedCertificatePem: string;
  password: string;
  connectorId: string;
  connectorType: string;
  certificatePem: string;
}
async function login(page: Page, f: Fixture) {
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
  });
  await page.goto("/admin");
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
  if (enrolling) await page.locator("input[name=userLabel]").fill("Administration browser");
  await page.getByRole("button", { name: enrolling ? "Submit" : "Sign In", exact: true }).click();
  await expect(page.getByRole("heading", { name: "Administration", exact: true })).toBeVisible();
}
async function open(page: Page, name: string) {
  await page
    .getByRole("navigation", { name: "Administration", exact: true })
    .getByRole("button", { name, exact: true })
    .click();
}
async function create(page: Page, section: string, name: string) {
  await open(page, section);
  await page.getByRole("button", { name: "Create", exact: true }).click();
  const editor = page.getByRole("region", { name: section, exact: true });
  await editor.getByLabel("Name", { exact: true }).fill(name);
  return editor;
}
async function save(page: Page, editor: Locator) {
  const response = page.waitForResponse(
    (response) => ["POST", "PUT"].includes(response.request().method()) && response.url().includes("/v1/"),
  );
  await editor.getByRole("button", { name: "Save", exact: true }).click();
  const result = await response;
  const body: unknown = await result.json();
  expect(result.status(), JSON.stringify(body)).toBeLessThan(300);
  await expect(editor.getByRole("button", { name: "Save", exact: true })).toBeEnabled();
}
async function confirm(page: Page, name: string) {
  await page.getByRole("button", { name, exact: true }).click();
  await page.getByRole("alertdialog").getByRole("button", { name: "Confirm action", exact: true }).click();
}
test("S1-S2 administrator configures connector, mapping, catalog, import, rule and response through the UI", async ({
  page,
}) => {
  const f = JSON.parse(await readFile(`${directory}/fixture.json`, "utf8")) as Fixture;
  const errors: string[] = [];
  page.on("pageerror", (error) => errors.push(error.message));
  await login(page, f);
  const connector = await create(page, "Connectors", "Browser connector");
  await connector.getByLabel("Connector identifier", { exact: true }).fill(f.connectorId);
  await connector.getByLabel("Connector type", { exact: true }).fill(f.connectorType);
  await connector.getByLabel("Public certificate PEM").fill(f.certificatePem);
  await connector.getByRole("button", { name: "Save", exact: true }).click();
  await expect(connector.getByRole("button", { name: "Renew certificate" })).toBeVisible();
  await connector.getByLabel("Public certificate PEM").fill(f.renewedCertificatePem);
  await confirm(page, "Renew certificate");
  await expect(connector.getByText(/^Version: 2/u)).toBeVisible();
  await open(page, "Normalization mappings");
  await page.getByRole("button", { name: "Create", exact: true }).click();
  const mapping = page.getByRole("region", { name: "Normalization mappings", exact: true });
  await mapping.getByLabel("Connector type", { exact: true }).fill(f.connectorType);
  await mapping.getByRole("button", { name: "Add: Mappings", exact: true }).click();
  await mapping.getByLabel("Raw event code").fill("ALARM*");
  await mapping.getByLabel("Taxonomy type", { exact: true }).fill("intrusion.zone.alarm");
  await save(page, mapping);
  const site = await create(page, "Locations", "Browser site");
  const siteId = await site.getByLabel("Location identifier", { exact: true }).inputValue();
  await site.getByLabel("Code", { exact: true }).fill("browser-site-" + f.connectorId);
  await save(page, site);
  async function location(kind: string, name: string, parent: string) {
    const editor = await create(page, "Locations", name);
    const id = await editor.getByLabel("Location identifier", { exact: true }).inputValue();
    await editor.getByLabel("Kind", { exact: true }).selectOption("LOCATION_KIND_" + kind);
    await editor.getByLabel("Parent identifier", { exact: true }).fill(parent);
    await editor.getByLabel("Code", { exact: true }).fill(name.replaceAll(" ", "-").toLowerCase());
    await save(page, editor);
    return id;
  }
  const buildingId = await location("BUILDING", "Browser building", siteId);
  const floorId = await location("FLOOR", "Browser floor", buildingId);
  const zoneId = await location("ZONE", "Browser zone", floorId);
  const floors = [floorId];
  for (let index = 2; index <= 3; index++)
    floors.push(await location("FLOOR", `Browser floor ${String(index)}`, buildingId));
  const zones = [zoneId];
  for (let index = 1; index < 20; index++)
    zones.push(await location("ZONE", `Browser zone ${String(index)}`, floors[index % floors.length] ?? floorId));
  for (const planFloor of floors) {
    await open(page, "Floor plans");
    await page.getByLabel("Floor identifier").fill(planFloor);
    await page.getByRole("button", { name: "Select record", exact: true }).click();
    await page.getByLabel("File", { exact: true }).setInputFiles(`${directory}/plan.png`);
    await page.getByRole("button", { name: "Upload plan", exact: true }).click();
    await expect(page.getByRole("button", { name: "View version 1" })).toBeVisible();
    await page.getByRole("button", { name: "View version 1" }).click();
    await expect(page.getByRole("img", { name: "Floor plans: 1" })).toBeVisible();
    expect(
      await page
        .getByRole("img", { name: "Floor plans: 1" })
        .evaluate((image: HTMLImageElement) => image.src.startsWith("blob:") && image.naturalWidth === 100),
    ).toBe(true);
  }
  const deviceType = await create(page, "Device types", "Browser device type");
  const deviceTypeId = await deviceType.getByLabel("Device type identifier", { exact: true }).inputValue();
  await deviceType.getByLabel("Code", { exact: true }).fill("browser-device-" + f.connectorId);
  await deviceType.getByLabel("Taxonomy domain").fill("intrusion");
  await deviceType.getByRole("button", { name: "Configure: Source templates" }).click();
  await deviceType.getByRole("button", { name: "Add: Source templates", exact: true }).click();
  const template = deviceType.getByRole("group", { name: "Source templates 1", exact: true });
  await template.getByLabel("Kind", { exact: true }).fill("alarm");
  await template.getByLabel("Name", { exact: true }).fill("Alarm");
  await save(page, deviceType);
  const devices = [
    {
      deviceTypeId,
      zoneId,
      siteId,
      connectorId: f.connectorId,
      externalId: "browser-device",
      name: "Browser imported device " + f.connectorId,
      state: "RESOURCE_STATE_ACTIVE",
      sources: [{ kind: "alarm", externalRef: "browser-alarm", name: "Alarm" }],
    },
  ];
  async function importDevices(rows: readonly unknown[], created: string) {
    await open(page, "Device import");
    const started = Date.now();
    await page
      .getByLabel("File", { exact: true })
      .setInputFiles({ name: "devices.json", mimeType: "application/json", buffer: Buffer.from(JSON.stringify(rows)) });
    const checking = page.waitForRequest(
      (request) => request.method() === "GET" && new URL(request.url()).pathname.startsWith("/v1/imports/"),
    );
    await page.getByRole("button", { name: "Check import", exact: true }).click();
    const checked = (await checking).url();
    await expect(page.getByRole("button", { name: "Apply checked import", exact: true })).toBeEnabled({
      timeout: 300000,
    });
    const applied = page.waitForResponse((response) => {
      const url = new URL(response.url());
      return (
        response.request().method() === "POST" &&
        url.pathname === "/v1/imports" &&
        url.searchParams.get("dryRun") === "false"
      );
    });
    const selected = page.waitForResponse(
      (response) =>
        response.request().method() === "GET" &&
        response.url() !== checked &&
        new URL(response.url()).pathname.startsWith("/v1/imports/"),
    );
    await confirm(page, "Apply checked import");
    expect((await applied).ok()).toBe(true);
    const response = await selected;
    expect(response.ok()).toBe(true);
    await response.finished();
    await expect(page.getByTestId("admin-result").locator('[data-field="state"]')).toHaveText("Completed", {
      timeout: 300000,
    });
    await expect(page.getByTestId("admin-result").locator('[data-field="rowsCreated"]')).toHaveText(created, {
      timeout: 300000,
    });
    return Date.now() - started;
  }
  await importDevices(devices, "1");
  await open(page, "Devices");
  await page.getByRole("button", { name: "Browser imported device " + f.connectorId, exact: true }).click();
  const deviceId = await page.getByLabel("Device identifier", { exact: true }).inputValue();
  const first = devices[0];
  if (!first) throw new Error("Missing import template");
  for (let index = 1; index < 50000; index++)
    devices.push({
      ...first,
      externalId: `qualification-device-${String(index)}`,
      name: `Qualification device ${String(index).padStart(5, "0")}`,
      zoneId: zones[index % zones.length] ?? zoneId,
      sources: [{ kind: "alarm", externalRef: `qualification-alarm-${String(index)}`, name: "Alarm" }],
    });
  const importMs = await importDevices(devices, "49999");
  await expect(page.getByTestId("admin-result").locator('[data-field="rowsUpdated"]')).toHaveText("1");
  expect(importMs).toBeLessThanOrEqual(300000);
  await open(page, "Device import");
  await page
    .getByLabel("File", { exact: true })
    .setInputFiles({ name: "invalid.svg", mimeType: "image/svg+xml", buffer: Buffer.from("<svg/>") });
  await expect(page.getByText(/Unsupported file or size/u)).toBeVisible();
  await expect(page.getByRole("button", { name: "Check import", exact: true })).toBeDisabled();
  await open(page, "Maintenance");
  await page.getByRole("button", { name: "Create", exact: true }).click();
  const maintenance = page.getByRole("region", { name: "Maintenance", exact: true });
  await maintenance.getByLabel("Site identifier", { exact: true }).fill(siteId);
  await maintenance.getByLabel("Device identifier", { exact: true }).fill(deviceId);
  await maintenance.getByLabel("Starts at", { exact: true }).fill(new Date().toISOString());
  await maintenance.getByLabel("Ends at", { exact: true }).fill(new Date(Date.now() + 60000).toISOString());
  await maintenance.getByLabel("Reason", { exact: true }).fill("Inspect device");
  await maintenance.getByLabel("Responsible user", { exact: true }).fill(f.userId);
  await maintenance.getByRole("button", { name: "Add: Event types", exact: true }).click();
  await maintenance.getByLabel("Event types 1", { exact: true }).fill("intrusion.zone.alarm");
  await save(page, maintenance);
  await maintenance.getByLabel("Reason: End maintenance", { exact: true }).fill("Inspection complete");
  await confirm(page, "End maintenance");
  await expect(maintenance.getByText(/Cancelled/u).first()).toBeVisible();
  const policy = await create(page, "SLA and escalation", "Browser SLA");
  const policyId = await policy.getByLabel("Policy identifier", { exact: true }).inputValue();
  await policy.getByRole("button", { name: "Add: SLA targets", exact: true }).click();
  await policy.getByLabel("Priority", { exact: true }).selectOption("PRIORITY_P2");
  await policy.getByLabel("Acknowledge within", { exact: true }).fill("60s");
  await policy.getByLabel("Resolve within", { exact: true }).fill("600s");
  await policy.getByRole("button", { name: "Add: Escalation levels", exact: true }).click();
  await policy.getByLabel("Level", { exact: true }).fill("1");
  await policy.getByLabel("Delay", { exact: true }).fill("0s");
  await policy.getByRole("button", { name: "Configure: Recipients", exact: true }).click();
  await policy.getByRole("button", { name: "Add: Recipients", exact: true }).click();
  await policy.getByLabel("Recipients 1: Type").selectOption("userId");
  await policy.getByLabel("User identifier", { exact: true }).fill(f.userId);
  await policy.getByRole("button", { name: "Configure: Channels", exact: true }).click();
  await policy.getByRole("button", { name: "Add: Channels", exact: true }).click();
  await policy.getByLabel("Channels 1", { exact: true }).selectOption("NOTIFICATION_CHANNEL_IN_APP");
  await save(page, policy);
  const incidentType = await create(page, "Incident types", "Browser incident");
  const incidentTypeId = await incidentType.getByLabel("Incident type identifier", { exact: true }).inputValue();
  await incidentType.getByLabel("Code", { exact: true }).fill("browser-incident-" + f.connectorId);
  await incidentType.getByLabel("Escalation policy identifier", { exact: true }).fill(policyId);
  await save(page, incidentType);
  const plan = await create(page, "Response plans", "Browser response");
  const planId = await plan.getByLabel("Plan identifier", { exact: true }).inputValue();
  await plan.getByRole("button", { name: "Add: Incident bindings", exact: true }).click();
  await plan.getByLabel("Incident type identifier", { exact: true }).fill(incidentTypeId);
  await plan.getByLabel("Title", { exact: true }).fill("Inspect device");
  await plan.getByLabel("Text", { exact: true }).fill("Verify the device and record the result");
  await save(page, plan);
  await plan.getByRole("button", { name: "Validate saved version", exact: true }).click();
  await expect(plan.getByRole("button", { name: "Publish saved version" })).toBeEnabled();
  await confirm(page, "Publish saved version");
  await expect(plan.getByText(/^Version: [0-9]+ Published$/u)).toBeVisible();
  const rule = await create(page, "Correlation rules", "Browser rule");
  const ruleId = await rule.getByLabel("Rule identifier", { exact: true }).inputValue();
  await rule.getByRole("button", { name: "Add: Taxonomy types", exact: true }).click();
  await rule.getByLabel("Taxonomy types 1", { exact: true }).fill("intrusion.zone.alarm");
  await rule.getByLabel("Incident type identifier", { exact: true }).fill(incidentTypeId);
  await rule.getByLabel("Summary template", { exact: true }).fill("Browser incident");
  await save(page, rule);
  await rule.getByRole("button", { name: "Validate saved version", exact: true }).click();
  await expect(rule.getByRole("button", { name: "Activate saved rule" })).toBeEnabled();
  const end = new Date(Date.now() - 1000);
  await rule.getByLabel("History start (UTC)").fill(new Date(end.getTime() - 60000).toISOString());
  await rule.getByLabel("History end (UTC)").fill(end.toISOString());
  await rule.getByRole("button", { name: "Run against history", exact: true }).click();
  await expect(rule.getByText("Completed", { exact: true }).last()).toBeVisible({ timeout: 60000 });
  await confirm(page, "Activate saved rule");
  await expect(rule.getByText("Active rules", { exact: true })).toBeVisible();
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: `${directory}/desktop.png`, fullPage: true });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByLabel("Language").selectOption("ru");
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: `${directory}/mobile.png`, fullPage: true });
  expect(errors).toEqual([]);
  await writeFile(
    `${directory}/browser.json`,
    JSON.stringify(
      {
        importMs,
        importRows: devices.length,
        floorIds: floors,
        zoneIds: zones,
        siteId,
        incidentTypeId,
        zoneId,
        deviceId,
        ruleId,
        planId,
        checks: [
          "real PKCE with MFA",
          "connector registration and certificate renewal with external PKI",
          "one-off maintenance setup and explicit completion",
          "SLA and escalation policy configuration",
          "unsupported upload rejected",
          "foreign floor denied to scoped administrator",
          "new connector normalization mapping",
          "site, building, three floors and twenty zones",
          "50000-row import through the browser within five minutes",
          "existing import row updates without duplication",
          "private versioned floor plan",
          "validated device import",
          "incident type",
          "response form validation and publication",
          "rule validation, historical dry run and atomic activation",
          "ru/en and accessibility",
        ],
      },
      null,
      2,
    ),
  );
});

test("scoped administrator cannot read a foreign floor through the UI", async ({ page }) => {
  const f = JSON.parse(await readFile(`${directory}/fixture.json`, "utf8")) as Fixture;
  await login(page, { ...f, username: f.scopedUsername });
  await open(page, "Retention");
  await expect(page.getByText(/requires access to the entire tenant/u)).toBeVisible();
  await open(page, "Floor plans");
  await page.getByLabel("Floor identifier").fill(f.foreignFloorId);
  const response = page.waitForResponse((response) => response.url().endsWith(`/v1/locations/${f.foreignFloorId}`));
  await page.getByRole("button", { name: "Select record", exact: true }).click();
  expect((await response).status()).toBe(403);
  await expect(page.getByText(/Previously loaded administrative data has been hidden/u)).toBeVisible();
  await expect(page.getByRole("img")).toHaveCount(0);
});
