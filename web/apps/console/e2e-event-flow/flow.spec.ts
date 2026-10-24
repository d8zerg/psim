import { readFile, writeFile, mkdir, access } from "node:fs/promises";
import { expect, test } from "@playwright/test";
const directory = "/repo/build/event-flow-test";
async function marker(name: string) {
  await writeFile(`${directory}/${name}`, "ready");
}
async function inputs(name: string): Promise<{ event_id: string; occurred_ns: number; trace_id: string }[]> {
  await expect
    .poll(
      async () => {
        try {
          await access(`${directory}/${name}.json`);
          return true;
        } catch {
          return false;
        }
      },
      { timeout: 30_000 },
    )
    .toBe(true);
  return JSON.parse(await readFile(`${directory}/${name}.json`, "utf8")) as {
    event_id: string;
    occurred_ns: number;
    trace_id: string;
  }[];
}
test("simulator reaches the browser through Kafka, resumes and resynchronizes after API restart", async ({
  page,
  context,
  request,
}) => {
  await mkdir(directory, { recursive: true });
  const renders = new Map<string, number>();
  await page.exposeFunction("recordRender", (id: string, at: number) => {
    if (id !== "" && !renders.has(id)) {
      renders.set(id, at);
    }
  });
  await page.addInitScript(() => {
    localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
    const sockets: WebSocket[] = [];
    const NativeSocket = window.WebSocket;
    window.WebSocket = class extends NativeSocket {
      constructor(url: string | URL, protocols?: string | string[]) {
        super(url, protocols);
        sockets.push(this);
      }
    };
    (window as unknown as { testSockets: WebSocket[] }).testSockets = sockets;
    const observer = new MutationObserver(() => {
      document.querySelectorAll<HTMLElement>("[data-event-id][data-rendered-at]").forEach((row) => {
        const record = (window as unknown as { recordRender: (id: string, at: number) => Promise<void> }).recordRender;
        void record(row.dataset.eventId ?? "", Number(row.dataset.renderedAt));
      });
    });
    observer.observe(document, { subtree: true, attributes: true, childList: true });
  });
  let snapshots = 0;
  page.on("request", (request) => {
    if (request.url().endsWith("/v1/feed/incidents")) {
      snapshots += 1;
    }
  });
  await page.goto("/");
  await page.getByRole("button", { name: "Sign in" }).click();
  await page.locator("#username").fill("operator1");
  await page.locator("#password").fill("psim-dev-Passw0rd");
  const tokenResponse = page.waitForResponse((response) => response.url().endsWith("/protocol/openid-connect/token"));
  await page.locator("#kc-login").click();
  let userToken = (await (await tokenResponse).json()) as { access_token: string };
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open", { timeout: 30_000 });
  expect(page.url()).not.toContain("code=");
  expect((await request.get("http://api-gateway:8080/v1/feed/incidents")).status()).toBe(401);
  expect(
    (
      await request.get("http://api-gateway:8080/v1/feed/incidents", { headers: { Authorization: "Bearer invalid" } })
    ).status(),
  ).toBe(401);
  const credentials = await request.post("http://keycloak:8080/realms/psim/protocol/openid-connect/token", {
    form: { grant_type: "client_credentials", client_id: "psim-services", client_secret: "psim-dev-services-secret" },
  });
  let serviceToken = (await credentials.json()) as { access_token: string };
  expect(
    (
      await request.get("http://api-gateway:8080/v1/feed/incidents", {
        headers: { Authorization: `Bearer ${serviceToken.access_token}` },
      })
    ).status(),
  ).toBe(403);
  await marker("warmup-ready");
  const warmup = await inputs("warmup");
  await expect(page.locator(`[data-event-id="${warmup.at(-1)?.event_id ?? ""}"]`)).toBeVisible({ timeout: 30_000 });
  await marker("measure-ready");
  const measured = await inputs("input");
  await expect.poll(() => measured.every((event) => renders.has(event.event_id)), { timeout: 30_000 }).toBe(true);
  const baseline = measured.map((event) => ({
    ...event,
    rendered_ms: renders.get(event.event_id) ?? 0,
    browser_ms: (renders.get(event.event_id) ?? 0) - event.occurred_ns / 1_000_000,
  }));
  await writeFile(`${directory}/browser.json`, JSON.stringify(baseline));
  expect(snapshots).toBe(1);
  // Close the browser connection, publish while it is away, then resume from its last token.
  await context.setOffline(true);
  await page.evaluate(() => {
    (window as unknown as { testSockets: WebSocket[] }).testSockets.forEach((socket) => {
      socket.close();
    });
  });
  await marker("offline-ready");
  const offline = await inputs("offline");
  const outage = Number(process.env.PSIM_SHELL_OUTAGE_MS ?? 0);
  const outageStarted = Date.now();
  await writeFile(`${directory}/outage-started.json`, JSON.stringify({ requestedMs: outage }));
  if (outage > 0) await new Promise((resolve) => setTimeout(resolve, outage));
  const refreshed =
    outage >= 300000
      ? page.waitForResponse((response) => response.url().endsWith("/protocol/openid-connect/token") && response.ok(), {
          timeout: 45000,
        })
      : undefined;
  await context.setOffline(false);
  if (refreshed) {
    userToken = (await (await refreshed).json()) as { access_token: string };
    const renewedService = await request.post("http://keycloak:8080/realms/psim/protocol/openid-connect/token", {
      form: { grant_type: "client_credentials", client_id: "psim-services", client_secret: "psim-dev-services-secret" },
    });
    expect(renewedService.ok()).toBe(true);
    serviceToken = (await renewedService.json()) as { access_token: string };
  }
  expect(Date.now() - outageStarted).toBeGreaterThanOrEqual(outage);
  await writeFile(
    `${directory}/outage.json`,
    JSON.stringify({ requestedMs: outage, elapsedMs: Date.now() - outageStarted }),
  );
  await page.evaluate(() => {
    window.dispatchEvent(new Event("online"));
  });
  await expect(page.locator(`[data-event-id="${offline.at(-1)?.event_id ?? ""}"]`)).toBeVisible({ timeout: 30_000 });
  expect(snapshots).toBe(1);
  const previousConnections = await page.evaluate(
    () => (window as unknown as { testSockets: WebSocket[] }).testSockets.length,
  );
  await marker("restart-ready");
  await expect
    .poll(() => page.evaluate(() => (window as unknown as { testSockets: WebSocket[] }).testSockets.length), {
      timeout: 30_000,
    })
    .toBeGreaterThan(previousConnections);
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open");
  await expect(page.getByTestId("incident-row").first()).toBeVisible();
  expect(snapshots).toBe(1);
  await marker("after-restart-ready");
  const afterRestart = await inputs("after-restart");
  await expect(page.locator(`[data-event-id="${afterRestart[0]?.event_id ?? ""}"]`)).toBeVisible();
  expect((await request.get("http://api-gateway:8080/v1/feed/incidents")).status()).toBe(401);
  expect(
    (
      await request.get("http://api-gateway:8080/v1/feed/incidents", { headers: { Authorization: "Bearer invalid" } })
    ).status(),
  ).toBe(401);
  expect(
    (
      await request.get("http://api-gateway:8080/v1/feed/incidents", {
        headers: { Authorization: `Bearer ${serviceToken.access_token}` },
      })
    ).status(),
  ).toBe(403);
  expect(
    (
      await request.get("http://api-gateway:8080/v1/missing", {
        headers: { Authorization: `Bearer ${userToken.access_token}` },
      })
    ).status(),
  ).toBe(404);
  const feedResponse = await request.get("http://api-gateway:8080/v1/feed/incidents", {
    headers: { Authorization: `Bearer ${userToken.access_token}` },
  });
  const feed = (await feedResponse.json()) as { resumeToken: string };
  const frames = await page.evaluate(
    async ({ accessToken, resumeToken }) => {
      const url = `ws://${location.host}/v1/realtime`;
      for (const message of [
        "{broken",
        JSON.stringify({ subscribe: { subscriptionId: "unauthenticated" } }),
        JSON.stringify({ authenticate: { accessToken: "invalid" } }),
      ]) {
        await new Promise<void>((resolve, reject) => {
          const socket = new WebSocket(url);
          const timeout = setTimeout(() => {
            socket.close();
            reject(new Error("invalid session remained open"));
          }, 5000);
          socket.onopen = () => {
            socket.send(message);
          };
          socket.onclose = () => {
            clearTimeout(timeout);
            resolve();
          };
        });
      }
      return await new Promise<string[]>((resolve, reject) => {
        const seen: string[] = [];
        const socket = new WebSocket(url);
        const timeout = setTimeout(() => {
          socket.close();
          reject(new Error("protocol response timed out"));
        }, 5000);
        socket.onopen = () => {
          socket.send(JSON.stringify({ authenticate: { accessToken } }));
        };
        socket.onmessage = (event: MessageEvent<string>) => {
          const frame = JSON.parse(event.data) as Record<string, unknown>;
          if ("authenticated" in frame) {
            seen.push("authenticated");
            socket.send(JSON.stringify({ ping: {} }));
          } else if ("pong" in frame) {
            seen.push("pong");
            const token = JSON.parse(resumeToken) as { offsets: Record<string, number> };
            token.offsets["unknown-partition"] = 0;
            socket.send(
              JSON.stringify({
                subscribe: {
                  subscriptionId: "invalid",
                  stream: "STREAM_INCIDENTS",
                  resumeToken: JSON.stringify(token),
                },
              }),
            );
          } else if ("resyncRequired" in frame) {
            seen.push("resyncRequired");
            if (seen.length === 3) {
              socket.send(
                JSON.stringify({
                  subscribe: { subscriptionId: "invalid", stream: "STREAM_INCIDENTS", resumeToken: "expired" },
                }),
              );
            } else {
              socket.send(JSON.stringify({ unsubscribe: { subscriptionId: "invalid" } }));
              socket.close();
            }
          }
        };
        socket.onclose = () => {
          clearTimeout(timeout);
          resolve(seen);
        };
      });
    },
    { accessToken: userToken.access_token, resumeToken: feed.resumeToken },
  );
  expect(frames).toEqual(["authenticated", "pong", "resyncRequired", "resyncRequired"]);
  await page.screenshot({ path: `${directory}/incident-feed.png`, fullPage: true });
  await marker("browser-done");
});
