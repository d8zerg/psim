import { usePreferences } from "../ui/preferences";
import en from "./locales/en.json";
import ru from "./locales/ru.json";
export interface Dictionary {
  readonly strings: { [K in keyof typeof en.strings]: string };
  readonly fields: Readonly<Record<string, string>>;
  readonly enums: Readonly<Record<string, string>>;
}
const read = (locale: "en" | "ru"): Dictionary => (locale === "en" ? en : ru);
export function label(key: string, language: "ru" | "en"): string {
  return read(language).fields[key] ?? key.replace(/([a-z])([A-Z])/gu, "$1 $2");
}
export function enumLabel(value: string, language: "ru" | "en") {
  const tail = value.replace(
    /^(?:LOCATION_KIND|RESOURCE_STATE|CORRELATION_SCOPE|RULE_STATE|PLAN_STATE|MAINTENANCE_STATE|DRY_RUN_STATE|IMPORT_STATE|SEVERITY|PRIORITY|NOTIFICATION_CHANNEL|PARAMETER_TYPE)_/u,
    "",
  );
  return read(language).enums[tail] ?? tail;
}
export function useAdminStrings() {
  const language = usePreferences((state) => state.language);
  return { t: read(language).strings, language };
}
