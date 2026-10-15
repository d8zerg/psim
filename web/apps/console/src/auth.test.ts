import { webcrypto } from "node:crypto";
import { describe, expect, it, vi } from "vitest";
import { SessionAuth, type AuthEnvironment } from "./auth";
function environment(search = "") {
  const storage = new Map<string, string>();
  const assign = vi.fn();
  const cleanUrl = vi.fn();
  const fetch = vi
    .fn<typeof globalThis.fetch>()
    .mockResolvedValue(
      new Response(JSON.stringify({ access_token: "token", expires_in: 300, refresh_token: "refresh" })),
    );
  const env: AuthEnvironment = {
    issuer: "https://identity/realms/psim",
    location: { origin: "https://psim", pathname: "/", search, assign },
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
  return { env, storage, fetch, assign, cleanUrl };
}
describe("OIDC session", () => {
  it("starts unauthenticated and creates a PKCE login with a fresh state", async () => {
    const fixture = environment();
    const auth = new SessionAuth(fixture.env);
    expect(await auth.token()).toBeUndefined();
    await auth.login();
    const url = new URL(String(fixture.assign.mock.calls[0]?.[0]));
    expect(url.searchParams.get("code_challenge_method")).toBe("S256");
    expect(url.searchParams.get("redirect_uri")).toBe("https://psim/");
    expect(fixture.storage.get("psim.pkce")).toContain("verifier");
  });
  it("exchanges a matching callback once, removes the verifier, and keeps tokens in memory", async () => {
    const fixture = environment("?code=code&state=state");
    fixture.storage.set(
      "psim.pkce",
      JSON.stringify({ state: "state", verifier: "verifier", redirect: "https://psim/" }),
    );
    const auth = new SessionAuth(fixture.env);
    expect(await auth.token()).toBe("token");
    expect(await auth.token()).toBe("token");
    expect(fixture.fetch).toHaveBeenCalledOnce();
    expect(fixture.cleanUrl).toHaveBeenCalledOnce();
    expect(fixture.storage.size).toBe(0);
  });
  it("rejects a callback without its matching state", async () => {
    const fixture = environment("?code=code&state=wrong");
    await expect(new SessionAuth(fixture.env).token()).rejects.toThrow("session expired");
    expect(fixture.fetch).not.toHaveBeenCalled();
  });
  it("rejects token endpoint errors and malformed responses", async () => {
    for (const body of [{}, { access_token: 1, expires_in: 2 }, { access_token: "x" }]) {
      const fixture = environment("?code=code&state=state");
      fixture.storage.set(
        "psim.pkce",
        JSON.stringify({ state: "state", verifier: "verifier", redirect: "https://psim/" }),
      );
      fixture.fetch.mockResolvedValue(new Response(JSON.stringify(body), { status: 400 }));
      await expect(new SessionAuth(fixture.env).token()).rejects.toThrow("Sign-in failed");
    }
  });
  it("refreshes expiring credentials and drops an expired session without a refresh token", async () => {
    for (const refresh of [true, false]) {
      const fixture = environment("?code=code&state=state");
      fixture.storage.set(
        "psim.pkce",
        JSON.stringify({ state: "state", verifier: "verifier", redirect: "https://psim/" }),
      );
      fixture.fetch.mockResolvedValueOnce(
        new Response(
          JSON.stringify({ access_token: "old", expires_in: 0, ...(refresh ? { refresh_token: "r" } : {}) }),
        ),
      );
      expect(await new SessionAuth(fixture.env).token()).toBe(refresh ? "token" : undefined);
      expect(fixture.fetch).toHaveBeenCalledTimes(refresh ? 2 : 1);
    }
  });
});
