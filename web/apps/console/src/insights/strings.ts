import { usePreferences } from "../ui/preferences";
import en from "./locales/en.json";
import ru from "./locales/ru.json";
export type Strings = { [K in keyof typeof en]: string };
const read = (locale: "en" | "ru"): Strings => (locale === "en" ? en : ru);
export function useInsightsStrings() {
  const language = usePreferences((state) => state.language);
  return { t: read(language), language };
}
