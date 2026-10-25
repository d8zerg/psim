import { usePreferences } from "../preferences";
import type { Locale } from "./loader";
const active = new Set<(locale: Locale) => Promise<unknown>>();
export function moduleDictionary<T>(imports: Record<Locale, () => Promise<{ default: T }>>) {
  const values = new Map<Locale, T>();
  const pending = new Map<Locale, Promise<T>>();
  function load(locale: Locale): Promise<T> {
    const existing = pending.get(locale);
    if (existing) return existing;
    const request = imports[locale]()
      .then((module) => {
        values.set(locale, module.default);
        return module.default;
      })
      .catch((error: unknown) => {
        pending.delete(locale);
        throw error;
      });
    pending.set(locale, request);
    return request;
  }
  active.add(load);
  return (locale: Locale): T => {
    const value = values.get(locale);
    if (!value) throw new Error(`Module locale is not loaded: ${locale}`);
    return value;
  };
}
export async function loadModuleLocales(locale: Locale): Promise<void> {
  await Promise.all([...active].map((load) => load(locale)));
}
export async function moduleWithLocale<T>(importer: () => Promise<T>): Promise<T> {
  const module = await importer();
  for (;;) {
    const locale = usePreferences.getState().language;
    await loadModuleLocales(locale);
    if (locale === usePreferences.getState().language) return module;
  }
}

// eslint-disable-next-line @typescript-eslint/no-unnecessary-type-parameters -- static dictionaries are checked against their module contracts
export async function readDictionary<T>(url: URL): Promise<{ default: T }> {
  const response = await fetch(url, { credentials: "omit" });
  if (!response.ok) throw new Error(`Dictionary unavailable: ${String(response.status)}`);
  const value: unknown = await response.json();
  if (value === null || typeof value !== "object" || Array.isArray(value)) throw new Error("Invalid dictionary");
  return { default: value as T };
}
