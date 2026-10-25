import { useCallback, useEffect, useLayoutEffect, useMemo, useState, type ReactNode } from "react";
import { QueryClient, QueryClientProvider, useQuery } from "@tanstack/react-query";
import { Button, Field, Notice } from "@psim/design-system";
import { useShell } from "../shell/context";
import { usePreferences } from "../ui/preferences";
import { AdminApi, identityConsole, status } from "./api";
import { Editor, GuardButton, type Permissions } from "./editor";
import { FloorPlans, Imports } from "./files";
import { Result } from "./actions";
import { list, newRecord, record, resources, string, type RecordValue, type Resource, type ResourceKey } from "./model";
import { useAdminStrings } from "./strings";
type Section = ResourceKey | "imports" | "floorPlans" | "access";
function Collection({
  resourceKey,
  api,
  permissions,
  dirty,
  onDirty,
}: {
  readonly resourceKey: ResourceKey;
  readonly api: AdminApi;
  readonly permissions: Permissions;
  readonly dirty: boolean;
  readonly onDirty: (dirty: boolean) => void;
}) {
  const { t } = useAdminStrings();
  const resource: Resource = resources[resourceKey];
  const [page, setPage] = useState("");
  const [filter, setFilter] = useState("");
  const [selection, setSelection] = useState<{ value: RecordValue; existing: boolean; revision: number }>();
  const allowed = !resource.global || permissions.global;
  const query = useQuery({
    queryKey: ["admin", resourceKey, page],
    queryFn: ({ signal }) =>
      api.get(
        resource.path +
          (resource.singleton || resourceKey === "mappings"
            ? ""
            : `?${new URLSearchParams({ pageSize: "100", pageToken: page })}`),
        signal,
      ),
    enabled: allowed,
    retry: false,
    refetchInterval: 5000,
  });
  useEffect(() => {
    if ([401, 403].includes(status(query.error))) permissions.denied();
  }, [query.error, permissions]);
  if (!allowed) return <Notice>{t.global}</Notice>;
  const rows = resource.singleton
    ? query.data?.[resource.item]
      ? [record(query.data[resource.item])]
      : []
    : list(query.data?.[resource.collection]).map(record);
  const choose = (value: RecordValue, existing: boolean) => {
    setSelection({ value: structuredClone(value), existing, revision: (selection?.revision ?? 0) + 1 });
    onDirty(false);
  };
  return (
    <div className="admin-collection">
      <section className="admin-fields">
        <h2>{t[resourceKey]}</h2>
        {query.error && <Notice tone="warning">{t.failed}</Notice>}
        <Button
          onClick={() => {
            void query.refetch();
          }}
        >
          {t.reload}
        </Button>
        {query.isPending && <p role="status">{t.loading}</p>}
        {!resource.singleton && (
          <GuardButton
            dirty={dirty}
            disabled={!permissions.writable}
            text={t.create}
            action={() => {
              choose(newRecord(resourceKey), false);
            }}
          />
        )}
        <Field
          label={t.search}
          value={filter}
          onChange={(event) => {
            setFilter(event.target.value);
          }}
        />
        <ul className="admin-records">
          {rows
            .filter(
              (row) =>
                [row.name, row.code, row[resource.id]].some((value) =>
                  string(value).toLocaleLowerCase().includes(filter.toLocaleLowerCase()),
                ) || resource.singleton,
            )
            .map((row, index) => (
              <li key={string(row[resource.id]) || index}>
                <GuardButton
                  dirty={dirty}
                  text={string(row.name) || string(row[resource.id]) || t[resourceKey]}
                  action={() => {
                    choose(row, true);
                  }}
                />
              </li>
            ))}
        </ul>
        {!query.isPending && !rows.length && <p>{t.empty}</p>}
        {string(query.data?.nextPageToken) && (
          <GuardButton
            dirty={dirty}
            text={t.next}
            action={() => {
              setPage(string(query.data?.nextPageToken));
            }}
          />
        )}
        {page && (
          <GuardButton
            dirty={dirty}
            text={t.previous}
            action={() => {
              setPage("");
            }}
          />
        )}
      </section>
      {selection && (
        <Editor
          key={selection.revision}
          resourceKey={resourceKey}
          value={selection.value}
          existing={selection.existing}
          api={api}
          permissions={{ ...permissions, writable: permissions.writable && query.isSuccess }}
          onDirty={onDirty}
          reload={() => {
            void query.refetch().then((result) => {
              if (result.error || !result.data) return;
              const candidates = resource.singleton
                ? [record(result.data[resource.item])]
                : list(result.data[resource.collection]).map(record);
              const current = candidates.find(
                (row) => !resource.id || row[resource.id] === selection.value[resource.id],
              );
              if (current) choose(current, true);
            });
          }}
        />
      )}
    </div>
  );
}
function Workspace({ integration }: { readonly integration: boolean }) {
  const { shell, session } = useShell();
  const { t } = useAdminStrings();
  const mode = usePreferences((state) => state.mode);
  const [section, setSection] = useState<Section>(integration ? "connectors" : "locations");
  const [dirty, setDirty] = useState(false);
  const [online, setOnline] = useState(navigator.onLine);
  const [revoked, setRevoked] = useState(false);
  const api = useMemo(() => new AdminApi(shell.auth.token), [shell]);
  const denied = useCallback(() => {
    setRevoked(true);
  }, []);
  const me = useQuery({
    queryKey: ["admin", "me"],
    queryFn: ({ signal }) => api.get("/v1/me", signal),
    retry: false,
    refetchInterval: 5000,
  });
  useLayoutEffect(() => {
    api.activate();
    return () => {
      api.dispose();
    };
  }, [api]);
  useEffect(() => {
    const sync = () => {
      setOnline(navigator.onLine);
    };
    window.addEventListener("online", sync);
    window.addEventListener("offline", sync);
    return () => {
      window.removeEventListener("online", sync);
      window.removeEventListener("offline", sync);
    };
  }, []);
  const roles = list(me.data?.roles).map(string);
  const permitted = roles.includes("admin") || (integration && roles.includes("integrator"));
  const permissions = useMemo<Permissions>(
    () => ({
      writable: online && me.isSuccess && mode !== "training" && mode !== "wall",
      global: list(me.data?.siteIds).includes("*"),
      denied,
    }),
    [online, me.isSuccess, me.data, mode, denied],
  );
  const context = JSON.stringify([
    session.profile?.context,
    me.data?.roles,
    me.data?.siteIds,
    me.data?.zoneIds,
    me.data?.actions,
  ]);
  if (revoked || [401, 403].includes(status(me.error)) || (me.isSuccess && !permitted))
    return (
      <main id="main-content" tabIndex={-1} className="workbench">
        <h1>{integration ? t.integrations : t.module}</h1>
        <Notice tone="warning">{t.forbidden}</Notice>
        <Button
          onClick={() => {
            void me.refetch().then((result) => {
              if (result.isSuccess) setRevoked(false);
            });
          }}
        >
          {t.reload}
        </Button>
      </main>
    );
  const sections: Section[] = [
    ...(Object.keys(resources) as ResourceKey[]).filter(
      (key) => !integration || (resources[key] as Resource).integration,
    ),
    ...(!integration ? (["imports", "floorPlans", "access"] as const) : (["access"] as const)),
  ];
  const identity = identityConsole(window.psimConfig?.issuer ?? "http://localhost:8180/realms/psim");
  return (
    <main id="main-content" tabIndex={-1} className="workbench admin-workspace">
      <h1>{integration ? t.integrations : t.module}</h1>
      {!online || me.error ? <Notice tone="warning">{t.offline}</Notice> : null}
      {(mode === "training" || mode === "wall") && <Notice>{t.readOnly}</Notice>}
      {me.isPending ? (
        <p role="status">{t.loading}</p>
      ) : (
        me.data && (
          <>
            <nav aria-label={t.module} className="admin-nav">
              {sections.map((key) => (
                <GuardButton
                  key={key}
                  dirty={dirty}
                  text={t[key]}
                  action={() => {
                    setSection(key);
                    setDirty(false);
                  }}
                />
              ))}
            </nav>
            <AccessBoundary key={context}>
              <div key={section}>
                {section === "imports" ? (
                  <Imports api={api} permissions={permissions} />
                ) : section === "floorPlans" ? (
                  <FloorPlans api={api} permissions={permissions} />
                ) : section === "access" ? (
                  <section>
                    <h2>{t.access}</h2>
                    <p>{t.accessHint}</p>
                    <Result
                      value={{
                        userId: me.data.userId ?? "",
                        tenantId: me.data.tenantId ?? "",
                        roles: me.data.roles ?? [],
                        siteIds: me.data.siteIds ?? [],
                        zoneIds: me.data.zoneIds ?? [],
                        actions: me.data.actions ?? [],
                      }}
                    />
                    {identity && (
                      <a href={identity} target="_blank" rel="noreferrer">
                        {t.identity}
                      </a>
                    )}
                    <Button
                      onClick={() => {
                        void me.refetch();
                      }}
                    >
                      {t.reload}
                    </Button>
                  </section>
                ) : (
                  <Collection
                    key={section}
                    resourceKey={section}
                    api={api}
                    permissions={permissions}
                    dirty={dirty}
                    onDirty={setDirty}
                  />
                )}
              </div>
            </AccessBoundary>
          </>
        )
      )}
    </main>
  );
}
function AccessBoundary({ children }: { readonly children: ReactNode }) {
  const [client] = useState(() => new QueryClient());
  useEffect(
    () => () => {
      void client.cancelQueries();
      client.clear();
    },
    [client],
  );
  return <QueryClientProvider client={client}>{children}</QueryClientProvider>;
}
export function AdministrationPage({ integration = false }: { readonly integration?: boolean }) {
  const { session } = useShell();
  return (
    <AdministrationSession key={`${session.profile?.context ?? ""}/${String(integration)}`} integration={integration} />
  );
}
function AdministrationSession({ integration }: { readonly integration: boolean }) {
  const [client] = useState(() => new QueryClient());
  useEffect(
    () => () => {
      void client.cancelQueries();
      client.clear();
    },
    [client],
  );
  return (
    <QueryClientProvider client={client}>
      <Workspace integration={integration} />
    </QueryClientProvider>
  );
}
