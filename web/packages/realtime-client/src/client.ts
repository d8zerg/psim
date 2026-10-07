// Realtime channel client (ADR-019): authenticate, subscribe with a resume token, track the token of
// every delta, reconnect with backoff and resume from the last token, report resync to the caller.
// The socket and the timers are injected, so the protocol logic is tested without a network.
import { create, fromJsonString, type MessageInitShape, toJsonString } from "@bufbuild/protobuf";

import {
  ClientFrameSchema,
  type Delta,
  type FeedFilterSchema,
  ServerFrameSchema,
  type ServerFrame,
  type Stream,
} from "./gen/psim/realtime/v1/realtime_pb";

/** The subset of the browser WebSocket the client uses. */
export interface SocketLike {
  send(data: string): void;
  close(code?: number, reason?: string): void;
  onopen: (() => void) | null;
  onmessage: ((event: { data: unknown }) => void) | null;
  onclose: ((event: { code: number; reason: string }) => void) | null;
  onerror: (() => void) | null;
}

export interface Timers {
  setTimeout(callback: () => void, ms: number): unknown;
  clearTimeout(handle: unknown): void;
}

export type ConnectionStatus = "idle" | "connecting" | "authenticating" | "open" | "reconnecting" | "stopped";

export interface Subscription {
  readonly id: string;
  readonly stream: Stream;
  readonly filter?: MessageInitShape<typeof FeedFilterSchema>;
  /** Token of the snapshot (GET /v1/feed/incidents) the subscription continues from. */
  readonly resumeToken: string;
}

export interface RealtimeHandlers {
  onDelta(delta: Delta): void;
  /** The server cannot resume: take a new snapshot and subscribe again with its token. */
  onResync(subscriptionId: string, reason: string): void;
  onStatus?(status: ConnectionStatus): void;
  onError?(code: string, message: string): void;
}

export interface RealtimeClientOptions {
  readonly url: string;
  readonly getAccessToken: () => string | Promise<string>;
  readonly socketFactory: (url: string) => SocketLike;
  readonly handlers: RealtimeHandlers;
  readonly timers?: Timers;
  /** Reconnect delay: doubles from initialMs up to maxMs; random() adds jitter. */
  readonly backoff?: { readonly initialMs: number; readonly maxMs: number; readonly random?: () => number };
  /** Keepalive: a ping every pingIntervalMs, the connection is dropped without a pong in pongTimeoutMs. */
  readonly keepalive?: { readonly pingIntervalMs: number; readonly pongTimeoutMs: number };
}

const defaultTimers: Timers = {
  setTimeout: (callback, ms) => globalThis.setTimeout(callback, ms),
  clearTimeout: (handle) => {
    globalThis.clearTimeout(handle as ReturnType<typeof globalThis.setTimeout>);
  },
};

export class RealtimeClient {
  private readonly options: RealtimeClientOptions;
  private readonly timers: Timers;
  private readonly subscriptions = new Map<string, Subscription>();
  private socket: SocketLike | null = null;
  private currentStatus: ConnectionStatus = "idle";
  private attempt = 0;
  private reconnectTimer: unknown = null;
  private pingTimer: unknown = null;
  private pongTimer: unknown = null;
  private requestSeq = 0;

  constructor(options: RealtimeClientOptions) {
    this.options = options;
    this.timers = options.timers ?? defaultTimers;
  }

  get status(): ConnectionStatus {
    return this.currentStatus;
  }

  /** Last known resume token of a subscription (from Subscribed or the latest Delta). */
  resumeToken(subscriptionId: string): string | undefined {
    return this.subscriptions.get(subscriptionId)?.resumeToken;
  }

  start(): void {
    if (this.currentStatus !== "idle" && this.currentStatus !== "stopped") {
      return;
    }
    this.connect();
  }

  stop(): void {
    this.setStatus("stopped");
    this.clearTimers();
    const socket = this.socket;
    this.socket = null;
    socket?.close(1000, "client stop");
  }

  subscribe(subscription: Subscription): void {
    this.subscriptions.set(subscription.id, subscription);
    if (this.currentStatus === "open") {
      this.sendSubscribe(subscription);
    }
  }

  unsubscribe(subscriptionId: string): void {
    if (this.subscriptions.delete(subscriptionId) && this.currentStatus === "open") {
      this.send({ case: "unsubscribe", value: { subscriptionId } });
    }
  }

  /** Send a fresh access token over the open connection (ADR-019, item 8). */
  async reauthenticate(): Promise<void> {
    if (this.currentStatus === "open") {
      this.send({ case: "authenticate", value: { accessToken: await this.options.getAccessToken() } });
    }
  }

