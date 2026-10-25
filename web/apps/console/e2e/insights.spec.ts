import { expect, test, type Page } from "@playwright/test";
import AxeBuilder from "@axe-core/playwright";
import { issuer, jwks, nonce, state, tokens } from "./identity";
async function session(page: Page, audit: boolean, language: "en" | "ru") {
  const roles = [audit ? "auditor" : "manager"],
    path = audit ? "/audit" : "/reports";
  const credentials = await tokens({ roles });
  await page.route("**/config.js", (route) =>
    route.fulfill({ contentType: "application/javascript", body: `window.psimConfig=${JSON.stringify({ issuer })};` }),
  );
  await page.route(`${issuer}/protocol/openid-connect/**`, (route) =>
    route.fulfill({
      contentType: "application/json",
      headers: { "Access-Control-Allow-Origin": "*" },
      body: JSON.stringify(route.request().url().endsWith("/certs") ? jwks : credentials),
    }),
  );
  await page.route("**/v1/me", (route) => route.fulfill({ json: { userId: "reader", roles, siteIds: ["*"] } }));
  await page.route("**/v1/audit/records?**", (route) =>
    route.fulfill({
      json: {
        records: [
          {
            auditSeq: "9007199254740993",
            occurredAt: new Date().toISOString(),
            entry: {
              actor: { id: "actor" },
              action: "incident.acknowledge",
              objectId: "incident",
              details: { safe: "<script>text only</script>" },
            },
          },
        ],
      },
    }),
  );
  await page.route("**/v1/audit:verify", (route) => route.fulfill({ json: { intact: true, recordsChecked: "1" } }));
  await page.route("**/v1/reports/incidents?**", (route) =>
    route.fulfill({
      json: {
        totals: { incidentsCreated: "10", incidentsResolved: "10", mtta: "60s", mttr: "120s", ackSlaEvaluated: "0" },
        rows: [{ group: "site", incidentsCreated: "10", incidentsResolved: "10", mtta: "60s", mttr: "120s" }],
        generatedAt: new Date().toISOString(),
      },
    }),
  );
  await page.addInitScript(
    ({ issuer, nonce, state, path, language }) => {
      localStorage.setItem("psim.ui", JSON.stringify({ language, theme: language === "en" ? "light" : "dark" }));
      sessionStorage.setItem(
        "psim.pkce",
        JSON.stringify({
          state,
          nonce,
          verifier: "v".repeat(43),
          issuer,
          issued: Date.now(),
          redirect: location.origin + path,
          returnTo: path,
        }),
      );
    },
    { issuer, nonce, state, path, language },
  );
  await page.goto(`${path}?code=code&state=${state}`);
}
for (const language of ["en", "ru"] as const)
  for (const audit of [true, false]) {
    test(`@gate audit and reporting accessibility ${language} ${audit ? "audit" : "report"}`, async ({ page }) => {
      await session(page, audit, language);
      const label =
        language === "en" ? (audit ? "Search audit" : "Build report") : audit ? "Найти записи" : "Построить отчёт";
      await page.getByRole("button", { name: label, exact: true }).click();
      if (audit) {
        await expect(page.locator("tbody")).toContainText("9007199254740993");
        await page.locator("summary").click();
        await expect(page.locator("details")).toContainText("<script>text only</script>");
        await page
          .getByRole("button", { name: language === "en" ? "Verify integrity" : "Проверить целостность", exact: true })
          .click();
        await expect(page.getByTestId("integrity-result")).toContainText(
          language === "en" ? "No signed checkpoint" : "Нет подписанной контрольной точки",
        );
      } else {
        await expect(page.getByTestId("total-incidentsCreated")).toHaveText("10");
        await expect(page.getByRole("img")).toHaveCount(2);
      }
      expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
      await page.setViewportSize({ width: 390, height: 844 });
      expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
      expect(await page.evaluate(() => document.documentElement.scrollWidth <= window.innerWidth)).toBe(true);
    });
  }
