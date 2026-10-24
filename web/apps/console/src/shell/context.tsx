import { createContext, useContext, useSyncExternalStore } from "react";
import type { ShellController } from "./controller";
export const ShellContext = createContext<ShellController | null>(null);
export function useShell() {
  const shell = useContext(ShellContext);
  if (!shell) throw new Error("Missing application shell");
  const state = useSyncExternalStore(shell.subscribe, shell.getSnapshot);
  return { shell, ...state };
}
