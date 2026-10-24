import { webcrypto } from "node:crypto";
export const issuer = "https://identity.test/realms/psim";
export const nonce = "n".repeat(43);
export const state = "s".repeat(43);
const pair = await webcrypto.subtle.generateKey(
  { name: "RSASSA-PKCS1-v1_5", modulusLength: 2048, publicExponent: new Uint8Array([1, 0, 1]), hash: "SHA-256" },
  true,
  ["sign", "verify"],
);
export const jwks = {
  keys: [
    { ...(await webcrypto.subtle.exportKey("jwk", pair.publicKey)), kid: "console-test", use: "sig", alg: "RS256" },
  ],
};
function encode(value: unknown): string {
  return Buffer.from(JSON.stringify(value)).toString("base64url");
}
export async function jwt(body: Record<string, unknown>): Promise<string> {
  const content = `${encode({ alg: "RS256", kid: "console-test" })}.${encode(body)}`;
  const signature = await webcrypto.subtle.sign(
    "RSASSA-PKCS1-v1_5",
    pair.privateKey,
    new TextEncoder().encode(content),
  );
  return content + "." + Buffer.from(signature).toString("base64url");
}
export async function tokens(
  options: {
    nonce?: string;
    roles?: readonly string[];
    ttl?: number;
    identity?: Record<string, unknown>;
    access?: Record<string, unknown>;
  } = {},
) {
  const now = Math.floor(Date.now() / 1000);
  const common = { iss: issuer, sub: "operator", iat: now, exp: now + (options.ttl ?? 300) };
  return {
    access_token: await jwt({
      ...common,
      aud: "psim",
      psim_tenant: "tenant",
      psim_scope_set: "scope",
      resource_access: { psim: { roles: options.roles ?? ["operator"] } },
      ...options.access,
    }),
    id_token: await jwt({
      ...common,
      aud: "psim-console",
      nonce: options.nonce ?? nonce,
      preferred_username: "operator",
      ...options.identity,
    }),
    refresh_token: "rotated-refresh",
    expires_in: options.ttl ?? 300,
  };
}
