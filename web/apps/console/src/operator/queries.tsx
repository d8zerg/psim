import { useMutation, useQueryClient } from "@tanstack/react-query";
import { useRef, useState } from "react";
import { Notice } from "@psim/design-system";
import { statusOf } from "./api";
import { errorKind } from "./model";
import { useStrings } from "../ui/strings";
export function useActions() {
  const client = useQueryClient();
  const locked = useRef(false);
  const [error, setError] = useState<ReturnType<typeof errorKind> | "">("");
  const mutation = useMutation({
    mutationFn: (work: () => Promise<unknown>) => work(),
    retry: false,
    onMutate: () => {
      setError("");
    },
    onError: (error: unknown) => {
      setError(errorKind(statusOf(error)));
    },
    onSettled: async () => {
      await client.invalidateQueries();
    },
  });
  return {
    pending: mutation.isPending,
    error,
    perform: async (work: () => Promise<unknown>) => {
      if (locked.current) return false;
      locked.current = true;
      try {
        await mutation.mutateAsync(work);
        return true;
      } catch {
        return false;
      } finally {
        locked.current = false;
      }
    },
  };
}
export type Actions = ReturnType<typeof useActions>;
export function ActionNotice({ actions }: { readonly actions: Actions }) {
  const t = useStrings();
  return actions.error ? <Notice tone="warning">{t[actions.error]}</Notice> : null;
}
export const queryDefaults = { retry: false, staleTime: 0, refetchOnWindowFocus: true, refetchInterval: 3000 } as const;
