export interface SessionTokens {
  readonly access_token: string;
  readonly expires_in: number;
  readonly refresh_token?: string;
}
export interface AuthEnvironment {
  readonly issuer: string;
  readonly storage: Storage;
  readonly location: Pick<Location, "origin" | "pathname" | "search" | "assign">;
  readonly cleanUrl: () => void;
  readonly fetch: typeof fetch;
  readonly crypto: Crypto;
}
function base64(bytes: Uint8Array): string {
  return btoa(String.fromCharCode(...bytes))
    .replaceAll("+", "-")
    .replaceAll("/", "_")
    .replaceAll("=", "");
}
export class SessionAuth {
  private tokens: SessionTokens | undefined;
  private expires = 0;
  private completing: Promise<void> | undefined;
  constructor(private readonly environment: AuthEnvironment) {}
  private async exchange(form: URLSearchParams): Promise<void> {
    const response = await this.environment.fetch(`${this.environment.issuer}/protocol/openid-connect/token`, {
      method: "POST",
      headers: { "Content-Type": "application/x-www-form-urlencoded" },
      body: form,
    });
    const body: unknown = await response.json();
    if (
      !response.ok ||
      typeof body !== "object" ||
      body === null ||
      !("access_token" in body) ||
      typeof body.access_token !== "string" ||
      !("expires_in" in body) ||
      typeof body.expires_in !== "number"
    ) {
      this.tokens = undefined;
      throw new Error("Sign-in failed. Please sign in again.");
    }
    this.tokens = {
      access_token: body.access_token,
      expires_in: body.expires_in,
      ...("refresh_token" in body && typeof body.refresh_token === "string"
        ? { refresh_token: body.refresh_token }
        : {}),
    };
    this.expires = Date.now() + body.expires_in * 1000;
  }
  login = async (): Promise<void> => {
    const { crypto, location, storage, issuer } = this.environment;
    const verifier = base64(crypto.getRandomValues(new Uint8Array(32)));
    const state = base64(crypto.getRandomValues(new Uint8Array(32)));
    const challenge = base64(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))));
    const redirect = location.origin + location.pathname;
    storage.setItem("psim.pkce", JSON.stringify({ verifier, state, redirect }));
    const query = new URLSearchParams({
      client_id: "psim-console",
      response_type: "code",
      scope: "openid profile",
      redirect_uri: redirect,
      state,
      code_challenge: challenge,
      code_challenge_method: "S256",
    });
    location.assign(`${issuer}/protocol/openid-connect/auth?${query.toString()}`);
  };
  complete(): Promise<void> {
    this.completing ??= this.finish();
    return this.completing;
  }
  private async finish(): Promise<void> {
    const query = new URLSearchParams(this.environment.location.search);
    if (!query.has("code")) {
      return;
    }
    const stored = this.environment.storage.getItem("psim.pkce");
    this.environment.storage.removeItem("psim.pkce");
    this.environment.cleanUrl();
    const saved: unknown = JSON.parse(stored ?? "null");
    if (
      typeof saved !== "object" ||
      saved === null ||
      !("state" in saved) ||
      saved.state !== query.get("state") ||
      !("verifier" in saved) ||
      typeof saved.verifier !== "string" ||
      !("redirect" in saved) ||
      typeof saved.redirect !== "string"
    ) {
      throw new Error("Sign-in session expired. Please try again.");
    }
    await this.exchange(
      new URLSearchParams({
        grant_type: "authorization_code",
        client_id: "psim-console",
        code: query.get("code") ?? "",
        code_verifier: saved.verifier,
        redirect_uri: saved.redirect,
      }),
    );
  }
  token = async (): Promise<string | undefined> => {
    await this.complete();
    if (this.tokens !== undefined && Date.now() >= this.expires - 30_000) {
      if (this.tokens.refresh_token === undefined) {
        this.tokens = undefined;
      } else {
        await this.exchange(
          new URLSearchParams({
            grant_type: "refresh_token",
            client_id: "psim-console",
            refresh_token: this.tokens.refresh_token,
          }),
        );
      }
    }
    return this.tokens?.access_token;
  };
}
declare global {
  interface Window {
    psimConfig?: { issuer: string };
  }
}
export const auth = new SessionAuth({
  issuer: window.psimConfig?.issuer ?? "http://localhost:8180/realms/psim",
  storage: sessionStorage,
  location: window.location,
  cleanUrl: () => {
    history.replaceState(null, "", location.pathname);
  },
  fetch: (...args) => fetch(...args),
  crypto: globalThis.crypto,
});
