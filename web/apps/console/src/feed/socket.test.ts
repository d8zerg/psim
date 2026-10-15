import { afterEach, expect, it, vi } from "vitest";
import { browserSocket } from "./socket";
afterEach(() => {
  vi.unstubAllGlobals();
});
it("bridges native socket events and writes", () => {
  const native = {
    send: vi.fn(),
    close: vi.fn(),
    onopen: null as null | (() => void),
    onerror: null as null | (() => void),
    onmessage: null as null | ((event: { data: string }) => void),
    onclose: null as null | ((event: { code: number; reason: string }) => void),
  };
  vi.stubGlobal(
    "WebSocket",
    vi.fn(function () {
      return native;
    }),
  );
  const socket = browserSocket("ws://test");
  native.onopen?.();
  native.onerror?.();
  native.onmessage?.({ data: "ignored" });
  native.onclose?.({ code: 1000, reason: "" });
  socket.onopen = vi.fn();
  socket.onerror = vi.fn();
  socket.onmessage = vi.fn();
  socket.onclose = vi.fn();
  native.onopen?.();
  native.onerror?.();
  native.onmessage?.({ data: "frame" });
  native.onclose?.({ code: 1000, reason: "done" });
  socket.send("hello");
  socket.close(1000, "done");
  expect(native.send).toHaveBeenCalledWith("hello");
  expect(native.close).toHaveBeenCalledWith(1000, "done");
  expect(socket.onmessage).toHaveBeenCalledWith({ data: "frame" });
  expect(socket.onopen).toHaveBeenCalledOnce();
  expect(socket.onclose).toHaveBeenCalledWith({ code: 1000, reason: "done" });
  expect(socket.onerror).toHaveBeenCalledOnce();
});
