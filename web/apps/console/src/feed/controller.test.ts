import { IncidentState, Priority } from "@psim/realtime-client";
import { createApiClient } from "@psim/api-client";
import type { Delta, RealtimeClientOptions } from "@psim/realtime-client";
import { describe, expect, it, vi } from "vitest";
import { FeedController, type Channel } from "./controller";
export function change(version = 2, eventId = "event"): Delta {
  return {
    change: {
      case: "incidentEvent",
      value: {
        envelope: { correlationId: eventId },
        payload: {
          case: "incidentCreated",
          value: {
            incident: {
              incidentId: "incident",
              version: BigInt(version),
              title: "Intrusion alarm",
              priority: Priority.P2,
            },
          },
        },
      },
    },
  } as Delta;
}
function fixture(ok = true) {
  const fetch = vi.fn<typeof globalThis.fetch>().mockResolvedValue(
    new Response(
      JSON.stringify(
        ok
          ? {
              incidents: [
                {
                  incidentId: "incident",
                  version: "1",
                  title: "Old alarm",
                  priority: "PRIORITY_P2",
                  state: "INCIDENT_STATE_NEW",
                },
              ],
              resumeToken: "snapshot",
            }
          : { code: "AUTH_FORBIDDEN" },
      ),
      { status: ok ? 200 : 403 },
    ),
  );
  const channel: Channel = { start: vi.fn(), stop: vi.fn(), subscribe: vi.fn() };
  let handlers: RealtimeClientOptions["handlers"] | undefined;
  const controller = new FeedController(
    createApiClient({ baseUrl: "https://test", getAccessToken: () => undefined, fetch }),
    (callbacks) => {
      handlers = callbacks;
      return channel;
    },
  );
  return {
    controller,
    channel,
    fetch,
    handlers: () => {
      if (handlers === undefined) {
        throw new Error("channel not created");
      }
      return handlers;
    },
  };
}
describe("incident feed", () => {
  it("snapshots before subscribing and applies only newer versions", async () => {
    const { controller, channel, handlers } = fixture();
    const listener = vi.fn();
    const unsubscribe = controller.subscribe(listener);
    await controller.start();
    expect(channel.start).toHaveBeenCalledOnce();
    expect(channel.subscribe).toHaveBeenCalledWith({ id: "feed", stream: 1, resumeToken: "snapshot" });
    handlers().onDelta(change());
    handlers().onDelta(change(1));
    expect(controller.getSnapshot().rows[0]?.title).toBe("Intrusion alarm");
    expect(controller.getSnapshot().rows[0]?.version).toBe("2");
    handlers().onStatus?.("open");
    handlers().onError?.("error", "Disconnected");
    expect(controller.getSnapshot().status).toBe("open");
    expect(controller.getSnapshot().error).toBe("Disconnected");
    expect(listener).toHaveBeenCalled();
    unsubscribe();
    controller.stop();
    expect(channel.stop).toHaveBeenCalledOnce();
  });
  it("applies lifecycle events using their state and version", async () => {
    const { controller } = fixture();
    await controller.start();
    controller.delta({
      change: {
        case: "incidentEvent",
        value: {
          payload: {
            case: "incidentResolved",
            value: {
              incident: {
                incidentId: "incident",
                version: 3n,
                title: "Resolved alarm",
                priority: Priority.P2,
                state: IncidentState.RESOLVED,
              },
            },
          },
        },
      },
    } as Delta);
    expect(controller.getSnapshot().rows[0]?.state).toBe("INCIDENT_STATE_RESOLVED");
    expect(controller.getSnapshot().rows[0]?.version).toBe("3");
  });
  it("reloads a snapshot after a gap and reports a failed reload", async () => {
    const { controller, handlers, fetch } = fixture();
    await controller.start();
    fetch.mockResolvedValueOnce(new Response(JSON.stringify({ incidents: [], resumeToken: "fresh" })));
    handlers().onResync("feed", "gap");
    await vi.waitFor(() => {
      expect(controller.getSnapshot().rows).toHaveLength(0);
    });
    fetch.mockRejectedValueOnce(new Error("offline"));
    handlers().onResync("feed", "gap");
    await vi.waitFor(() => {
      expect(controller.getSnapshot().error).toBe("Could not reload incidents.");
    });
  });
  it("rejects denied snapshots and ignores unsupported changes", async () => {
    const denied = fixture(false);
    await expect(denied.controller.start()).rejects.toThrow("Could not load");
    denied.controller.delta({ change: { case: undefined } } as Delta);
    denied.controller.delta({ change: { case: "incidentEvent", value: { payload: { case: undefined } } } } as Delta);
    denied.controller.delta({
      change: { case: "incidentEvent", value: { payload: { case: "signalAttached", value: {} } } },
    } as Delta);
    denied.controller.fail("unknown");
    expect(denied.controller.getSnapshot().error).toBe("Connection failed.");
    denied.controller.fail(new Error("network"));
    expect(denied.controller.getSnapshot().error).toBe("network");
  });
  it("does not reopen a channel after being stopped while the snapshot is pending", async () => {
    const { controller, channel, fetch } = fixture();
    let deliver: (response: Response) => void = () => undefined;
    fetch.mockReturnValueOnce(
      new Promise<Response>((resolve) => {
        deliver = resolve;
      }),
    );
    const starting = controller.start();
    controller.stop();
    deliver(new Response(JSON.stringify({ resumeToken: "empty" })));
    await starting;
    expect(channel.start).not.toHaveBeenCalled();
  });
});

it("retains row references while reconnecting and during an identical pending snapshot", async () => {
  const { controller, handlers, fetch } = fixture();
  await controller.start();
  const original = controller.getSnapshot().rows;
  handlers().onStatus?.("reconnecting");
  expect(controller.getSnapshot().rows).toBe(original);
  let complete: (response: Response) => void = () => undefined;
  fetch.mockReturnValueOnce(
    new Promise<Response>((resolve) => {
      complete = resolve;
    }),
  );
  handlers().onResync("feed", "gap");
  expect(controller.getSnapshot().rows).toBe(original);
  expect(controller.getSnapshot().loading).toBe(true);
  complete(
    new Response(
      JSON.stringify({
        incidents: [
          {
            incidentId: "incident",
            version: "1",
            title: "Old alarm",
            priority: "PRIORITY_P2",
            state: "INCIDENT_STATE_NEW",
          },
        ],
        resumeToken: "new-snapshot",
      }),
    ),
  );
  await vi.waitFor(() => {
    expect(controller.getSnapshot().loading).toBe(false);
  });
  expect(controller.getSnapshot().rows).toBe(original);
  fetch.mockRejectedValueOnce(new Error("offline"));
  handlers().onResync("feed", "gap");
  await vi.waitFor(() => {
    expect(controller.getSnapshot().error).toBe("Could not reload incidents.");
  });
  expect(controller.getSnapshot().rows).toBe(original);
  expect(controller.getSnapshot().loading).toBe(false);
});
