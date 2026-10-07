import { describe, expect, it } from "vitest";

import { ApiProblemError, createApiClient, isProblem, type Problem } from "./index";

function recordingFetch(response: Response) {
  const requests: Request[] = [];
  const fetchImpl: typeof globalThis.fetch = (input, init) => {
    requests.push(input instanceof Request ? input : new Request(input, init));
    return Promise.resolve(response.clone());
  };
  return { requests, fetchImpl };
}

const problem: Problem = {
  type: "https://psim.example/errors/AUTH_SCOPE_DENIED",
  title: "Access scope denied",
  status: 403,
  code: "AUTH_SCOPE_DENIED",
};

describe("createApiClient", () => {
  it("calls a contract path with the access token", async () => {
    const { requests, fetchImpl } = recordingFetch(Response.json({ devices: [] }));
    const client = createApiClient({ baseUrl: "https://psim.test", getAccessToken: () => "token-1", fetch: fetchImpl });

    const { data, error } = await client.GET("/v1/devices", { params: { query: { pageSize: 10 } } });

    expect(error).toBeUndefined();
    expect(data).toEqual({ devices: [] });
    expect(requests[0]?.url).toBe("https://psim.test/v1/devices?pageSize=10");
    expect(requests[0]?.headers.get("Authorization")).toBe("Bearer token-1");
  });

  it("sends no Authorization header without a token", async () => {
    const { requests, fetchImpl } = recordingFetch(Response.json({}));
    const client = createApiClient({ baseUrl: "https://psim.test", getAccessToken: () => undefined, fetch: fetchImpl });

    await client.GET("/v1/devices");

    expect(requests[0]?.headers.has("Authorization")).toBe(false);
  });

  it("returns an RFC 9457 problem as the error", async () => {
    const response = new Response(JSON.stringify(problem), {
      status: 403,
      headers: { "Content-Type": "application/problem+json" },
    });
    const { fetchImpl } = recordingFetch(response);
    const client = createApiClient({
      baseUrl: "https://psim.test",
      getAccessToken: () => Promise.resolve("t"),
      fetch: fetchImpl,
    });

    const { error } = await client.GET("/v1/devices");

    expect(isProblem(error)).toBe(true);
    expect(new ApiProblemError(error as Problem).message).toBe("AUTH_SCOPE_DENIED: Access scope denied");
  });
});

describe("isProblem", () => {
  it.each([null, "text", 42, {}, { ...problem, code: 7 }, { title: "x", status: 400, code: "C" }])(
    "rejects %j",
    (value) => {
      expect(isProblem(value)).toBe(false);
    },
  );

  it("accepts a problem with the catalog code", () => {
    expect(isProblem(problem)).toBe(true);
  });
});
