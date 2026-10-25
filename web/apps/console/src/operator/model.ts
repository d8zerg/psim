import type { Device, DeviceStatus, Location } from "./api";
import type { FeedRow } from "../feed/controller";
export function filteredFeed(
  rows: readonly FeedRow[],
  search: string,
  priority: string,
  state: string,
  site: string,
  assignee: string,
) {
  const query = search.trim().toLocaleLowerCase();
  return rows.filter(
    (row) =>
      (!priority || row.priority === priority) &&
      (!state || row.state === state) &&
      (!site || row.siteId === site) &&
      (!assignee || row.assigneeId === assignee) &&
      (!query ||
        [row.id, row.title, row.eventId, row.siteId, row.zoneId].some((value) =>
          value?.toLocaleLowerCase().includes(query),
        )),
  );
}
export function deviceHealth(
  device: Device,
  status: DeviceStatus | undefined,
  connected: boolean,
  maintained: boolean,
): "normal" | "alarm" | "offline" {
  if (
    !connected ||
    status?.quality !== "DATA_QUALITY_ACTUAL" ||
    status.connection !== "online" ||
    !status.online ||
    device.state !== "RESOURCE_STATE_ACTIVE" ||
    maintained
  )
    return "offline";
  return ["EVENT_CLASS_ALARM", "EVENT_CLASS_FAULT", "EVENT_CLASS_WARNING"].includes(status.state ?? "")
    ? "alarm"
    : "normal";
}
export function locationTree(
  locations: readonly Location[],
  parentId = "",
  ancestors = new Set<string>(),
): { id: string; label: string; children: ReturnType<typeof locationTree> }[] {
  return locations
    .filter(
      (item) =>
        ((item.parentId ?? "") === parentId ||
          (!parentId && !locations.some((parent) => parent.locationId === item.parentId))) &&
        item.locationId &&
        !ancestors.has(item.locationId),
    )
    .map((item) => ({
      id: item.locationId ?? "",
      label: item.name ?? item.code ?? "",
      children: locationTree(locations, item.locationId, new Set([...ancestors, item.locationId ?? ""])),
    }));
}
export function errorKind(status: number): "conflict" | "forbidden" | "unknownResult" | "operationFailed" {
  if (status === 409 || status === 412) return "conflict";
  if (status === 401 || status === 403) return "forbidden";
  return status === 0 || status >= 500 ? "unknownResult" : "operationFailed";
}
