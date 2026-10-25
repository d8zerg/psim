import { createApiClient, isProblem, ApiProblemError, type ApiClient, type components } from "@psim/api-client";
export type Schema = components["schemas"];
export type Incident = Schema["psim.incident.v1.Incident"];
export type Command = Schema["psim.response.v1.Command"];
export type CommandRequest = Schema["psim.api.v1.RequestCommandRequest"];
export type Step = Schema["psim.response.v1.Step"];
export type Run = Schema["psim.response.v1.ResponseRun"];
export type Location = Schema["psim.catalog.v1.Location"];
export type Device = Schema["psim.catalog.v1.Device"];
export type DeviceStatus = Schema["psim.api.v1.DeviceStatus"];
export class RequestFailure extends Error {
  constructor(
    readonly status: number,
    readonly code: string,
  ) {
    super(code);
  }
}
export function data<T>(response: { data?: T; error?: unknown; response: Response }): T {
  if (response.data !== undefined && response.response.ok) return response.data;
  if (isProblem(response.error)) throw new ApiProblemError(response.error);
  throw new RequestFailure(response.response.status, "COMMON_UNAVAILABLE");
}
export function statusOf(error: unknown): number {
  return error instanceof ApiProblemError ? error.problem.status : error instanceof RequestFailure ? error.status : 0;
}
export function headers(version?: string, id: string = crypto.randomUUID()) {
  return { "Idempotency-Key": id, ...(version === undefined ? {} : { "If-Match": `"${version}"` }) };
}
export type IncidentAction = "acknowledge" | "start-work" | "assign" | "resolve" | "reopen" | "close";
const actionPaths = {
  acknowledge: "/v1/incidents/{incidentId}:acknowledge",
  "start-work": "/v1/incidents/{incidentId}:start-work",
  assign: "/v1/incidents/{incidentId}:assign",
  resolve: "/v1/incidents/{incidentId}:resolve",
  reopen: "/v1/incidents/{incidentId}:reopen",
  close: "/v1/incidents/{incidentId}:close",
} as const;
export class OperatorApi {
  constructor(readonly client: ApiClient) {}
  async notifications(signal: AbortSignal, pageToken = "") {
    return data(
      await this.client.GET("/v1/me/notifications", { params: { query: { pageSize: 100, pageToken } }, signal }),
    );
  }
  async readNotification(id: string, version: string) {
    return data(
      await this.client.POST("/v1/me/notifications/{notificationId}:read", {
        params: { path: { notificationId: id } },
        headers: headers(version),
        body: {},
      }),
    );
  }
  async operations(id: string, signal: AbortSignal) {
    return data(
      await this.client.GET("/v1/incidents/{incidentId}/operations", {
        params: { path: { incidentId: id } },
        signal,
      }),
    );
  }
  async timeline(id: string, signal: AbortSignal, pageToken = "") {
    return data(
      await this.client.GET("/v1/incidents/{incidentId}/timeline", {
        params: { path: { incidentId: id }, query: { pageSize: 100, pageToken } },
        signal,
      }),
    );
  }
  async signals(id: string, signal: AbortSignal, pageToken = "") {
    return data(
      await this.client.GET("/v1/incidents/{incidentId}/signals", {
        params: { path: { incidentId: id }, query: { pageSize: 100, pageToken } },
        signal,
      }),
    );
  }
  async run(id: string, signal: AbortSignal): Promise<Schema["psim.api.v1.GetIncidentRunResponse"]> {
    const response = await this.client.GET("/v1/incidents/{incidentId}/run", {
      params: { path: { incidentId: id } },
      signal,
    });
    if (response.response.status === 404) return {};
    return data(response);
  }
  async commands(id: string, signal: AbortSignal, pageToken = "") {
    return data(
      await this.client.GET("/v1/commands", {
        params: { query: { incidentId: id, pageSize: 100, pageToken } },
        signal,
      }),
    );
  }
  async locations(signal: AbortSignal, parentId = "", pageToken = "") {
    return data(
      await this.client.GET("/v1/locations", { params: { query: { parentId, pageSize: 1000, pageToken } }, signal }),
    );
  }
  async devices(id: string, signal: AbortSignal, pageToken = "") {
    return data(
      await this.client.GET("/v1/devices", {
        params: { query: { locationId: id, pageSize: 1000, pageToken } },
        signal,
      }),
    );
  }
  async device(id: string, signal: AbortSignal) {
    return data(await this.client.GET("/v1/devices/{deviceId}", { params: { path: { deviceId: id } }, signal }));
  }
  async floorPlan(id: string, signal: AbortSignal) {
    return data(
      await this.client.GET("/v1/locations/{locationId}/floor-plan", {
        params: { path: { locationId: id } },
        parseAs: "blob",
        signal,
      }),
    );
  }
  async event(id: string, signal: AbortSignal) {
    return data(await this.client.GET("/v1/events/{eventId}", { params: { path: { eventId: id } }, signal }));
  }
  async locationStatus(id: string, signal: AbortSignal) {
    return data(
      await this.client.GET("/v1/feed/locations/{locationId}/status", {
        params: { path: { locationId: id } },
        signal,
      }),
    );
  }
  async duty(id: string, signal: AbortSignal): Promise<Schema["psim.api.v1.GetSiteDutyResponse"]> {
    const response = await this.client.GET("/v1/sites/{siteId}/duty", { params: { path: { siteId: id } }, signal });
    if (response.response.status === 404) return {};
    return data(response);
  }
  async setDuty(siteId: string, duty: Schema["psim.api.v1.SiteDuty"]) {
    return data(
      await this.client.PUT("/v1/sites/{siteId}/duty", {
        params: { path: { siteId: siteId } },
        body: duty,
        headers: headers(duty.version ?? "0"),
      }),
    );
  }
  async incidentAction(
    incident: Incident,
    action: IncidentAction,
    body: {
      reason?: string;
      assigneeId?: string;
      resolution?: Incident["resolution"];
      note?: string;
      knownResponseRunVersion?: string;
    } = {},
  ) {
    return data(
      await this.client.POST(actionPaths[action], {
        params: { path: { incidentId: incident.incidentId ?? "" } },
        headers: headers(incident.version),
        body:
          action === "assign"
            ? { assigneeId: body.assigneeId ?? "", reason: body.reason ?? "" }
            : action === "resolve"
              ? {
                  resolution: body.resolution ?? "RESOLUTION_UNSPECIFIED",
                  note: body.note ?? "",
                  knownResponseRunVersion: body.knownResponseRunVersion ?? "0",
                }
              : action === "reopen"
                ? { reason: body.reason ?? "" }
                : {},
      }),
    );
  }
  async comment(incident: Incident, text: string) {
    return data(
      await this.client.POST("/v1/incidents/{incidentId}/comments", {
        params: { path: { incidentId: incident.incidentId ?? "" } },
        headers: headers(incident.version),
        body: { text },
      }),
    );
  }
  async alarm(incident: Incident, action: string, reason: string, until?: string) {
    return data(
      await this.client.POST("/v1/incidents/{incidentId}:update-alarm", {
        params: { path: { incidentId: incident.incidentId ?? "" } },
        headers: headers(incident.version),
        body: { action, reason, ...(until ? { until } : {}) },
      }),
    );
  }
  async transfer(incident: Incident, recipientId: string, comment: string, transferId = "") {
    const result = data(
      await this.client.POST("/v1/incidents:transfer", {
        headers: headers(),
        body: {
          items: [
            {
              incidentId: incident.incidentId ?? "",
              expectedVersion: incident.version ?? "0",
              recipientId,
              comment,
              transferId,
            },
          ],
        },
      }),
    );
    const item = result.results?.[0];
    if (!item?.incident || item.errorCode) throw new RequestFailure(409, item?.errorCode ?? "COMMON_UNAVAILABLE");
    return item.incident;
  }
  async step(
    run: Run,
    stepId: string,
    action: "complete" | "skip" | "retry" | "override",
    result: Schema["psim.response.v1.StepResult"],
  ) {
    const paths = {
      complete: "/v1/runs/{runId}/steps/{stepId}:complete",
      skip: "/v1/runs/{runId}/steps/{stepId}:skip",
      retry: "/v1/runs/{runId}/steps/{stepId}:retry",
      override: "/v1/runs/{runId}/steps/{stepId}:override",
    } as const;
    return data(
      await this.client.POST(paths[action], {
        params: { path: { runId: run.runId ?? "", stepId: stepId } },
        headers: headers(run.version),
        body: action === "complete" ? { result } : action === "retry" ? {} : { reason: result.note ?? "" },
      }),
    );
  }
  async preview(command: CommandRequest) {
    return data(await this.client.POST("/v1/commands:preview", { headers: headers(), body: { command } }));
  }
  async send(command: CommandRequest) {
    return data(
      await this.client.POST("/v1/commands", { headers: headers(undefined, command.commandId), body: command }),
    );
  }
  async command(id: string, signal: AbortSignal) {
    return data(await this.client.GET("/v1/commands/{commandId}", { params: { path: { commandId: id } }, signal }));
  }
  async commandAction(command: Command, action: "approve" | "reject" | "cancel", fingerprint = "", reason = "") {
    const paths = {
      approve: "/v1/commands/{commandId}:approve",
      reject: "/v1/commands/{commandId}:reject",
      cancel: "/v1/commands/{commandId}:cancel",
    } as const;
    return data(
      await this.client.POST(paths[action], {
        params: { path: { commandId: command.commandId ?? "" } },
        headers: headers(command.version),
        body: action === "approve" ? { previewFingerprint: fingerprint } : action === "reject" ? { reason } : {},
      }),
    );
  }
}
export function createOperatorApi(token: () => Promise<string | undefined>) {
  return new OperatorApi(createApiClient({ baseUrl: location.origin, getAccessToken: token }));
}
