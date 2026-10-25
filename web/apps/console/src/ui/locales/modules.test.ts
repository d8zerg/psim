import { afterEach, expect, it, vi } from "vitest";
import { loadModuleLocales, moduleDictionary, moduleWithLocale, readDictionary } from "./modules";
import { setPreferences } from "../preferences";
import adminEn from "../../admin/locales/en.json";
import adminRu from "../../admin/locales/ru.json";
import insightsEn from "../../insights/locales/en.json";
import insightsRu from "../../insights/locales/ru.json";
import type { Dictionary } from "../../admin/strings";
import type { Strings } from "../../insights/strings";
afterEach(() => {
  vi.unstubAllGlobals();
});
it("checks complete matching language structures at compilation and runtime", () => {
  const admin: Dictionary = adminRu,
    insights: Strings = insightsRu;
  expect(Object.keys(insights).sort()).toEqual(Object.keys(insightsEn).sort());
  for (const key of ["strings", "fields", "enums"] as const)
    expect(Object.keys(admin[key]).sort()).toEqual(Object.keys(adminEn[key]).sort());
});
it("coalesces locale requests and does not expose a module before its selected dictionary is ready", async () => {
  const en = vi.fn(() => Promise.resolve({ default: { name: "English" } }));
  const read = moduleDictionary({ en, ru: () => Promise.resolve({ default: { name: "Русский" } }) });
  expect(() => read("en")).toThrow("Module locale is not loaded");
  setPreferences({ language: "en" });
  await Promise.all([moduleWithLocale(() => Promise.resolve("module")), loadModuleLocales("en")]);
  expect(read("en").name).toBe("English");
  expect(en).toHaveBeenCalledTimes(1);
  await loadModuleLocales("ru");
  expect(read("ru").name).toBe("Русский");
});
it("retries a failed public dictionary without credentials and rejects invalid files", async () => {
  const fetcher = vi
    .fn<typeof fetch>()
    .mockResolvedValueOnce(new Response("unavailable", { status: 503 }))
    .mockResolvedValueOnce(Response.json(["wrong"]))
    .mockResolvedValueOnce(Response.json({ name: "Recovered" }));
  vi.stubGlobal("fetch", fetcher);
  const read = moduleDictionary({
    en: () => readDictionary<{ name: string }>(new URL("https://assets.test/en.json")),
    ru: () => Promise.resolve({ default: { name: "Русский" } }),
  });
  await expect(loadModuleLocales("en")).rejects.toThrow("503");
  await expect(loadModuleLocales("en")).rejects.toThrow("Invalid dictionary");
  await loadModuleLocales("en");
  expect(read("en").name).toBe("Recovered");
  expect(fetcher.mock.calls[0]?.[1]?.credentials).toBe("omit");
});
it("waits for the new language when preferences change during module loading", async () => {
  let finish: () => void = () => {
    throw new Error("Dictionary was not requested");
  };
  const en = vi.fn(
    () =>
      new Promise<{ default: { name: string } }>((resolve) => {
        finish = () => {
          resolve({ default: { name: "English" } });
        };
      }),
  );
  const ru = vi.fn(() => Promise.resolve({ default: { name: "Русский" } }));
  const read = moduleDictionary({ en, ru });
  setPreferences({ language: "en" });
  const ready = moduleWithLocale(() => Promise.resolve("module"));
  await vi.waitFor(() => {
    expect(en).toHaveBeenCalledTimes(1);
  });
  setPreferences({ language: "ru" });
  finish();
  expect(await ready).toBe("module");
  expect(read("ru").name).toBe("Русский");
  expect(ru).toHaveBeenCalledTimes(1);
});
