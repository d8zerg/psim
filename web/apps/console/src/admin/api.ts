import { ApiProblemError, isProblem } from "@psim/api-client";
import { record, string, type RecordValue, type ResourceKey, type Resource, resources } from "./model";
export class AdminFailure extends Error {
  constructor(readonly status: number) {
    super(String(status));
  }
}
export function status(error: unknown): number {
  return error instanceof AdminFailure ? error.status : error instanceof ApiProblemError ? error.problem.status : 0;
}
export interface Write {
  readonly path: string;
  readonly method: "POST" | "PUT";
  readonly body: string | Blob;
  readonly type: string;
  readonly version?: string;
  readonly key: string;
}
export function write(
  path: string,
  body: RecordValue | Blob,
  method: Write["method"] = "POST",
  version?: string,
  type = "application/json",
): Write {
  return {
    path,
    body: body instanceof Blob ? body : JSON.stringify(body),
    method,
    type,
    ...(version === undefined ? {} : { version }),
    key: crypto.randomUUID(),
  };
}
export class AdminApi {
  private lifetime = new AbortController();
  constructor(
    private readonly token: () => Promise<string | undefined>,
    private readonly fetcher: typeof fetch = (...args) => fetch(...args),
  ) {}
  activate() {
    if (this.lifetime.signal.aborted) this.lifetime = new AbortController();
  }
  dispose() {
    this.lifetime.abort();
  }
  private async request(path: string, options: RequestInit, signal?: AbortSignal) {
    const lifetime = this.lifetime.signal;
    const token = await this.token();
    lifetime.throwIfAborted();
    if (!token) throw new AdminFailure(401);
    const headers = new Headers(options.headers);
    headers.set("Authorization", `Bearer ${token}`);
    const response = await this.fetcher(path, {
      ...options,
      credentials: "omit",
      cache: "no-store",
      signal: AbortSignal.any([lifetime, AbortSignal.timeout(30000), ...(signal ? [signal] : [])]),
      headers,
    });
    if (!response.ok) {
      const problem: unknown = await response.json().catch(() => null);
      if (isProblem(problem)) throw new ApiProblemError(problem);
      throw new AdminFailure(response.status);
    }
    return { response, lifetime };
  }
  async get(path: string, signal?: AbortSignal): Promise<RecordValue> {
    const { response, lifetime } = await this.request(path, {}, signal);
    const body = (await response.json()) as RecordValue;
    lifetime.throwIfAborted();
    return record(body);
  }
  async send(request: Write): Promise<RecordValue> {
    const { response, lifetime } = await this.request(request.path, {
      method: request.method,
      body: request.body,
      headers: {
        "Content-Type": request.type,
        "Idempotency-Key": request.key,
        ...(request.version === undefined ? {} : { "If-Match": `"${request.version}"` }),
      },
    });
    const body = (await response.json()) as RecordValue;
    lifetime.throwIfAborted();
    return record(body);
  }
  async blob(path: string, signal?: AbortSignal) {
    const { response, lifetime } = await this.request(path, {}, signal);
    const blob = await response.blob();
    lifetime.throwIfAborted();
    return blob;
  }
}
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
