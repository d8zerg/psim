import type { SessionProfile } from "../auth";
export const modules = [
  { path: "/", key: "console", roles: ["operator", "supervisor"] },
  { path: "/admin", key: "administration", roles: ["admin"] },
  { path: "/integrations", key: "integrations", roles: ["admin", "integrator"] },
  { path: "/audit", key: "audit", roles: ["auditor"] },
  { path: "/reports", key: "reports", roles: ["manager", "supervisor"] },
] as const;
export type Module = (typeof modules)[number];
export function allowedModules(profile?: SessionProfile): readonly Module[] {
  return profile ? modules.filter((module) => module.roles.some((role) => profile.roles.includes(role))) : [];
}
