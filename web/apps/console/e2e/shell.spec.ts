import { expect, test, type Page } from "@playwright/test";
import AxeBuilder from "@axe-core/playwright";
import { issuer, jwks, nonce, state, tokens } from "./identity";
async function session(page: Page, roles: string[], path = "/") {
  let snapshots = 0;
  let sockets = 0;
  let connected = true;
  const peers: { close: () => void }[] = [];
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
  await page.route("**/v1/feed/incidents", (route) => {
    snapshots += 1;
    return route.fulfill({
      contentType: "application/json",
      body: JSON.stringify({
        incidents: [
          {
            incidentId: "incident",
            version: "1",
            title: "Live incident",
            priority: "PRIORITY_P2",
            state: "INCIDENT_STATE_NEW",
          },
        ],
        resumeToken: "snapshot",
      }),
    });
  });
  await page.routeWebSocket("**/v1/realtime", (socket) => {
    sockets += 1;
    peers.push(socket);
    socket.onMessage((data) => {
      const frame = JSON.parse(String(data)) as Record<string, unknown>;
      if (frame.authenticate && connected)
        socket.send(JSON.stringify({ authenticated: { userId: "operator", expiresIn: 300 } }));
      if (frame.subscribe)
        socket.send(JSON.stringify({ subscribed: { subscriptionId: "feed", resumeToken: "snapshot" } }));
      if (frame.ping) socket.send(JSON.stringify({ pong: {} }));
    });
  });
  await page.addInitScript(
    ({ issuer, nonce, state, path }) => {
      localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
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
    { issuer, nonce, state, path },
  );
  await page.goto(`${path}?code=code&state=${state}`);
  return {
    snapshots: () => snapshots,
    sockets: () => sockets,
    disconnect: () => {
      connected = false;
      peers.at(-1)?.close();
    },
    resume: () => {
      connected = true;
      peers.at(-1)?.close();
    },
  };
}
test("@gate shell preserves its live feed through navigation, back and logout", async ({ page }) => {
  const f = await session(page, ["supervisor"]);
  await expect(page.getByTestId("incident-row")).toHaveText(/Live incident/u);
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open");
  await page.getByRole("link", { name: "Incident report", exact: true }).click();
  await expect(page.getByRole("heading", { name: "Incident report" })).toBeVisible();
  await page.goBack();
  await expect(page.getByTestId("incident-row")).toBeVisible();
  expect(f.snapshots()).toBe(1);
  expect(f.sockets()).toBe(1);
  await expect(page.locator("#main-content")).toBeFocused();
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: "/repo/build/shell-desktop.png" });
  await page.setViewportSize({ width: 390, height: 844 });
  await page.getByLabel("Language").selectOption("ru");
  await expect(page.getByRole("button", { name: "Выйти" })).toBeVisible();
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  expect((await new AxeBuilder({ page }).analyze()).violations).toEqual([]);
  await page.screenshot({ path: "/repo/build/shell-mobile.png" });
  const top = await page.getByTestId("incident-row").evaluate((node) => node.getBoundingClientRect().top + scrollY);
  f.disconnect();
  await expect(page.getByTestId("connection-status")).not.toHaveAttribute("data-status", "open");
  const offlineTop = await page
    .getByTestId("incident-row")
    .evaluate((node) => node.getBoundingClientRect().top + scrollY);
  expect(Math.abs(offlineTop - top)).toBeLessThanOrEqual(1);
  f.resume();
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open");
  expect(f.snapshots()).toBe(1);

  await page.getByLabel("Язык").selectOption("en");

  await page.getByRole("button", { name: "Sign out" }).click();
  await expect(page.getByRole("button", { name: "Sign in" })).toBeVisible();
  await expect(page.getByTestId("incident-row")).toHaveCount(0);
  expect(await page.evaluate(() => Object.keys(sessionStorage))).not.toContain("psim.pkce");
});
test("@gate manager deep links reject manual module access without opening incident APIs", async ({ page }) => {
  const f = await session(page, ["manager"], "/reports");
  await expect(page.getByRole("heading", { name: "Incident report" })).toBeVisible();
  expect(f.snapshots()).toBe(0);
  expect(f.sockets()).toBe(0);
  await page.evaluate(() => {
    history.pushState(null, "", "/admin");
    window.dispatchEvent(new PopStateEvent("popstate"));
  });
  await expect(page.getByRole("heading", { name: "Access denied" })).toBeVisible();
  expect(f.snapshots()).toBe(0);
});
