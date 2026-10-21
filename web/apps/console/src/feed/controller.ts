import { browserSocket } from "./socket";
import { createApiClient, type ApiClient, type components } from "@psim/api-client";
import { RealtimeClient, Stream, VersionGate, type Delta, type RealtimeClientOptions } from "@psim/realtime-client";
export type Incident = components["schemas"]["psim.incident.v1.Incident"];
export interface FeedRow {
  readonly id: string;
  readonly title: string;
  readonly version: string;
  readonly priority: string;
  readonly state: string;
  readonly eventId: string;
}
export interface FeedState {
  readonly rows: readonly FeedRow[];
  readonly status: string;
  readonly error: string;
}
export interface Channel {
  start: () => void;
  stop: () => void;
  subscribe: (subscription: { id: string; stream: Stream; resumeToken: string }) => void;
}
export class FeedController {
  private readonly gate = new VersionGate();
  private readonly rows = new Map<string, FeedRow>();
  private readonly listeners = new Set<() => void>();
  private channel: Channel | undefined;
  private running = false;
  private state: FeedState = { rows: [], status: "offline", error: "" };
  constructor(
    private readonly api: ApiClient,
    private readonly makeChannel: (handlers: RealtimeClientOptions["handlers"]) => Channel,
  ) {}
  getSnapshot = (): FeedState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };
  private update(status = this.state.status, error = ""): void {
    this.state = {
      rows: [...this.rows.values()].sort((left, right) => right.id.localeCompare(left.id)),
      status,
      error,
    };
    this.listeners.forEach((listener) => {
      listener();
    });
  }
  async start(): Promise<void> {
    this.running = true;
    this.channel = this.makeChannel({
      onDelta: (delta) => {
        this.delta(delta);
      },
      onResync: () => {
        void this.snapshot().catch(() => {
          this.update("offline", "Could not reload incidents.");
        });
      },
      onStatus: (status) => {
        this.update(status);
      },
      onError: (_code, message) => {
        this.update(this.state.status, message);
      },
    });
    await this.snapshot();
    if (this.isRunning()) {
      this.channel.start();
    }
  }
  private isRunning(): boolean {
    return this.running;
  }
  private async snapshot(): Promise<void> {
    const response = await this.api.GET("/v1/feed/incidents");
    if (response.data === undefined) {
      throw new Error("Could not load incidents.");
    }
    if (!this.running) {
      return;
    }
    this.rows.clear();
    for (const incident of response.data.incidents ?? []) {
      const id = incident.incidentId ?? "";
      const version = incident.version ?? "0";
      this.gate.seed(id, BigInt(version));
      this.rows.set(id, {
        id,
        version,
        title: incident.title ?? "Incident",
        priority: incident.priority ?? "",
        state: incident.state ?? "",
        eventId: "",
      });
    }
    this.channel?.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: response.data.resumeToken ?? "" });
    this.update();
  }
  delta(delta: Delta): void {
    const change = delta.change;
    if (change.case !== "incidentEvent") {
      return;
    }
    const payload = change.value.payload;
    if (payload.value === undefined || !("incident" in payload.value)) {
      return;
    }
    const incident = payload.value.incident;
    if (incident === undefined || !this.gate.accept(incident.incidentId, incident.version)) {
      return;
    }
    this.rows.set(incident.incidentId, {
      id: incident.incidentId,
      version: incident.version.toString(),
      title: incident.title,
      state:
        [
          "INCIDENT_STATE_UNSPECIFIED",
          "INCIDENT_STATE_NEW",
          "INCIDENT_STATE_ACKNOWLEDGED",
          "INCIDENT_STATE_IN_PROGRESS",
          "INCIDENT_STATE_RESOLVED",
          "INCIDENT_STATE_CLOSED",
        ][incident.state] ?? "INCIDENT_STATE_UNSPECIFIED",
      priority: `PRIORITY_P${String(incident.priority)}`,
      eventId: change.value.envelope?.correlationId ?? "",
    });
    this.update();
  }
  stop(): void {
    this.running = false;
    this.channel?.stop();
  }
  fail(error: unknown): void {
    this.update("offline", error instanceof Error ? error.message : "Connection failed.");
  }
}
export function createFeed(getAccessToken: () => Promise<string | undefined>): FeedController {
  const api = createApiClient({ baseUrl: location.origin, getAccessToken });
  return new FeedController(
    api,
    (handlers) =>
      new RealtimeClient({
        url: `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/v1/realtime`,
        getAccessToken: async () => (await getAccessToken()) ?? "",
        socketFactory: browserSocket,
        handlers,
        keepalive: { pingIntervalMs: 5000, pongTimeoutMs: 3000 },
      }),
  );
}
