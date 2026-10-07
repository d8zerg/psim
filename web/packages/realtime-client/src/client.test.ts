import { beforeEach, describe, expect, it, vi } from "vitest";

import { type ConnectionStatus, RealtimeClient, type SocketLike, type Timers } from "./client";
import { Stream } from "./gen/psim/realtime/v1/realtime_pb";

class FakeSocket implements SocketLike {
  sent: Record<string, unknown>[] = [];
  closed: { code?: number; reason?: string } | null = null;
  onopen: (() => void) | null = null;
  onmessage: ((event: { data: unknown }) => void) | null = null;
  onclose: ((event: { code: number; reason: string }) => void) | null = null;
  onerror: (() => void) | null = null;

  send(data: string): void {
    this.sent.push(JSON.parse(data) as Record<string, unknown>);
  }

  close(code?: number, reason?: string): void {
    this.closed = { ...(code === undefined ? {} : { code }), ...(reason === undefined ? {} : { reason }) };
    this.onclose?.({ code: code ?? 1006, reason: reason ?? "" });
  }

  server(frame: Record<string, unknown>): void {
    this.onmessage?.({ data: JSON.stringify(frame) });
  }

  lastSent(): Record<string, unknown> | undefined {
    return this.sent.at(-1);
  }
}

class FakeTimers implements Timers {
  private next = 1;
  readonly pending = new Map<number, { callback: () => void; ms: number }>();

  setTimeout(callback: () => void, ms: number): unknown {
    const id = this.next++;
    this.pending.set(id, { callback, ms });
    return id;
  }

  clearTimeout(handle: unknown): void {
    this.pending.delete(handle as number);
  }

  /** Run the earliest timer; returns its delay. */
  fire(): number {
    const [id, timer] = [...this.pending.entries()].sort((a, b) => a[1].ms - b[1].ms)[0] ?? [];
    if (id === undefined || timer === undefined) {
      throw new Error("no pending timers");
    }
    this.pending.delete(id);
    timer.callback();
    return timer.ms;
  }
}

const flush = () => new Promise((resolve) => setTimeout(resolve, 0));

function setup(options: { keepalive?: boolean } = {}) {
  const sockets: FakeSocket[] = [];
  const timers = new FakeTimers();
  const statuses: ConnectionStatus[] = [];
  const handlers = {
    onDelta: vi.fn(),
    onResync: vi.fn(),
    onError: vi.fn(),
    onStatus: (s: ConnectionStatus) => statuses.push(s),
  };
  const client = new RealtimeClient({
    url: "wss://psim.test/v1/realtime",
    getAccessToken: () => "token-1",
    socketFactory: () => {
      const socket = new FakeSocket();
      sockets.push(socket);
      return socket;
    },
    handlers,
    timers,
    backoff: { initialMs: 100, maxMs: 1_000, random: () => 1 },
    ...(options.keepalive ? { keepalive: { pingIntervalMs: 25_000, pongTimeoutMs: 10_000 } } : {}),
  });
  const socket = () => {
    const last = sockets.at(-1);
    if (last === undefined) {
      throw new Error("no socket");
    }
    return last;
  };
  async function open() {
    socket().onopen?.();
    await flush();
    socket().server({ authenticated: { userId: "u1", expiresIn: 300 } });
  }
  return { client, sockets, socket, timers, statuses, handlers, open };
}

describe("RealtimeClient", () => {
  let env: ReturnType<typeof setup>;

  beforeEach(() => {
    env = setup();
  });

  it("authenticates, then subscribes with the snapshot token", async () => {
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-1", filter: { siteIds: ["s1"] } });
    env.client.start();
    await env.open();

    expect(env.socket().sent[0]).toEqual({ requestId: "r1", authenticate: { accessToken: "token-1" } });
    expect(env.socket().sent[1]).toEqual({
      requestId: "r2",
      subscribe: {
        subscriptionId: "feed",
        stream: "STREAM_INCIDENTS",
        resumeToken: "snap-1",
        filter: { siteIds: ["s1"] },
      },
    });
    expect(env.client.status).toBe("open");
    expect(env.statuses).toEqual(["connecting", "authenticating", "open"]);
  });

  it("delivers deltas and tracks the latest resume token", async () => {
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-1" });
    env.client.start();
    await env.open();

    env.socket().server({ subscribed: { subscriptionId: "feed", resumeToken: "t1" } });
    env.socket().server({ delta: { subscriptionId: "feed", resumeToken: "t2", incidentEvent: {} } });

    expect(env.handlers.onDelta).toHaveBeenCalledOnce();
    expect(env.client.resumeToken("feed")).toBe("t2");
  });

  it("reconnects with backoff and resumes from the last token", async () => {
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-1" });
    env.client.start();
    await env.open();
    env.socket().server({ delta: { subscriptionId: "feed", resumeToken: "t7", incidentEvent: {} } });

    env.socket().close(1006, "network");
    expect(env.client.status).toBe("reconnecting");
    expect(env.timers.fire()).toBe(100);
    await env.open();

    expect(env.sockets).toHaveLength(2);
    expect(env.socket().lastSent()).toMatchObject({ subscribe: { subscriptionId: "feed", resumeToken: "t7" } });
  });

  it("doubles the reconnect delay up to the maximum", async () => {
    env.client.start();
    const delays: number[] = [];
    for (let i = 0; i < 6; i++) {
      env.socket().close();
      delays.push(env.timers.fire());
    }
    expect(delays).toEqual([100, 200, 400, 800, 1000, 1000]);
    await env.open();
    env.socket().close();
    expect(env.timers.fire()).toBe(100);
  });

  it("reports resync and forgets the subscription until it is renewed", async () => {
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-1" });
    env.client.start();
    await env.open();

    env.socket().server({ resyncRequired: { subscriptionId: "feed", reason: "REALTIME_RESYNC_REQUIRED" } });

    expect(env.handlers.onResync).toHaveBeenCalledWith("feed", "REALTIME_RESYNC_REQUIRED");
    expect(env.client.resumeToken("feed")).toBeUndefined();
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-2" });
    expect(env.socket().lastSent()).toMatchObject({ subscribe: { resumeToken: "snap-2" } });
  });

  it("unsubscribes and re-authenticates over the open connection", async () => {
    env.client.subscribe({ id: "feed", stream: Stream.INCIDENTS, resumeToken: "snap-1" });
    env.client.start();
    await env.open();

    env.client.unsubscribe("feed");
    expect(env.socket().lastSent()).toMatchObject({ unsubscribe: { subscriptionId: "feed" } });
    await env.client.reauthenticate();
    expect(env.socket().lastSent()).toMatchObject({ authenticate: { accessToken: "token-1" } });
  });

  it("closes on a fatal error frame and reports it", async () => {
    env.client.start();
    await env.open();

    env.socket().server({ error: { code: "AUTH_TOKEN_EXPIRED", message: "expired", fatal: true } });

    expect(env.handlers.onError).toHaveBeenCalledWith("AUTH_TOKEN_EXPIRED", "expired");
    expect(env.socket().closed).toEqual({ code: 4000, reason: "AUTH_TOKEN_EXPIRED" });
    expect(env.client.status).toBe("reconnecting");
  });

  it("reports an unparsable frame without dropping the connection", async () => {
    env.client.start();
    await env.open();

    env.socket().onmessage?.({ data: "{not json" });

    expect(env.handlers.onError).toHaveBeenCalledWith("REALTIME_BAD_FRAME", "unparsable server frame");
    expect(env.client.status).toBe("open");
  });

  it("stops for good and ignores further closes", async () => {
    env.client.start();
    await env.open();

    env.client.stop();

    expect(env.client.status).toBe("stopped");
    expect(env.timers.pending.size).toBe(0);
    env.client.start();
    expect(env.sockets).toHaveLength(2);
  });
});

