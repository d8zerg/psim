import { webcrypto } from "node:crypto";
import { afterEach, describe, expect, it, vi } from "vitest";
import { SessionAuth, SessionError, type AuthEnvironment } from "./auth";
import { issuer, jwks, nonce, state, tokens } from "../e2e/identity";
export function fixture(search = `?code=code&state=${state}`) {
  const storage = new Map<string, string>();
  const assign = vi.fn();
  const cleanUrl = vi.fn();
  const exchange = vi.fn(async () => new Response(JSON.stringify(await tokens())));
  const fetch = vi.fn<typeof globalThis.fetch>(async (url) =>
    (typeof url === "string" ? url : url instanceof URL ? url.href : url.url).endsWith("/certs")
      ? new Response(JSON.stringify(jwks))
      : exchange(),
  );
  const env: AuthEnvironment = {
    issuer,
    location: { origin: "https://psim.test", pathname: "/", search, assign },
    storage: {
      getItem: (key) => storage.get(key) ?? null,
      setItem: (key, value) => {
        storage.set(key, value);
      },
      removeItem: (key) => {
        storage.delete(key);
      },
      clear: () => {
        storage.clear();
      },
      key: () => null,
      length: 0,
    },
    crypto: webcrypto as Crypto,
    fetch,
    cleanUrl,
  };
  const saved = {
    state,
    nonce,
    verifier: "v".repeat(43),
    issuer,
    issued: Date.now(),
    redirect: "https://psim.test/",
    returnTo: "/?priority=P1",
  };
  storage.set("psim.pkce", JSON.stringify(saved));
  return { env, storage, exchange, fetch, assign, cleanUrl, saved, auth: new SessionAuth(env) };
}
afterEach(() => {
  vi.useRealTimers();
});
describe("OIDC session", () => {
  it("starts anonymously and stores only a bounded PKCE transaction", async () => {
    const f = fixture("");
    expect(await f.auth.token()).toBeUndefined();
    await f.auth.login();
    const url = new URL(String(f.assign.mock.calls[0]?.[0]));
    expect(url.searchParams.get("code_challenge_method")).toBe("S256");
    expect(url.searchParams.get("nonce")).toHaveLength(43);
    const saved: unknown = JSON.parse(f.storage.get("psim.pkce") ?? "{}");
    expect(JSON.stringify(saved)).not.toContain("access_token");
  });
  it("validates identity and exchanges a callback once under concurrent calls", async () => {
    const f = fixture();
    const [first, second] = await Promise.all([f.auth.token(), f.auth.token()]);
    expect(first).toBe(second);
    expect(f.exchange).toHaveBeenCalledOnce();
    expect(f.auth.getSnapshot().profile?.roles).toEqual(["operator"]);
    expect(f.cleanUrl).toHaveBeenLastCalledWith("/?priority=P1");
    expect(f.storage.size).toBe(0);
  });
  it.each([
    "?code=code&state=wrong",
    "?error=access_denied",
    `?code=a&code=b&state=${state}`,
    `?code=x&state=${state}&iss=https://other`,
  ])("rejects invalid callbacks %s without exchanging credentials", async (query) => {
    const f = fixture(query);
    await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
    expect(f.exchange).not.toHaveBeenCalled();
    expect(f.auth.getSnapshot().status).toBe("anonymous");
    expect(f.cleanUrl).toHaveBeenCalled();
  });
  it("rejects missing, malformed and expired transactions", async () => {
    for (const saved of [
      "{",
      "null",
      JSON.stringify({ ...fixture().saved, issued: Date.now() - 600001 }),
      JSON.stringify({ ...fixture().saved, redirect: "https://foreign/" }),
    ]) {
      const f = fixture();
      f.storage.set("psim.pkce", saved);
      await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
      expect(f.exchange).not.toHaveBeenCalled();
    }
  });
  it("rejects untrusted ID claims, signatures and mismatched subjects", async () => {
    for (const identity of [
      { nonce: "wrong" },
      { aud: "other" },
      { iss: "https://foreign" },
      { exp: 1 },
      { sub: "someone-else" },
    ]) {
      const f = fixture();
      f.exchange.mockResolvedValueOnce(new Response(JSON.stringify(await tokens({ identity }))));
      await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
    }
    const f = fixture();
    const body = await tokens();
    body.id_token = body.id_token.slice(0, -10) + "AAAA";
    f.exchange.mockResolvedValueOnce(new Response(JSON.stringify(body)));
    await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
  });
  it("rejects malformed token and JWKS responses", async () => {
    for (const body of [
      {},
      { access_token: "x", expires_in: 0 },
      { access_token: "x", expires_in: 300, id_token: "invalid" },
    ]) {
      const f = fixture();
      f.exchange.mockResolvedValueOnce(new Response(JSON.stringify(body)));
      await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
    }
    const f = fixture();
    f.fetch.mockImplementation(async (url) =>
      (typeof url === "string" ? url : url instanceof URL ? url.href : url.url).endsWith("/certs")
        ? new Response('{"keys":[]}')
        : f.exchange(),
    );
    await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
  });
  it("coalesces refresh rotation and retains a recoverable session across network failure", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    const f = fixture();
    await f.auth.token();
    vi.setSystemTime(Date.now() + 271000);
    f.exchange.mockRejectedValueOnce(new TypeError("network"));
    await expect(f.auth.token()).rejects.toMatchObject({ terminal: false });
    expect(f.auth.getSnapshot().status).toBe("authenticated");
    await Promise.all([f.auth.token(), f.auth.token(), f.auth.token()]);
    expect(f.exchange).toHaveBeenCalledTimes(3);
  });
  it("invalidates the session when refresh is rejected", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    const f = fixture();
    await f.auth.token();
    vi.setSystemTime(Date.now() + 271000);
    f.exchange.mockResolvedValueOnce(new Response("{}", { status: 400 }));
    await expect(f.auth.token()).rejects.toBeInstanceOf(SessionError);
    expect(f.auth.getSnapshot().profile).toBeUndefined();
  });
  it("clears memory synchronously on logout and rejects a late refresh", async () => {
    vi.useFakeTimers({ toFake: ["Date"] });
    const f = fixture();
    await f.auth.token();
    vi.setSystemTime(Date.now() + 271000);
    let finish: (response: Response) => void = () => undefined;
    f.exchange.mockImplementationOnce(
      () =>
        new Promise((resolve) => {
          finish = resolve;
        }),
    );
    const pending = f.auth.token();
    await vi.waitFor(() => {
      expect(f.exchange).toHaveBeenCalledTimes(2);
    });
    const logout = f.auth.logout();
    expect(f.auth.getSnapshot().status).toBe("anonymous");
    finish(new Response(JSON.stringify(await tokens())));
    await logout;
    expect(await pending).toBeUndefined();
    expect(f.auth.getSnapshot().profile).toBeUndefined();
  });
});
