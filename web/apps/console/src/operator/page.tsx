import { QueryClient, QueryClientProvider, useQueryClient } from "@tanstack/react-query";
import { useEffect, useMemo, useState } from "react";
import { Notice } from "@psim/design-system";
import { useShell } from "../shell/context";
import { HomePage } from "../pages/home";
import { createOperatorApi } from "./api";
import { IncidentPanel } from "./incident";
import { NotificationsPanel } from "./notifications";
import { ObjectPanel } from "./objects";
import { ActionNotice, useActions } from "./queries";
import { useStrings } from "../ui/strings";
import { usePreferences } from "../ui/preferences";
function Workspace() {
  const { shell, feed, session } = useShell();
  const t = useStrings();
  const mode = usePreferences((state) => state.mode);
  const [selected, setSelected] = useState("");
  const api = useMemo(() => createOperatorApi(shell.auth.token), [shell]);
  const client = useQueryClient();
  const actions = useActions();
  const live = feed.loaded && feed.status === "open" && !feed.error;
  const writable = live && mode !== "training" && mode !== "wall" && !actions.pending;
  useEffect(() => {
    if (!feed.loaded) {
      void client.cancelQueries();
      client.clear();
    } else if (live) void client.invalidateQueries({}, { cancelRefetch: false });
  }, [client, live, feed.loaded, feed.revision]);
  useEffect(
    () => () => {
      void client.cancelQueries();
      client.clear();
    },
    [client],
  );
  const retry = () => {
    shell.reconnect();
    void client.invalidateQueries();
  };
  return (
    <HomePage state={feed} retry={retry} selected={selected} choose={setSelected}>
      <div className="operator-panels">
        {(mode === "training" || mode === "wall") && <Notice live={false}>{t.readOnlyMode}</Notice>}
        <ActionNotice actions={actions} />
        {feed.loaded && (
          <NotificationsPanel api={api} live={live} writable={writable} actions={actions} choose={setSelected} />
        )}
        {feed.loaded && selected && session.profile ? (
          <IncidentPanel
            key={selected}
            id={selected}
            api={api}
            live={live}
            writable={writable}
            profile={session.profile}
            actions={actions}
          />
        ) : (
          <p>{t.selectIncident}</p>
        )}
        {feed.loaded && (
          <ObjectPanel
            api={api}
            live={live}
            writable={writable}
            supervisor={session.profile?.roles.includes("supervisor") ?? false}
            actions={actions}
            watchLocation={shell.watchLocation}
          />
        )}
      </div>
    </HomePage>
  );
}
export function OperatorPage() {
  const [client] = useState(() => new QueryClient());
  return (
    <QueryClientProvider client={client}>
      <Workspace />
    </QueryClientProvider>
  );
}
