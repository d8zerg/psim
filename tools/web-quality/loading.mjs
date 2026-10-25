import { createServer } from "node:http";
import { readFile, writeFile, mkdir } from "node:fs/promises";
import { resolve, extname, sep } from "node:path";
import { gzipSync } from "node:zlib";
import { createRequire } from "node:module";
import {
  tokens,
  jwks,
  nonce,
  state,
} from "../../web/apps/console/e2e/identity.ts";
const root = resolve(import.meta.dirname, "../..");
const { chromium } = createRequire(import.meta.url)(
  resolve(root, "web/apps/console/node_modules/@playwright/test"),
);
const variants = JSON.parse(
  await readFile(resolve(root, "build/web-quality/variants.json"), "utf8"),
);
let current = variants[0];
let blocked = "";
let fixtureRole = "operator";
const codes = new Map();
const mime = {
  ".js": "text/javascript",
  ".json": "application/json",
  ".html": "text/html",
  ".css": "text/css",
  ".woff2": "font/woff2",
  ".svg": "image/svg+xml",
  ".png": "image/png",
};
const rolesFor = (role) =>
  role === "all"
    ? ["operator", "supervisor", "admin", "integrator", "auditor", "manager"]
    : [role];
let origin;
const server = createServer(async (request, response) => {
  try {
    const url = new URL(request.url, origin);
    const role = fixtureRole;
    const json = (value) => {
      response.setHeader("Content-Type", "application/json");
      response.setHeader("Cache-Control", "no-store");
      response.end(JSON.stringify(value));
    };
    if (url.pathname === "/config.js") {
      response.setHeader("Content-Type", "text/javascript");
      response.setHeader("Cache-Control", "no-store");
      response.end(
        `window.psimConfig={issuer:${JSON.stringify(origin + "/identity")}};`,
      );
      return;
    }
    if (url.pathname.endsWith("/certs")) {
      json(jwks);
      return;
    }
    if (url.pathname.endsWith("/auth")) {
      const redirect = new URL(url.searchParams.get("redirect_uri"));
      if (redirect.origin !== origin) throw new Error("Invalid redirect");
      const code = crypto.randomUUID();
      codes.set(code, url.searchParams.get("nonce"));
      redirect.searchParams.set("code", code);
      redirect.searchParams.set("state", url.searchParams.get("state"));
      response.writeHead(302, { Location: redirect.href });
      response.end();
      return;
    }
    if (url.pathname.endsWith("/token")) {
      let body = "";
      for await (const part of request) body += part;
      const code = new URLSearchParams(body).get("code");
      json(
        await tokens({
          roles: rolesFor(role),
          nonce: codes.get(code) ?? nonce,
          access: { iss: origin + "/identity" },
          identity: { iss: origin + "/identity" },
        }),
      );
      return;
    }
    if (url.pathname === "/v1/me") {
      json({
        userId: "operator",
        tenantId: "tenant",
        displayName: "Loading fixture",
        roles: rolesFor(role),
        siteIds: ["*"],
        zoneIds: [],
        actions: [],
      });
      return;
    }
    if (url.pathname.startsWith("/v1/")) {
      if (url.pathname === "/v1/audit:verify")
        json({ intact: true, recordsChecked: "1" });
      else if (url.pathname === "/v1/reports/incidents")
        json({
          totals: {
            incidentsCreated: "10",
            incidentsResolved: "10",
            mtta: "60s",
            mttr: "120s",
          },
          rows: [{ group: "site", incidentsCreated: "10" }],
          generatedAt: new Date().toISOString(),
        });
      else
        json({
          incidents: [],
          notifications: [],
          locations: [],
          connectors: [],
          devices: [],
          resumeToken: "fixture",
          records: [],
          mappingRuleSets: [],
        });
      return;
    }
    const path =
      url.pathname === "/" || !extname(url.pathname)
        ? "/index.html"
        : url.pathname;
    if (blocked && path === blocked) {
      response.writeHead(503, { "Cache-Control": "no-store" });
      response.end("unavailable");
      return;
    }
    const file = resolve(current.directory, "." + path);
    if (!file.startsWith(resolve(current.directory) + sep))
      throw new Error("Invalid static path");
    const bytes = await readFile(file);
    response.setHeader(
      "Content-Type",
      mime[extname(file)] ?? "application/octet-stream",
    );
    response.setHeader(
      "Cache-Control",
      path.startsWith("/assets/")
        ? "public,max-age=31536000,immutable"
        : "no-store",
    );
    response.setHeader("Content-Encoding", "gzip");
    response.end(gzipSync(bytes, { level: 9 }));
  } catch {
    response.writeHead(404, { "Cache-Control": "no-store" });
    response.end("missing");
  }
});
server.on("upgrade", (_request, socket) => {
  socket.end("HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\n\r\n");
});
await new Promise((resolve) => server.listen(4187, "127.0.0.1", resolve));
origin = "http://127.0.0.1:4187";
const browser = await chromium.launch();
const results = [];
const navigations = [];
const routes = [
  ["/", "operator", ".operator-panels"],
  ["/admin", "admin", ".admin-workspace"],
  ["/integrations", "integrator", ".admin-workspace"],
  ["/audit", "auditor", ".insights-fields"],
  ["/reports", "manager", ".insights-fields"],
];
async function prepare(context, language, role) {
  fixtureRole = role;
  await context.addCookies([{ name: "role", value: role, url: origin }]);
  await context.addInitScript(
    ({ origin, nonce, state, language }) => {
      localStorage.setItem("psim.ui", JSON.stringify({ language }));
      if (new URL(location.href).searchParams.get("code") === "fixture")
        sessionStorage.setItem(
          "psim.pkce",
          JSON.stringify({
            state,
            nonce,
            verifier: "v".repeat(43),
            issuer: origin + "/identity",
            issued: Date.now(),
            redirect: origin + location.pathname,
            returnTo: location.pathname,
          }),
        );
    },
    { origin, nonce, state, language },
  );
}
async function open(page, path, selector) {
  const started = Date.now();
  await page.goto(`${origin}${path}?code=fixture&state=${state}`);
  try {
    await page.locator(selector).first().waitFor();
  } catch (error) {
    await writeFile(
      resolve(root, "build/web-quality/loading-failure.txt"),
      await page.locator("body").innerText(),
    );
    throw error;
  }
  const elapsed = Date.now() - started;
  const resources = await page.evaluate(() =>
    performance
      .getEntriesByType("resource")
      .filter((r) => /\/assets\/.*\.(js|json)(?:\?|$)/u.test(r.name))
      .map((r) => ({
        url: new URL(r.name).pathname,
        transfer: r.transferSize,
        encoded: r.encodedBodySize,
      })),
  );
  return {
    elapsed,
    resources,
    transfer: resources.reduce((n, r) => n + r.transfer, 0),
    encoded: resources.reduce((n, r) => n + r.encoded, 0),
  };
}
try {
  for (const variant of variants.filter((v) => v.entry !== "design")) {
    current = variant;
    for (const language of ["en", "ru"])
      for (const [path, role, selector] of routes) {
        const samples = [];
        for (let iteration = 0; iteration < 3; iteration++) {
          const context = await browser.newContext();
          await prepare(context, language, role);
          const page = await context.newPage();
          const errors = [];
          page.on("pageerror", (e) => errors.push(e.message));
          const cold = await open(page, path, selector);
          const warm = await open(page, path, selector);
          if (warm.resources.some((r) => r.transfer !== 0))
            throw new Error(
              `Warm assets were fetched: ${variant.name} ${path}`,
            );
          const actions = {};
          if (path === "/admin") {
            const started = Date.now();
            await page
              .getByRole("button", {
                name: language === "en" ? "Create" : "Создать",
                exact: true,
              })
              .click();
            await page
              .getByRole("textbox", {
                name: language === "en" ? "Name" : "Название",
                exact: true,
              })
              .waitFor();
            actions.editor = Date.now() - started;
          }
          if (path === "/reports") {
            const started = Date.now();
            await page
              .getByRole("button", {
                name: language === "en" ? "Build report" : "Построить отчёт",
                exact: true,
              })
              .click();
            await page.getByTestId("total-incidentsCreated").waitFor();
            actions.report = Date.now() - started;
          }
          if (errors.length) throw new Error(errors.join("\n"));
          samples.push({ cold, warm, actions });
          await context.close();
        }
        results.push({ variant: variant.name, path, role, language, samples });
        console.log(
          `${variant.name} ${path} ${language}: ${samples.map((s) => s.cold.elapsed + "/" + s.warm.elapsed).join(",")}`,
        );
      }
  }

  const links = {
    en: {
      "/": "Operations console",
      "/admin": "Administration",
      "/integrations": "Integrations",
      "/audit": "Audit",
      "/reports": "Incident report",
    },
    ru: {
      "/": "Рабочее место оператора",
      "/admin": "Администрирование",
      "/integrations": "Интеграции",
      "/audit": "Аудит",
      "/reports": "Отчёт по инцидентам",
    },
  };
  async function navigate(page, path, language, selector) {
    await page.evaluate(() => performance.clearResourceTimings());
    const started = Date.now();
    await page
      .getByRole("link", { name: links[language][path], exact: true })
      .click();
    await page.locator(selector).first().waitFor();
    const elapsed = Date.now() - started;
    const resources = await page.evaluate(() =>
      performance
        .getEntriesByType("resource")
        .filter((r) => /\/assets\/.*\.(js|json)(?:\?|$)/u.test(r.name))
        .map((r) => ({
          url: new URL(r.name).pathname,
          transfer: r.transferSize,
          encoded: r.encodedBodySize,
        })),
    );
    return {
      elapsed,
      resources,
      transfer: resources.reduce((sum, r) => sum + r.transfer, 0),
    };
  }
  for (const variant of variants.filter((v) => v.entry !== "design")) {
    current = variant;
    for (const language of ["en", "ru"])
      for (const [path, , selector] of routes.slice(1)) {
        const samples = [];
        for (let iteration = 0; iteration < 3; iteration++) {
          const context = await browser.newContext();
          await prepare(context, language, "all");
          const page = await context.newPage();
          await open(page, "/", ".operator-panels");
          const cold = await navigate(page, path, language, selector);
          await navigate(page, "/", language, ".operator-panels");
          const warm = await navigate(page, path, language, selector);
          if (warm.resources.some((r) => r.transfer !== 0))
            throw new Error("Cached module was transferred again");
          samples.push({ cold, warm });
          await context.close();
        }
        navigations.push({ variant: variant.name, path, language, samples });
        console.log(
          `navigation ${variant.name} ${path} ${language}: ${samples.map((s) => s.cold.elapsed + "/" + s.warm.elapsed).join(",")}`,
        );
      }
  }

  const qualification = [];
  current = variants.find((v) => v.entry === "design");
  {
    const context = await browser.newContext();
    await prepare(context, "en", "all");
    const page = await context.newPage();
    await page.goto(origin + "/design-system.html");
    await page.getByRole("main").first().waitFor();
    await context.close();
    qualification.push(
      "independent design artifact renders without the application entry",
    );
  }
  const byName = (name) => variants.find((v) => v.name === name);
  async function authenticated(path, role = "all", variant = "inline") {
    current = byName(variant);
    const context = await browser.newContext();
    await prepare(context, "en", role);
    const page = await context.newPage();
    await open(
      page,
      path,
      path === "/reports" || path === "/audit"
        ? ".insights-fields"
        : ".admin-workspace",
    );
    return { context, page };
  }
  async function recover(page, selector) {
    await page
      .getByRole("button", { name: "Refresh data", exact: true })
      .click();
    await page.getByRole("button", { name: "Sign in", exact: true }).click();
    await page.locator(selector).first().waitFor();
  }
  {
    const { context, page } = await authenticated("/reports", "manager");
    const requests = [];
    page.on("request", (r) => {
      if (page.url().startsWith(origin + "/admin")) requests.push(r.url());
    });
    const manifest = JSON.parse(
      await readFile(resolve(current.directory, ".vite/manifest.json"), "utf8"),
    );
    await page.goto(`${origin}/admin?code=fixture&state=${state}`);
    await page
      .getByRole("heading", { name: "Access denied", exact: true })
      .waitFor();
    if (
      requests.some((url) =>
        url.endsWith("/" + manifest["src/admin/page.tsx"].file),
      )
    )
      throw new Error("Forbidden module was fetched");
    if (
      requests.some((value) => {
        const path = new URL(value).pathname;
        return path.startsWith("/v1/") && path !== "/v1/me";
      })
    )
      throw new Error("Forbidden module API was requested");
    qualification.push("role gating precedes code and API loading");
    await context.close();
  }
  {
    const { context, page } = await authenticated("/reports");
    const manifest = JSON.parse(
      await readFile(resolve(current.directory, ".vite/manifest.json"), "utf8"),
    );
    blocked = "/" + manifest["src/admin/page.tsx"].file;
    await page
      .getByRole("link", { name: "Administration", exact: true })
      .click();
    await page
      .getByRole("heading", { name: "Could not open this page", exact: true })
      .waitFor();
    blocked = "";
    await recover(page, ".admin-workspace");
    qualification.push(
      "failed chunk recovers through explicit reload and sign-in",
    );
    await context.close();
  }
  {
    const { context, page } = await authenticated(
      "/reports",
      "all",
      "baseline",
    );
    current = byName("inline");
    await page
      .getByRole("link", { name: "Administration", exact: true })
      .click();
    await page
      .getByRole("heading", { name: "Could not open this page", exact: true })
      .waitFor();
    await recover(page, ".admin-workspace");
    qualification.push(
      "old document recovers after an artifact update with missing old chunks",
    );
    await context.close();
  }
  {
    const { context, page } = await authenticated(
      "/audit",
      "auditor",
      "selected",
    );
    const manifest = JSON.parse(
      await readFile(resolve(current.directory, ".vite/manifest.json"), "utf8"),
    );
    const dictionary = Object.values(manifest).find(
      (v) =>
        v.file?.endsWith(".json") && v.src?.includes("insights/locales/ru"),
    );
    if (!dictionary) throw new Error("Missing Russian insights dictionary");
    await page
      .getByLabel("Action prefix", { exact: true })
      .fill("retained draft");
    blocked = "/" + dictionary.file;
    await page
      .getByRole("combobox", { name: "Language", exact: true })
      .selectOption("ru");
    await page.getByRole("alert").waitFor();
    if (
      (await page.getByLabel("Action prefix", { exact: true }).inputValue()) !==
      "retained draft"
    )
      throw new Error("Draft was lost after dictionary failure");
    blocked = "";
    await page
      .getByRole("combobox", { name: "Language", exact: true })
      .selectOption("ru");
    await page
      .getByRole("heading", { name: "Журнал аудита", exact: true })
      .waitFor();
    if (
      (await page
        .getByLabel("Префикс действия", { exact: true })
        .inputValue()) !== "retained draft"
    )
      throw new Error("Draft was lost during language recovery");
    qualification.push(
      "dictionary failure preserves language and draft, then retries without reload",
    );
    await context.close();
  }
  console.log("qualification:", qualification.join("; "));
  await mkdir(resolve(root, "build/web-quality"), { recursive: true });
  await writeFile(
    resolve(root, "build/web-quality/loading.json"),
    JSON.stringify(
      { passed: true, qualification, runs: results, navigations },
      null,
      2,
    ) + "\n",
  );
} finally {
  await browser.close();
  await new Promise((resolve) => server.close(resolve));
}
