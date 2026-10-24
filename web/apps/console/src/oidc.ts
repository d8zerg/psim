export class SessionError extends Error {
  constructor(readonly terminal = true) {
    super(terminal ? "Sign-in failed. Please sign in again." : "Identity provider unavailable.");
  }
}
export function object(value: unknown): Record<string, unknown> {
  if (typeof value !== "object" || value === null || Array.isArray(value)) throw new SessionError();
  return value as Record<string, unknown>;
}
function bytes(text: string): Uint8Array<ArrayBuffer> {
  return Uint8Array.from(atob(text.replaceAll("-", "+").replaceAll("_", "/")), (char) => char.charCodeAt(0));
}
export function claims(token: string): Record<string, unknown> {
  return object(JSON.parse(new TextDecoder().decode(bytes(token.split(".")[1] ?? ""))));
}
export async function validateIdentity(
  token: string,
  issuer: string,
  nonce: string,
  crypto: Crypto,
  fetch: typeof globalThis.fetch,
  refreshing: boolean,
): Promise<Record<string, unknown>> {
  const parts = token.split(".");
  if (parts.length !== 3) throw new SessionError();
  const header = object(JSON.parse(new TextDecoder().decode(bytes(parts[0] ?? ""))));
  const body = claims(token);
  const now = Date.now() / 1000;
  const audience = body.aud;
  if (
    header.alg !== "RS256" ||
    typeof header.kid !== "string" ||
    body.iss !== issuer ||
    !(
      audience === "psim-console" ||
      (Array.isArray(audience) && audience.includes("psim-console") && body.azp === "psim-console")
    ) ||
    (body.azp !== undefined && body.azp !== "psim-console") ||
    typeof body.sub !== "string" ||
    !body.sub ||
    typeof body.exp !== "number" ||
    body.exp <= now ||
    typeof body.iat !== "number" ||
    body.iat > now + 30 ||
    (!refreshing && body.nonce !== nonce) ||
    (body.nonce !== undefined && body.nonce !== nonce)
  )
    throw new SessionError();
  const response = await fetch(`${issuer}/protocol/openid-connect/certs`, {
    signal: AbortSignal.timeout(10000),
    credentials: "omit",
    cache: "no-store",
  });
  if (!response.ok) throw new SessionError(response.status < 500 && response.status !== 429);
  const keys = object(await response.json()).keys;
  if (!Array.isArray(keys)) throw new SessionError();
  const candidates = keys
    .map((key: unknown) => object(key))
    .filter(
      (key) =>
        key.kid === header.kid &&
        key.kty === "RSA" &&
        (key.use === undefined || key.use === "sig") &&
        (key.alg === undefined || key.alg === "RS256"),
    );
  if (candidates.length !== 1) throw new SessionError();
  const key = await crypto.subtle.importKey(
    "jwk",
    candidates[0] as JsonWebKey,
    { name: "RSASSA-PKCS1-v1_5", hash: "SHA-256" },
    false,
    ["verify"],
  );
  if (
    !(await crypto.subtle.verify(
      "RSASSA-PKCS1-v1_5",
      key,
      bytes(parts[2] ?? ""),
      new TextEncoder().encode(`${parts[0] ?? ""}.${parts[1] ?? ""}`),
    ))
  )
    throw new SessionError();
  return body;
}
