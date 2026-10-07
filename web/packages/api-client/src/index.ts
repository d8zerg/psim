// REST v1 client of the PSIM console (ADR-026). Request and response types are generated from the
// OpenAPI contract on every build (src/gen/schema.ts), so the client cannot drift from it.
import createClient, { type Client, type Middleware } from "openapi-fetch";

import type { components, paths } from "./gen/schema";

export type { components, paths };
export type Problem = components["schemas"]["Problem"];

export interface ApiClientOptions {
  /** Origin of the API Gateway, for example https://psim.example.org. */
  readonly baseUrl: string;
  /** Current access token (OIDC, ADR-020); undefined sends the request without one. */
  readonly getAccessToken: () => string | undefined | Promise<string | undefined>;
  /** Fetch implementation; the global fetch by default. */
  readonly fetch?: typeof globalThis.fetch;
}

export type ApiClient = Client<paths>;

/** Error raised for an RFC 9457 problem response: the code comes from the error catalog. */
export class ApiProblemError extends Error {
  readonly problem: Problem;

  constructor(problem: Problem) {
    super(`${problem.code}: ${problem.title}`);
    this.name = "ApiProblemError";
    this.problem = problem;
  }
}

/** Narrow an unknown error body to a Problem (application/problem+json). */
export function isProblem(body: unknown): body is Problem {
  if (typeof body !== "object" || body === null) {
    return false;
  }
  const candidate = body as Partial<Record<keyof Problem, unknown>>;
  return (
    typeof candidate.type === "string" &&
    typeof candidate.title === "string" &&
    typeof candidate.status === "number" &&
    typeof candidate.code === "string"
  );
}

function authMiddleware(getAccessToken: ApiClientOptions["getAccessToken"]): Middleware {
  return {
    async onRequest({ request }) {
      const token = await getAccessToken();
      if (token !== undefined) {
        request.headers.set("Authorization", `Bearer ${token}`);
      }
      return request;
    },
  };
}

export function createApiClient(options: ApiClientOptions): ApiClient {
  const client = createClient<paths>({
    baseUrl: options.baseUrl,
    ...(options.fetch === undefined ? {} : { fetch: options.fetch }),
  });
  client.use(authMiddleware(options.getAccessToken));
  return client;
}
