import { browserSocket } from "./socket";
import { createApiClient, ApiProblemError, type ApiClient, type components } from "@psim/api-client";
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
  readonly loading: boolean;
  readonly loaded: boolean;
  readonly rows: readonly FeedRow[];
  readonly status: string;
  readonly error: string;
}
export interface Channel {
  start: () => void;
  stop: () => void;
  reconnect?: () => void;
  subscribe: (subscription: { id: string; stream: Stream; resumeToken: string }) => void;
}
export class FeedController {
  private gate = new VersionGate();
  private readonly rows = new Map<string, FeedRow>();
  private readonly listeners = new Set<() => void>();
  private channel: Channel | undefined;
  private running = false;
  private epoch = 0;
  private retryTimer: ReturnType<typeof setTimeout> | undefined;
  private retryAttempt = 0;
  private resyncRequired = false;
  private snapshotPromise: Promise<void> | undefined;
  private state: FeedState = { rows: [], status: "offline", error: "", loading: false, loaded: false };
  constructor(
    private readonly api: ApiClient,
    private readonly makeChannel: (handlers: RealtimeClientOptions["handlers"]) => Channel,
    private readonly accessDenied?: () => void,
  ) {}
  getSnapshot = (): FeedState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  };
  private update(status = this.state.status, error = ""): void {
    const rows = [...this.rows.values()].sort((left, right) => right.id.localeCompare(left.id));
    this.state = {
      ...this.state,
      rows:
        rows.length === this.state.rows.length && rows.every((row, index) => row === this.state.rows[index])
          ? this.state.rows
          : rows,
      status,
      error,
    };
    this.listeners.forEach((listener) => {
      listener();
    });
  }
  async start(): Promise<void> {
    if (this.running) return;
    this.running = true;
    const epoch = ++this.epoch;
    this.channel = this.makeChannel({
      onAccessDenied: () => {
        if (this.running && this.epoch === epoch) this.accessDenied?.();
      },
      onDelta: (delta) => {
        if (this.running && this.epoch === epoch) this.delta(delta);
      },
      onResync: (_id, reason) => {
        if (this.running && this.epoch === epoch) {
          if (reason === "access scope changed" || reason === "authentication scope or capabilities changed") {
            this.rows.clear();
            this.gate = new VersionGate();
            this.state = { ...this.state, rows: [], loaded: false };
          }
          this.resyncRequired = true;
          void this.reload();
        }
      },
      onStatus: (status) => {
        if (this.running && this.epoch === epoch) this.update(status);
      },
      onError: (_code, message) => {
        if (this.running && this.epoch === epoch) this.update(this.state.status, message);
      },
    });
    await this.snapshot();
    if (this.isRunning() && this.epoch === epoch) {
      this.channel.start();
    }
  }
  private isRunning(): boolean {
    return this.running;
  }
  retry(): void {
    if (!this.running) return;
    clearTimeout(this.retryTimer);
    if (!this.state.loaded || this.resyncRequired) void this.reload();
    else this.channel?.reconnect?.();
  }
  private async reload(): Promise<void> {
    try {
      await this.snapshot();
      if (this.running) this.channel?.start();
    } catch {
      if (this.running) {
        this.fail(new Error("Could not reload incidents."));
      }
    }
  }
  private scheduleRetry(): void {
    clearTimeout(this.retryTimer);
    this.retryTimer = setTimeout(
      () => {
        void this.reload();
      },
      Math.min(30000, 500 * 2 ** this.retryAttempt++),
    );
  }
  private snapshot(): Promise<void> {
    if (!this.snapshotPromise) {
      const pending = this.loadSnapshot().finally(() => {
        if (this.snapshotPromise === pending) this.snapshotPromise = undefined;
      });
      this.snapshotPromise = pending;
    }
    return this.snapshotPromise;
  }
  private async loadSnapshot(): Promise<void> {
    const epoch = this.epoch;
    this.state = { ...this.state, loading: true };
    this.update();
    let response;
    try {
      response = await this.api.GET("/v1/feed/incidents", { signal: AbortSignal.timeout(15000) });
    } catch (error) {
      if (this.epoch === epoch && error instanceof ApiProblemError && [401, 403].includes(error.problem.status)) {
        this.stop();
        this.rows.clear();
        this.state = { rows: [], status: "offline", error: "Access denied", loaded: false, loading: false };
        this.update();
        this.accessDenied?.();
      }
      throw error;
    }
    if ([401, 403].includes(response.response.status) && this.epoch === epoch) {
      this.stop();
      this.rows.clear();
      this.state = { rows: [], status: "offline", error: "Access denied", loading: false, loaded: false };
      this.update();
      this.accessDenied?.();
    }
    if (response.data === undefined) {
      throw new Error("Could not load incidents.");
    }
    if (!this.running || this.epoch !== epoch) {
      return;
    }
    const previous = new Map(this.rows);
    this.rows.clear();
    this.gate = new VersionGate();
    this.retryAttempt = 0;
    this.resyncRequired = false;
    for (const incident of response.data.incidents ?? []) {
      const id = incident.incidentId ?? "";
      const version = incident.version ?? "0";
      this.gate.seed(id, BigInt(version));
      const row = {
        id,
        version,
        title: incident.title ?? "Incident",
        priority: incident.priority ?? "",
        state: incident.state ?? "",
        eventId: "",
      };
      const saved = previous.get(id);
      this.rows.set(
        id,
        saved?.version === row.version &&
          saved.title === row.title &&
          saved.priority === row.priority &&
          saved.state === row.state
          ? saved
          : row,
      );
    }
    this.channel?.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: response.data.resumeToken ?? "" });
    this.state = { ...this.state, loading: false, loaded: true };
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
    this.epoch += 1;
    this.snapshotPromise = undefined;
    clearTimeout(this.retryTimer);
    this.channel?.stop();
  }
  fail(error: unknown): void {
    this.state = { ...this.state, loading: false };
    this.update("offline", error instanceof Error ? error.message : "Connection failed.");
    if (this.running) this.scheduleRetry();
  }
}
export function createFeed(
  getAccessToken: () => Promise<string | undefined>,
  accessDenied?: () => void,
): FeedController {
  const api = createApiClient({ baseUrl: location.origin, getAccessToken });
  return new FeedController(
    api,
    (handlers) =>
      new RealtimeClient({
        url: `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/v1/realtime`,
        getAccessToken: async () => (await getAccessToken()) ?? "",
        socketFactory: browserSocket,
        handlers,
        supportedChangeTypes: ["incident_event", "incident_operational_updated"],
        keepalive: { pingIntervalMs: 5000, pongTimeoutMs: 3000 },
      }),
    accessDenied,
  );
}