describe("RealtimeClient keepalive", () => {
  it("pings, and drops the connection without a pong", async () => {
    const env = setup({ keepalive: true });
    env.client.start();
    await env.open();

    expect(env.timers.fire()).toBe(25_000);
    expect(env.socket().lastSent()).toMatchObject({ ping: {} });
    env.socket().server({ pong: {} });
    expect(env.timers.fire()).toBe(25_000);
    expect(env.timers.fire()).toBe(10_000);
    expect(env.socket().closed).toEqual({ code: 4001, reason: "pong timeout" });
  });
});

describe("RealtimeClient edge cases", () => {
  it("ignores start while already connecting", () => {
    const env = setup();
    env.client.start();
    env.client.start();
    expect(env.sockets).toHaveLength(1);
  });

  it("reconnects after a socket error", async () => {
    const env = setup();
    env.client.start();
    await env.open();

    env.socket().onerror?.();

    expect(env.client.status).toBe("reconnecting");
    env.timers.fire();
    expect(env.sockets).toHaveLength(2);
  });

  it("keeps the connection on a non-fatal error and ignores non-text frames", async () => {
    const env = setup();
    env.client.start();
    await env.open();

    env.socket().server({ error: { code: "REALTIME_FILTER_INVALID", message: "bad filter", fatal: false } });
    env.socket().onmessage?.({ data: new ArrayBuffer(4) });

    expect(env.handlers.onError).toHaveBeenCalledWith("REALTIME_FILTER_INVALID", "bad filter");
    expect(env.socket().closed).toBeNull();
    expect(env.client.status).toBe("open");
  });

  it("does not re-authenticate or track tokens outside an open subscription", async () => {
    const env = setup();
    env.client.start();
    await env.client.reauthenticate();
    expect(env.socket().sent).toHaveLength(0);

    await env.open();
    env.socket().server({ authenticated: { userId: "u1" } });
    env.socket().server({ delta: { subscriptionId: "unknown", resumeToken: "t9", incidentEvent: {} } });

    expect(env.client.resumeToken("unknown")).toBeUndefined();
    expect(env.socket().sent).toHaveLength(1);
  });

  it("drops the access token of a superseded connection", async () => {
    let release: (token: string) => void = () => undefined;
    const sockets: FakeSocket[] = [];
    const client = new RealtimeClient({
      url: "wss://psim.test/v1/realtime",
      getAccessToken: () => new Promise<string>((resolve) => (release = resolve)),
      socketFactory: () => {
        const socket = new FakeSocket();
        sockets.push(socket);
        return socket;
      },
      handlers: { onDelta: vi.fn(), onResync: vi.fn() },
      timers: new FakeTimers(),
    });
    client.start();
    sockets[0]?.onopen?.();
    client.stop();
    release("late-token");
    await flush();

    expect(sockets[0]?.sent).toEqual([]);
  });

  it("works with the default timers and backoff", async () => {
    vi.useFakeTimers();
    try {
      const sockets: FakeSocket[] = [];
      const client = new RealtimeClient({
        url: "wss://psim.test/v1/realtime",
        getAccessToken: () => "token-1",
        socketFactory: () => {
          const socket = new FakeSocket();
          sockets.push(socket);
          return socket;
        },
        handlers: { onDelta: vi.fn(), onResync: vi.fn() },
      });
      client.start();
      sockets[0]?.close();
      expect(client.status).toBe("reconnecting");
      await vi.advanceTimersByTimeAsync(500);
      expect(sockets).toHaveLength(2);
      client.stop();
    } finally {
      vi.useRealTimers();
    }
  });
});
