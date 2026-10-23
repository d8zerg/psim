import { usePreferences } from "./preferences";
import { getStrings, getDemoStrings } from "./locales/loader";
export function useStrings() {
  return getStrings(usePreferences((state) => state.language));
}
export function useDemoStrings() {
  return getDemoStrings(usePreferences((state) => state.language));
}
export type { DemoStrings as Strings } from "./locales/loader";
