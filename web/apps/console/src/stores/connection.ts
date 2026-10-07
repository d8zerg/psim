// Realtime connection state shown by the console shell (step 8.2 connects it to RealtimeClient).
import type { ConnectionStatus } from "@psim/realtime-client";
import { create } from "zustand";

export interface ConnectionState {
  readonly status: ConnectionStatus;
  /** Time of the last transition to "open", to show how long the console has been live. */
  readonly openSince: number | null;
  readonly setStatus: (status: ConnectionStatus, now?: number) => void;
}

export const useConnectionStore = create<ConnectionState>()((set) => ({
  status: "idle",
  openSince: null,
  setStatus: (status, now = Date.now()) => {
    set((state) => ({
      status,
      openSince: status === "open" ? (state.status === "open" ? state.openSince : now) : null,
    }));
  },
}));

/** Operators must see at a glance whether the feed is live (step 8.2, S6). */
export function connectionLabel(status: ConnectionStatus): string {
  switch (status) {
    case "open":
      return "online";
    case "connecting":
    case "authenticating":
      return "connecting";
    case "reconnecting":
      return "reconnecting";
    case "idle":
    case "stopped":
      return "offline";
  }
}
