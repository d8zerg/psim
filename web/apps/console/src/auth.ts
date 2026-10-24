import { claims, object, SessionError, validateIdentity } from "./oidc";
export { SessionError } from "./oidc";
export interface SessionTokens {
  readonly access_token: string;
  readonly expires_in: number;
  readonly refresh_token?: string;
}
export interface SessionProfile {
  readonly subject: string;
  readonly name: string;
  readonly roles: readonly string[];
  readonly context: string;
}
export interface SessionState {
  readonly status: "checking" | "anonymous" | "authenticated";
  readonly profile?: SessionProfile;
  readonly error: boolean;
}
export interface AuthEnvironment {
  readonly issuer: string;
  readonly storage: Storage;
  readonly location: Pick<Location, "origin" | "pathname" | "search" | "assign">;
  readonly cleanUrl: (path?: string) => void;
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
  private nonce = "";
  private epoch = 0;
  private completing: Promise<void> | undefined;
  private refreshing: Promise<void> | undefined;
  private state: SessionState = { status: "checking", error: false };
  private readonly listeners = new Set<() => void>();
  constructor(private readonly environment: AuthEnvironment) {}
  getSnapshot = (): SessionState => this.state;
  subscribe = (listener: () => void): (() => void) => {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  };
  private update(state: SessionState): void {
    this.state = state;
    this.listeners.forEach((listener) => {
      listener();
    });
  }
  invalidate = (error = true): void => {
    this.epoch += 1;
    this.tokens = undefined;
    this.environment.storage.removeItem("psim.pkce");
    this.update({ status: "anonymous", error });
  };
  private async exchange(form: URLSearchParams, refreshing = false): Promise<void> {
    const epoch = this.epoch;
    try {
      const response = await this.environment.fetch(`${this.environment.issuer}/protocol/openid-connect/token`, {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded" },
        body: form,
        credentials: "omit",
        cache: "no-store",
        signal: AbortSignal.timeout(10000),
      });
      if (!response.ok) throw new SessionError(response.status < 500 && response.status !== 429);
      const body = object(await response.json());
      if (
        typeof body.access_token !== "string" ||
        typeof body.expires_in !== "number" ||
        !Number.isFinite(body.expires_in) ||
        body.expires_in <= 0 ||
        (!refreshing && typeof body.id_token !== "string")
      )
        throw new SessionError();
      const identity =
        typeof body.id_token === "string"
          ? await validateIdentity(
              body.id_token,
              this.environment.issuer,
              this.nonce,
              this.environment.crypto,
              this.environment.fetch,
              refreshing,
            )
          : undefined;
      const access = claims(body.access_token);
      const subject = identity?.sub ?? this.state.profile?.subject;
      if (
        typeof subject !== "string" ||
        access.sub !== subject ||
        access.iss !== this.environment.issuer ||
        !(access.aud === "psim" || (Array.isArray(access.aud) && access.aud.includes("psim"))) ||
        typeof access.exp !== "number" ||
        access.exp <= Date.now() / 1000 ||
        (refreshing && subject !== this.state.profile?.subject)
      )
        throw new SessionError();
      const resources =
        typeof access.resource_access === "object" && access.resource_access !== null
          ? object(access.resource_access)
          : {};
      const psim = resources.psim === undefined ? {} : object(resources.psim);
      const roles = Array.isArray(psim.roles)
        ? psim.roles.filter((role): role is string => typeof role === "string").sort()
        : [];
      const profile: SessionProfile = {
        subject,
        name:
          typeof identity?.preferred_username === "string"
            ? identity.preferred_username
            : (this.state.profile?.name ?? subject),
        roles,
        context: JSON.stringify([subject, access.psim_tenant, access.psim_scope_set, roles]),
      };
      if (epoch !== this.epoch) return;
      this.tokens = {
        access_token: body.access_token,
        expires_in: body.expires_in,
        ...(typeof body.refresh_token === "string"
          ? { refresh_token: body.refresh_token }
          : refreshing && this.tokens?.refresh_token
            ? { refresh_token: this.tokens.refresh_token }
            : {}),
      };
      this.expires = Math.min(Date.now() + body.expires_in * 1000, access.exp * 1000);
      this.update({ status: "authenticated", profile, error: false });
    } catch (error) {
      if (epoch !== this.epoch) return;
      if (
        (error instanceof SessionError && !error.terminal) ||
        error instanceof TypeError ||
        (error instanceof DOMException && error.name === "TimeoutError")
      )
        throw new SessionError(false);
      this.invalidate();
      throw new SessionError();
    }
  }
  login = async (): Promise<void> => {
    const { crypto, location, storage, issuer } = this.environment;
    const verifier = base64(crypto.getRandomValues(new Uint8Array(32)));
    const state = base64(crypto.getRandomValues(new Uint8Array(32)));
    const nonce = base64(crypto.getRandomValues(new Uint8Array(32)));
    const challenge = base64(new Uint8Array(await crypto.subtle.digest("SHA-256", new TextEncoder().encode(verifier))));
    const redirect = location.origin + location.pathname;
    storage.setItem(
      "psim.pkce",
      JSON.stringify({
        verifier,
        state,
        nonce,
        redirect,
        returnTo: location.pathname + location.search,
        issuer,
        issued: Date.now(),
      }),
    );
    location.assign(
      `${issuer}/protocol/openid-connect/auth?${new URLSearchParams({ client_id: "psim-console", response_type: "code", scope: "openid profile", redirect_uri: redirect, state, nonce, code_challenge: challenge, code_challenge_method: "S256" }).toString()}`,
    );
  };
  complete(): Promise<void> {
    this.completing ??= this.finish();
    return this.completing;
  }
  private async finish(): Promise<void> {
    const { location, storage, issuer } = this.environment;
    const query = new URLSearchParams(location.search);
    if (!query.has("code") && !query.has("error")) {
      this.update({ status: "anonymous", error: false });
      return;
    }
    const stored = storage.getItem("psim.pkce");
    storage.removeItem("psim.pkce");
    this.environment.cleanUrl();
    try {
      const saved = object(JSON.parse(stored ?? "null"));
      if (
        query.has("error") ||
        query.getAll("code").length !== 1 ||
        query.getAll("state").length !== 1 ||
        saved.state !== query.get("state") ||
        typeof saved.verifier !== "string" ||
        typeof saved.nonce !== "string" ||
        saved.issuer !== issuer ||
        saved.redirect !== location.origin + location.pathname ||
        typeof saved.issued !== "number" ||
        Date.now() - saved.issued > 600000 ||
        saved.issued > Date.now() ||
        (query.has("iss") && query.get("iss") !== issuer)
      )
        throw new SessionError();
      this.nonce = saved.nonce;
      if (
        typeof saved.returnTo === "string" &&
        saved.returnTo.startsWith("/") &&
        !saved.returnTo.startsWith("//") &&
        new URL(saved.returnTo, location.origin).origin === location.origin
      )
        this.environment.cleanUrl(saved.returnTo);
      await this.exchange(
        new URLSearchParams({
          grant_type: "authorization_code",
          client_id: "psim-console",
          code: query.get("code") ?? "",
          code_verifier: saved.verifier,
          redirect_uri: saved.redirect,
        }),
      );
    } catch {
      this.invalidate();
      throw new SessionError();
    }
  }
  token = async (): Promise<string | undefined> => {
    await this.complete();
    if (this.tokens !== undefined && Date.now() >= this.expires - 30000) {
      if (!this.tokens.refresh_token) this.invalidate();
      else {
        this.refreshing ??= this.exchange(
          new URLSearchParams({
            grant_type: "refresh_token",
            client_id: "psim-console",
            refresh_token: this.tokens.refresh_token,
          }),
          true,
        ).finally(() => {
          this.refreshing = undefined;
        });
        await this.refreshing;
      }
    }
    return this.tokens?.access_token;
  };
  logout = async (): Promise<void> => {
    const refresh = this.tokens?.refresh_token;
    this.invalidate(false);
    if (refresh) {
      try {
        const response = await this.environment.fetch(`${this.environment.issuer}/protocol/openid-connect/logout`, {
          method: "POST",
          credentials: "omit",
          signal: AbortSignal.timeout(10000),
          headers: { "Content-Type": "application/x-www-form-urlencoded" },
          body: new URLSearchParams({ client_id: "psim-console", refresh_token: refresh }),
        });
        if (!response.ok) throw new SessionError(false);
      } catch {
        this.update({ status: "anonymous", error: true });
      }
    }
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
  cleanUrl: (path) => {
    history.replaceState(null, "", path ?? location.pathname);
  },
  fetch: (...args) => fetch(...args),
  crypto: globalThis.crypto,
});
