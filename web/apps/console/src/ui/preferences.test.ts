import { afterEach, expect, it, vi } from "vitest";
import { applyPreferences, defaults, readPreferences, setPreferences, usePreferences } from "./preferences";
afterEach(() => {
  vi.restoreAllMocks();
  localStorage.clear();
  setPreferences(defaults);
});
it("validates stored fields independently and falls back on malformed or inaccessible storage", () => {
  for (const value of ["null", "[]", "1", "{"]) {
    localStorage.setItem("psim.ui", value);
    expect(readPreferences()).toEqual(defaults);
  }
  localStorage.setItem("psim.ui", JSON.stringify({ theme: "light", density: "compact", language: "en", sound: true }));
  expect(readPreferences()).toEqual({
    theme: "light",
    density: "compact",
    language: "en",
    sound: true,
    mode: "normal",
  });
  vi.spyOn(Storage.prototype, "getItem").mockImplementation(() => {
    throw new Error("blocked");
  });
  expect(readPreferences()).toEqual(defaults);
});
it("applies presentation synchronously and keeps session preferences when persistence fails", () => {
  vi.spyOn(Storage.prototype, "setItem").mockImplementation(() => {
    throw new Error("blocked");
  });
  setPreferences({ theme: "light", density: "compact", language: "en", sound: true });
  expect(document.documentElement.dataset.theme).toBe("light");
  expect(document.documentElement.lang).toBe("en");
  expect(usePreferences.getState().density).toBe("compact");
  applyPreferences(defaults);
  expect(document.documentElement.style.colorScheme).toBe("dark");
});

it("restores all themes, operating modes and densities without accepting unknown storage values", () => {
  for (const theme of ["dark", "light", "contrast", "night"] as const) {
    for (const mode of ["normal", "crisis", "training", "wall"] as const) {
      localStorage.setItem("psim.ui", JSON.stringify({ theme, mode, density: "wall" }));
      const preferences = readPreferences();
      expect(preferences).toMatchObject({ theme, mode, density: "wall" });
      applyPreferences(preferences);
      expect(document.documentElement.dataset.mode).toBe(mode);
      expect(document.documentElement.style.colorScheme).toBe(theme === "light" ? "light" : "dark");
    }
  }
  localStorage.setItem("psim.ui", JSON.stringify({ theme: "__proto__", mode: "other", density: "other" }));
  expect(readPreferences()).toEqual(defaults);
});
