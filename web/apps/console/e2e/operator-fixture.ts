import { expect, type Page, type WebSocketRoute } from "@playwright/test";
import { issuer, jwks, nonce, state, tokens } from "./identity";
import type { components } from "@psim/api-client";
type Incident = components["schemas"]["psim.incident.v1.Incident"];
export async function operatorFixture(page: Page, count = 1) {
  const credentials = await tokens({ roles: ["supervisor"] });
  let incident: Incident = {
    incidentId: "00000000-0000-4000-8000-000000000001",
    version: "1",
    title: "Operational incident",
    priority: "PRIORITY_P1",
    state: "INCIDENT_STATE_NEW",
    siteId: "site",
    operational: { conditionKnown: true, active: true, acknowledged: false, resolveDue: "2030-01-01T00:00:00Z" },
  };
  let peer: WebSocketRoute | undefined;
  let escalated = false;
  let connected = true;
  const writes: { path: string; body: Record<string, unknown>; key: string; version: string }[] = [];
  const rows = Array.from({ length: count }, (_, i) => ({
    ...incident,
    incidentId: `00000000-0000-4000-8000-${String(i + 1).padStart(12, "0")}`,
    title: i === 0 ? incident.title : `Incident ${String(i + 1)}`,
    priority: i % 2 ? "PRIORITY_P2" : "PRIORITY_P1",
  }));
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
  await page.route("**/v1/**", async (route) => {
    const request = route.request();
    const path = new URL(request.url()).pathname;
    const body = request.method() === "GET" ? {} : (request.postDataJSON() as Record<string, unknown>);
    if (request.method() !== "GET")
      writes.push({
        path,
        body,
        key: request.headers()["idempotency-key"] ?? "",
        version: request.headers()["if-match"] ?? "",
      });
    let response: unknown = {};
    if (path === "/v1/feed/incidents") response = { incidents: rows, resumeToken: "snapshot" };
    else if (path.endsWith("/operations")) response = { incident, slaRisk: escalated ? "breached" : "on-time" };
    else if (path.endsWith("/run"))
      response = {
        run: {
          runId: "run",
          version: "1",
          planVersion: 1,
          activeStepIds: ["check"],
          steps: [{ stepId: "check", state: "STEP_STATE_ACTIVE" }],
        },
        plan: {
          name: "Response plan",
          steps: [
            {
              stepId: "check",
              title: "Safety",
              checklist: { items: [{ itemId: "verify", text: "Verify safety", required: true }] },
              mandatory: true,
            },
          ],
        },
      };
    else if (path.endsWith("/timeline")) response = { entries: [{ entryId: "entry", text: "Incident created" }] };
    else if (path.endsWith("/signals")) response = { signals: [] };
    else if (path === "/v1/me/notifications")
      response = {
        notifications: escalated
          ? [{ notificationId: "notice", version: "1", template: "SLA escalation", incidentId: incident.incidentId }]
          : [],
      };
    else if (path.startsWith("/v1/devices/"))
      response = {
        device: { deviceId: path.split("/")[3], deviceTypeId: "type" },
        deviceType: { capabilities: [{ code: "arm", name: "Arm", consequence: "System arms" }] },
      };
    else if (path === "/v1/commands:preview") response = { fingerprint: "preview", consequence: "System arms" };
    else if (path === "/v1/commands" && request.method() === "POST")
      response = { command: { commandId: body.commandId, state: "COMMAND_STATE_EXECUTED" } };
    else if (path.endsWith(":acknowledge") || path.endsWith(":start-work")) {
      incident = {
        ...incident,
        version: String(Number(incident.version) + 1),
        state: path.endsWith(":acknowledge") ? "INCIDENT_STATE_ACKNOWLEDGED" : "INCIDENT_STATE_IN_PROGRESS",
      };
      response = { incident };
    }
    await route.fulfill({ contentType: "application/json", body: JSON.stringify(response) });
  });
  await page.routeWebSocket("**/v1/realtime", (socket) => {
    peer = socket;
    socket.onMessage((data) => {
      const frame = JSON.parse(String(data)) as Record<string, unknown>;
      if (frame.authenticate && connected)
        socket.send(
          JSON.stringify({
            authenticated: {
              userId: "operator",
              expiresIn: 300,
              supportedStreams: ["STREAM_INCIDENTS", "STREAM_LOCATION_STATUS"],
            },
          }),
        );
      if (frame.subscribe)
        socket.send(JSON.stringify({ subscribed: { subscriptionId: "feed", resumeToken: "snapshot" } }));
      if (frame.ping) socket.send(JSON.stringify({ pong: {} }));
    });
  });
  await page.addInitScript(
    ({ issuer, nonce, state }) => {
      localStorage.setItem("psim.ui", JSON.stringify({ language: "en" }));
      sessionStorage.setItem(
        "psim.pkce",
        JSON.stringify({
          state,
          nonce,
          verifier: "v".repeat(43),
          issuer,
          issued: Date.now(),
          redirect: location.origin + "/",
          returnTo: "/",
        }),
      );
    },
    { issuer, nonce, state },
  );
  await page.goto(`/?code=code&state=${state}`);
  await expect(page.getByTestId("connection-status")).toHaveAttribute("data-status", "open");
  return {
    writes,
    disconnect: () => {
      connected = false;
      void peer?.close();
    },
    resume: () => {
      connected = true;
      void peer?.close();
    },
    escalate: () => {
      escalated = true;
      incident = { ...incident, version: String(Number(incident.version) + 1), escalationLevel: 2 };
      peer?.send(
        JSON.stringify({
          delta: {
            subscriptionId: "feed",
            resumeToken: "escalated",
            incidentEvent: { incidentEscalated: { incident } },
          },
        }),
      );
    },
    deliver: (i: number) => {
      const next = {
        ...incident,
        incidentId: `10000000-0000-4000-8000-${String(i).padStart(12, "0")}`,
        title: `Arrival ${String(i)}`,
        version: "1",
      };
      peer?.send(
        JSON.stringify({
          delta: {
            subscriptionId: "feed",
            resumeToken: `arrival-${String(i)}`,
            incidentEvent: { incidentCreated: { incident: next } },
          },
        }),
      );
    },
  };
}
