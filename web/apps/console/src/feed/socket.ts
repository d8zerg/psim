import type { SocketLike } from "@psim/realtime-client";
export function browserSocket(url: string): SocketLike {
  const native = new WebSocket(url);
  const socket: SocketLike = {
    send: (data) => {
      native.send(data);
    },
    close: (code, reason) => {
      native.close(code, reason);
    },
    onopen: null,
    onclose: null,
    onmessage: null,
    onerror: null,
  };
  native.onopen = () => {
    socket.onopen?.();
  };
  native.onclose = (event) => {
    socket.onclose?.({ code: event.code, reason: event.reason });
  };
  native.onmessage = (event) => {
    socket.onmessage?.({ data: event.data });
  };
  native.onerror = () => {
    socket.onerror?.();
  };
  return socket;
}
