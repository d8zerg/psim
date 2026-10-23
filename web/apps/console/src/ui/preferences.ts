import { presentation, type Theme, type Density, type Mode } from "@psim/design-system/tokens";
import { create } from "zustand";
export interface Preferences {
  readonly theme: Theme;
  readonly density: Density;
  readonly mode: Mode;
  readonly language: "ru" | "en";
  readonly sound: boolean;
}
export const defaults: Preferences = {
  theme: "dark",
  density: "comfortable",
  language: "ru",
  sound: false,
  mode: "normal",
};
export function isTheme(value: unknown): value is Theme {
  return typeof value === "string" && Object.hasOwn(presentation.themes, value);
}
export function isDensity(value: unknown): value is Density {
  return presentation.densities.some((choice) => choice === value);
}
export function isMode(value: unknown): value is Mode {
  return presentation.modes.some((choice) => choice === value);
}
export function readPreferences(): Preferences {
  try {
    const value: unknown = JSON.parse(localStorage.getItem("psim.ui") ?? "{}");
    if (typeof value !== "object" || value === null) return defaults;
    return {
      theme: "theme" in value && isTheme(value.theme) ? value.theme : "dark",
      density: "density" in value && isDensity(value.density) ? value.density : "comfortable",
      mode: "mode" in value && isMode(value.mode) ? value.mode : "normal",
      language: "language" in value && value.language === "en" ? "en" : "ru",
      sound: "sound" in value && value.sound === true,
    };
  } catch {
    return defaults;
  }
}
export function applyPreferences(value: Preferences): void {
  document.documentElement.dataset.theme = value.theme;
  document.documentElement.dataset.density = value.density;
  document.documentElement.dataset.mode = value.mode;
  document.documentElement.lang = value.language;
  document.documentElement.style.colorScheme = presentation.themes[value.theme].colorScheme;
  document.documentElement.style.backgroundColor = presentation.themes[value.theme].background;
}
export const usePreferences = create<Preferences>(() => readPreferences());
export function setPreferences(update: Partial<Preferences>): void {
  usePreferences.setState(update);
  const value = usePreferences.getState();
  applyPreferences(value);
  try {
    localStorage.setItem("psim.ui", JSON.stringify(value));
  } catch {
    /* Preferences remain available for this session. */
  }
}
