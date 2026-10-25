import { useInfiniteQuery, useQuery } from "@tanstack/react-query";
import { useEffect, useMemo, useRef, useState } from "react";
import { Badge, Button, Card, Field, Notice } from "@psim/design-system";
import { Tree } from "@psim/design-system/tree";
import { statusOf, type Device, type Location, type OperatorApi, type Schema } from "./api";
import { deviceHealth, locationTree } from "./model";
import { queryDefaults, type Actions } from "./queries";
import { useStrings } from "../ui/strings";
export function ObjectPanel({
  api,
  live,
  writable,
  supervisor,
  actions,
  watchLocation,
}: {
  readonly api: OperatorApi;
  readonly live: boolean;
  readonly writable: boolean;
  readonly supervisor: boolean;
  readonly actions: Actions;
  readonly watchLocation: (id: string, token: string) => () => void;
}) {
  const t = useStrings();
  const [selected, setSelected] = useState("");
  const locations = useInfiniteQuery({
    ...queryDefaults,
    refetchInterval: 30000,
    queryKey: ["locations"],
    queryFn: ({ signal, pageParam }) => api.locations(signal, "", pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  const entries = useMemo(() => locations.data?.pages.flatMap((page) => page.locations ?? []) ?? [], [locations.data]);
  const nodes = useMemo(() => locationTree(entries), [entries]);
  const location = entries.find((item) => item.locationId === selected);
  const floor =
    location?.kind === "LOCATION_KIND_ZONE"
      ? (entries.find((item) => item.locationId === location.parentId) ?? location)
      : location;
  const siteId = location?.siteId ?? (location?.kind === "LOCATION_KIND_SITE" ? location.locationId : "") ?? "";
  if ([401, 403].includes(statusOf(locations.error))) return <Notice tone="warning">{t.forbidden}</Notice>;
  return (
    <Card title={t.catalog}>
      {locations.isError && <Notice tone="warning">{t.detailsError}</Notice>}
      <Tree
        label={t.structure}
        nodes={nodes}
        loadingLabel={t.loading}
        errorLabel={t.detailsError}
        onSelect={setSelected}
      />
      {locations.hasNextPage && (
        <Button
          disabled={locations.isFetchingNextPage || !live}
          onClick={() => {
            void locations.fetchNextPage();
          }}
        >
          {t.loadMore}
        </Button>
      )}
      {floor && (
        <LocationPanel
          key={floor.locationId}
          location={floor}
          api={api}
          live={live && !locations.isError}
          watchLocation={watchLocation}
        />
      )}
      {siteId && (
        <DutyPanel
          key={siteId}
          siteId={siteId}
          api={api}
          live={live}
          writable={writable && !locations.isError}
          supervisor={supervisor}
          actions={actions}
        />
      )}
    </Card>
  );
}
function LocationPanel({
  location,
  api,
  live,
  watchLocation,
}: {
  readonly location: Location;
  readonly api: OperatorApi;
  readonly live: boolean;
  readonly watchLocation: (id: string, token: string) => () => void;
}) {
  const t = useStrings();
  const id = location.locationId ?? "";
  const [selected, setSelected] = useState("");
  const [zoom, setZoom] = useState(1);
  const devices = useInfiniteQuery({
    ...queryDefaults,
    refetchInterval: 30000,
    queryKey: ["devices", id],
    queryFn: ({ signal, pageParam }) => api.devices(id, signal, pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  const status = useQuery({
    ...queryDefaults,
    queryKey: ["location-status", id],
    queryFn: ({ signal }) => api.locationStatus(id, signal),
    enabled: live,
  });
  const plan = useQuery({
    queryKey: ["floor-plan", id, location.floorPlan?.file?.version ?? location.version],
    queryFn: ({ signal }) => api.floorPlan(id, signal),
    enabled: live && Boolean(location.floorPlan),
    retry: false,
  });
  const forbidden = [devices.error, status.error, plan.error].some((error) => [401, 403].includes(statusOf(error)));
  const image = useRef<HTMLImageElement>(null);
  useEffect(() => {
    if (forbidden || !plan.data || !["image/png", "image/jpeg"].includes(plan.data.type)) return;
    const url = URL.createObjectURL(plan.data);
    if (image.current) image.current.src = url;
    return () => {
      URL.revokeObjectURL(url);
    };
  }, [plan.data, forbidden]);
  const token = status.data?.resumeToken;
  useEffect(() => {
    if (live && token && !forbidden) return watchLocation(id, token);
    return undefined;
  }, [id, live, token, watchLocation, forbidden]);
  if (forbidden) return <Notice tone="warning">{t.forbidden}</Notice>;
  const records = devices.data?.pages.flatMap((page) => page.devices ?? []) ?? [];
  const current = live && !status.isError && !devices.isError;
  function maintained(device: Device) {
    return (
      status.data?.maintenanceWindows?.some(
        (window) =>
          window.state === "MAINTENANCE_STATE_ACTIVE" &&
          (window.deviceId === device.deviceId || window.zoneId === device.zoneId),
      ) ?? false
    );
  }
  const width = location.floorPlan?.width ?? 1;
  const height = location.floorPlan?.height ?? 1;
  const actualImage = !plan.isError && Boolean(plan.data && ["image/png", "image/jpeg"].includes(plan.data.type));
  return (
    <section className="grid gap-2" aria-label={t.planTitle}>
      <h3>{location.name}</h3>
      {(!current || plan.isError) && <Notice tone="warning">{t.stale}</Notice>}
      <div className="flex flex-wrap gap-2">
        <Button
          variant="secondary"
          disabled={zoom >= 3}
          onClick={() => {
            setZoom((value) => Math.min(3, value + 0.25));
          }}
        >
          {t.zoomIn}
        </Button>
        <Button
          variant="secondary"
          disabled={zoom <= 0.5}
          onClick={() => {
            setZoom((value) => Math.max(0.5, value - 0.25));
          }}
        >
          {t.zoomOut}
        </Button>
        <Button
          variant="secondary"
          onClick={() => {
            setZoom(1);
          }}
        >
          {t.resetZoom}
        </Button>
      </div>
      {actualImage ? (
        <div className="operator-plan-scroll">
          <div className="operator-plan" style={{ width: `${String(zoom * 100)}%` }}>
            <img ref={image} alt={`${t.planTitle}: ${location.name ?? ""}`} />
            {records
              .filter((device) => device.position)
              .map((device) => {
                const state = status.data?.devices?.find((item) => item.deviceId === device.deviceId);
                const health = deviceHealth(device, state, current, maintained(device));
                return (
                  <div
                    className="operator-marker"
                    key={device.deviceId}
                    style={{
                      left: `${String(Math.max(0, Math.min(100, ((device.position?.x ?? 0) / width) * 100)))}%`,
                      top: `${String(Math.max(0, Math.min(100, ((device.position?.y ?? 0) / height) * 100)))}%`,
                    }}
                  >
                    <Button
                      variant="secondary"
                      aria-pressed={selected === device.deviceId}
                      onClick={() => {
                        setSelected(device.deviceId ?? "");
                      }}
                    >
                      <Badge tone={health === "alarm" ? "danger" : health === "offline" ? "warning" : "neutral"}>
                        {device.name ?? device.deviceId} ·{" "}
                        {health === "alarm" ? t.activeAlarm : health === "offline" ? t.unknown : t.healthy}
                      </Badge>
                    </Button>
                  </div>
                );
              })}
          </div>
        </div>
      ) : (
        <p>{t.noPlan}</p>
      )}
      <ul aria-label={t.legend}>
        {records.map((device) => {
          const state = status.data?.devices?.find((item) => item.deviceId === device.deviceId);
          const maintenance = maintained(device);
          const health = deviceHealth(device, state, current, maintenance);
          return (
            <li key={device.deviceId} data-testid="device-status" data-device-id={device.deviceId} data-health={health}>
              <Button
                variant="ghost"
                aria-pressed={selected === device.deviceId}
                onClick={() => {
                  setSelected(device.deviceId ?? "");
                }}
              >
                {device.name ?? device.deviceId}
              </Button>
              <p>
                {t.status}: {health === "normal" ? t.healthy : health === "alarm" ? t.activeAlarm : t.unknown} ·{" "}
                {t.quality}:{" "}
                {!current || state?.quality === "DATA_QUALITY_STALE"
                  ? t.staleQuality
                  : state?.quality === "DATA_QUALITY_ACTUAL"
                    ? t.actual
                    : t.unknown}{" "}
                · {t.connection}:{" "}
                {current && state?.connection === "online" && state.online
                  ? t.healthy
                  : state?.connection === "offline"
                    ? t.lost
                    : t.unknown}{" "}
                · {t.confirmedAt}: {state?.confirmedAt ?? t.unknown}
              </p>
              <p>
                {t.maintenance}: {maintenance ? t.maintenance : t.unknown} ·{" "}
                {device.state === "RESOURCE_STATE_DISABLED"
                  ? t.disabled
                  : device.state === "RESOURCE_STATE_ARCHIVED"
                    ? t.archived
                    : t.status}
              </p>
              {selected === device.deviceId && (
                <dl>
                  <dt>{t.deviceId}</dt>
                  <dd>{device.deviceId}</dd>
                  <dt>{t.source}</dt>
                  <dd>{device.connectorId}</dd>
                  {state?.sources?.map((source) => (
                    <div key={source.sourceId}>
                      <dt>{source.sourceId}</dt>
                      <dd>
                        {t.connection}: {source.availability ?? t.unknown} · {t.confirmedAt}:{" "}
                        {source.availabilityConfirmedAt ?? t.unknown}
                      </dd>
                    </div>
                  ))}
                </dl>
              )}
            </li>
          );
        })}
      </ul>
      {devices.hasNextPage && (
        <Button
          disabled={devices.isFetchingNextPage || !live}
          onClick={() => {
            void devices.fetchNextPage();
          }}
        >
          {t.loadMore}
        </Button>
      )}
    </section>
  );
}
function DutyPanel({
  siteId,
  api,
  live,
  writable,
  supervisor,
  actions,
}: {
  readonly siteId: string;
  readonly api: OperatorApi;
  readonly live: boolean;
  readonly writable: boolean;
  readonly supervisor: boolean;
  readonly actions: Actions;
}) {
  const t = useStrings();
  const [user, setUser] = useState("");
  const [fallback, setFallback] = useState("");
  const duty = useQuery({
    ...queryDefaults,
    queryKey: ["duty", siteId],
    queryFn: ({ signal }) => api.duty(siteId, signal),
    enabled: live,
  });
  if ([401, 403].includes(statusOf(duty.error))) return <Notice tone="warning">{t.forbidden}</Notice>;
  const body: Schema["psim.api.v1.SiteDuty"] = {
    siteId,
    userId: user,
    fallbackUserId: fallback,
    version: duty.data?.duty?.version ?? "0",
  };
  return (
    <section className="grid gap-2">
      <h3>{t.duty}</h3>
      <p>
        {t.dutyOperator}: {duty.data?.duty?.userId ?? t.unknown} · {t.fallback}:{" "}
        {duty.data?.duty?.fallbackUserId ?? t.unknown}
      </p>
      {duty.isError && <Notice tone="warning">{t.detailsError}</Notice>}
      {supervisor && (
        <>
          <Field
            label={t.dutyOperator}
            value={user}
            onChange={(event) => {
              setUser(event.target.value);
            }}
          />
          <Field
            label={t.fallback}
            value={fallback}
            onChange={(event) => {
              setFallback(event.target.value);
            }}
          />
          <Button
            variant="secondary"
            disabled={!writable || !duty.isSuccess || duty.isError || !fallback.trim()}
            onClick={() => {
              void actions.perform(() => api.setDuty(siteId, body));
            }}
          >
            {t.save}
          </Button>
        </>
      )}
    </section>
  );
}