  private connect(): void {
    this.setStatus(this.attempt === 0 ? "connecting" : "reconnecting");
    const socket = this.options.socketFactory(this.options.url);
    this.socket = socket;
    socket.onopen = () => {
      this.setStatus("authenticating");
      void this.authenticate(socket);
    };
    socket.onmessage = (event) => {
      if (typeof event.data === "string") {
        this.receive(event.data);
      }
    };
    socket.onclose = () => {
      this.onDisconnected(socket);
    };
    socket.onerror = () => {
      socket.close();
    };
  }

  private async authenticate(socket: SocketLike): Promise<void> {
    const accessToken = await this.options.getAccessToken();
    if (this.socket === socket) {
      this.send({ case: "authenticate", value: { accessToken } });
    }
  }

  private receive(text: string): void {
    let frame: ServerFrame;
    try {
      frame = fromJsonString(ServerFrameSchema, text, { ignoreUnknownFields: true });
    } catch {
      this.options.handlers.onError?.("REALTIME_BAD_FRAME", "unparsable server frame");
      return;
    }
    const body = frame.frame;
    switch (body.case) {
      case "authenticated":
        if (this.currentStatus === "authenticating") {
          this.attempt = 0;
          this.setStatus("open");
          this.subscriptions.forEach((subscription) => {
            this.sendSubscribe(subscription);
          });
          this.schedulePing();
        }
        break;
      case "subscribed":
        this.updateToken(body.value.subscriptionId, body.value.resumeToken);
        break;
      case "delta":
        this.updateToken(body.value.subscriptionId, body.value.resumeToken);
        this.options.handlers.onDelta(body.value);
        break;
      case "resyncRequired":
        this.subscriptions.delete(body.value.subscriptionId);
        this.options.handlers.onResync(body.value.subscriptionId, body.value.reason);
        break;
      case "pong":
        this.timers.clearTimeout(this.pongTimer);
        this.pongTimer = null;
        this.schedulePing();
        break;
      case "error":
        this.options.handlers.onError?.(body.value.code, body.value.message);
        if (body.value.fatal) {
          this.socket?.close(4000, body.value.code);
        }
        break;
      case undefined:
        break;
    }
  }

  private updateToken(subscriptionId: string, resumeToken: string): void {
    const subscription = this.subscriptions.get(subscriptionId);
    if (subscription !== undefined && resumeToken !== "") {
      this.subscriptions.set(subscriptionId, { ...subscription, resumeToken });
    }
  }

  private sendSubscribe(subscription: Subscription): void {
    this.send({
      case: "subscribe",
      value: {
        subscriptionId: subscription.id,
        stream: subscription.stream,
        resumeToken: subscription.resumeToken,
        ...(subscription.filter === undefined ? {} : { filter: subscription.filter }),
      },
    });
  }

  private send(frame: NonNullable<MessageInitShape<typeof ClientFrameSchema>["frame"]>): void {
    this.requestSeq += 1;
    const message = create(ClientFrameSchema, { requestId: `r${String(this.requestSeq)}`, frame });
    this.socket?.send(toJsonString(ClientFrameSchema, message));
  }

  private schedulePing(): void {
    const keepalive = this.options.keepalive;
    if (keepalive === undefined) {
      return;
    }
    this.timers.clearTimeout(this.pingTimer);
    this.pingTimer = this.timers.setTimeout(() => {
      this.send({ case: "ping", value: {} });
      this.pongTimer = this.timers.setTimeout(() => {
        this.socket?.close(4001, "pong timeout");
      }, keepalive.pongTimeoutMs);
    }, keepalive.pingIntervalMs);
  }

  private onDisconnected(socket: SocketLike): void {
    if (this.socket !== socket) {
      return;
    }
    // stop() detaches the socket before closing it, so a close of the current socket is a disconnect.
    this.socket = null;
    this.clearTimers();
    this.attempt += 1;
    this.setStatus("reconnecting");
    this.reconnectTimer = this.timers.setTimeout(() => {
      this.reconnectTimer = null;
      this.connect();
    }, this.reconnectDelay());
  }

  private reconnectDelay(): number {
    const backoff = this.options.backoff ?? { initialMs: 500, maxMs: 30_000 };
    const base = Math.min(backoff.maxMs, backoff.initialMs * 2 ** (this.attempt - 1));
    const random = backoff.random ?? Math.random;
    return Math.round(base / 2 + (random() * base) / 2);
  }

  private clearTimers(): void {
    for (const timer of [this.reconnectTimer, this.pingTimer, this.pongTimer]) {
      if (timer !== null) {
        this.timers.clearTimeout(timer);
      }
    }
    this.reconnectTimer = this.pingTimer = this.pongTimer = null;
  }

  private setStatus(status: ConnectionStatus): void {
    if (this.currentStatus !== status) {
      this.currentStatus = status;
      this.options.handlers.onStatus?.(status);
    }
  }
}
