import { expect, it, vi } from "vitest";
import { ShellController, type AuthSession } from "./controller";
import type { SessionState } from "../auth";
import { FeedController } from "../feed/controller";
import { createApiClient } from "@psim/api-client";
function fixture(roles = ["operator"]) {
  let session: SessionState = {
    status: "authenticated",
    error: false,
    profile: { subject: "operator", name: "operator", roles, context: JSON.stringify(roles) },
  };
  const listeners = new Set<() => void>();
  const update = (state: SessionState) => {
    session = state;
    listeners.forEach((listener) => {
      listener();
    });
  };
  const auth: AuthSession = {
    getSnapshot: () => session,
    subscribe: (listener) => {
      listeners.add(listener);
      return () => {
        listeners.delete(listener);
      };
    },
    token: () => Promise.resolve("token"),
    login: vi.fn(),
    logout: vi.fn(),
    invalidate: () => {
      update({ status: "anonymous", error: false });
    },
  };
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockResolvedValue(
      new Response(
        JSON.stringify({ incidents: [{ incidentId: "a", version: "1", title: "Alarm" }], resumeToken: "snapshot" }),
      ),
    );
  const channel = { start: vi.fn(), stop: vi.fn(), subscribe: vi.fn(), reconnect: vi.fn() };
  const feed = new FeedController(
    createApiClient({ baseUrl: "https://test", getAccessToken: auth.token, fetch }),
    () => channel,
  );
  const makeFeed = vi.fn(() => feed);
  return { shell: new ShellController(auth, makeFeed), auth, update, fetch, channel, makeFeed };
}
it("shares one channel through session notifications and clears it synchronously on logout", async () => {
  const f = fixture();
  f.shell.start();
  await vi.waitFor(() => {
    expect(f.channel.start).toHaveBeenCalledOnce();
  });
  f.update({ ...f.auth.getSnapshot() });
  expect(f.makeFeed).toHaveBeenCalledOnce();
  expect(f.shell.getSnapshot().feed.rows).toHaveLength(1);
  f.auth.invalidate();
  expect(f.shell.getSnapshot().feed.rows).toHaveLength(0);
  expect(f.channel.stop).toHaveBeenCalledOnce();
  f.shell.stop();
});
it("opens no incident subscription for read-only or technical modules", async () => {
  for (const role of ["manager", "auditor", "admin", "integrator", "unknown"]) {
    const f = fixture([role]);
    f.shell.start();
    await Promise.resolve();
    expect(f.makeFeed).not.toHaveBeenCalled();
    f.shell.stop();
  }
});
it("discards old scope data and late HTTP responses", async () => {
  const f = fixture();
  let finish: (response: Response) => void = () => undefined;
  f.fetch.mockReturnValueOnce(
    new Promise((resolve) => {
      finish = resolve;
    }),
  );
  f.shell.start();
  await vi.waitFor(() => {
    expect(f.fetch).toHaveBeenCalledOnce();
  });
  f.update({
    status: "authenticated",
    error: false,
    profile: { subject: "manager", name: "manager", roles: ["manager"], context: "new-scope" },
  });
  finish(new Response(JSON.stringify({ incidents: [{ incidentId: "private", version: "99" }], resumeToken: "old" })));
  await Promise.resolve();
  expect(f.shell.getSnapshot().feed.rows).toEqual([]);
  expect(f.channel.start).not.toHaveBeenCalled();
  f.shell.stop();
});
it("supports repeated React mount cleanup without opening a stale channel", async () => {
  const f = fixture();
  f.shell.start();
  f.shell.stop();
  f.shell.start();
  await vi.waitFor(() => {
    expect(f.channel.start).toHaveBeenCalledOnce();
  });
  f.shell.reconnect();
  expect(f.channel.reconnect).toHaveBeenCalledOnce();
  f.shell.stop();
});
