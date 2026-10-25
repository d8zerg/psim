import { ApiProblemError, isProblem } from "@psim/api-client";
export type JsonValue = string | number | boolean | null | JsonValue[] | { [key: string]: JsonValue };
export type JsonRecord = Record<string, JsonValue>;
export class HttpFailure extends Error {
  constructor(readonly status: number) {
    super(String(status));
  }
}
export function status(error: unknown): number {
  return error instanceof HttpFailure ? error.status : error instanceof ApiProblemError ? error.problem.status : 0;
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
  body: JsonRecord | Blob,
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
export class SessionHttp {
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
    signal?.throwIfAborted();
    if (!token) throw new HttpFailure(401);
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
      throw new HttpFailure(response.status);
    }
    return { response, lifetime };
  }
  async get(path: string, signal?: AbortSignal): Promise<JsonRecord> {
    const { response, lifetime } = await this.request(path, {}, signal);
    const body: unknown = await response.json();
    lifetime.throwIfAborted();
    signal?.throwIfAborted();
    return body !== null && typeof body === "object" && !Array.isArray(body) ? (body as JsonRecord) : {};
  }
  async send(request: Write, signal?: AbortSignal): Promise<JsonRecord> {
    const { response, lifetime } = await this.request(
      request.path,
      {
        method: request.method,
        body: request.body,
        headers: {
          "Content-Type": request.type,
          "Idempotency-Key": request.key,
          ...(request.version === undefined ? {} : { "If-Match": `"${request.version}"` }),
        },
      },
      signal,
    );
    const body: unknown = await response.json();
    lifetime.throwIfAborted();
    signal?.throwIfAborted();
    return body !== null && typeof body === "object" && !Array.isArray(body) ? (body as JsonRecord) : {};
  }
  async blob(path: string, signal?: AbortSignal) {
    const { response, lifetime } = await this.request(path, {}, signal);
    const blob = await response.blob();
    lifetime.throwIfAborted();
    signal?.throwIfAborted();
    return blob;
  }
}
