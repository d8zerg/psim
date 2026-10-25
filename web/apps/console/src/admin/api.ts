import { write, type Write } from "../ui/http";
export { SessionHttp as AdminApi, HttpFailure as AdminFailure, status, write, type Write } from "../ui/http";
import { string, type RecordValue, type ResourceKey, type Resource, resources } from "./model";
export function saveRequest(key: ResourceKey, body: RecordValue, original?: RecordValue, certificatePem = ""): Write {
  const resource: Resource = resources[key];
  const version = original ? string(original.version) : undefined;
  if (key === "mappings")
    return write(
      `${resource.path}/${encodeURIComponent(string(body.connectorType))}`,
      { ...body, version: Number(version ?? 0) + 1 },
      "PUT",
      version ?? "0",
    );
  if (key === "rules" || key === "plans")
    return write(
      resource.path,
      { ...body, version: Number(version ?? 0) + 1, state: key === "rules" ? "RULE_STATE_DRAFT" : "PLAN_STATE_DRAFT" },
      "POST",
      version,
    );
  if (resource.singleton) return write(`/v1/settings/${key}`, body, "PUT", version);
  if (key === "connectors") return write(`${resource.path}?${new URLSearchParams({ certificatePem })}`, body);
  return write(
    resource.path + (original ? `/${encodeURIComponent(string(body[resource.id]))}` : ""),
    body,
    original ? "PUT" : "POST",
    version,
  );
}
export function identityConsole(issuer: string): string | undefined {
  try {
    const url = new URL(issuer);
    if (!["http:", "https:"].includes(url.protocol) || url.username || url.password) return undefined;
    const index = url.pathname.lastIndexOf("/realms/");
    if (index < 0) return undefined;
    const realm = url.pathname.slice(index + 8).replace(/\/$/u, "");
    if (!realm || realm.includes("/")) return undefined;
    return `${url.origin}${url.pathname.slice(0, index)}/admin/${encodeURIComponent(realm)}/console/`;
  } catch {
    return undefined;
  }
}
