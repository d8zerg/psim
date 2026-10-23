import IntlMessageFormat from "intl-messageformat";
import { getMessage, type Locale } from "./locales/loader";
export type { Locale } from "./locales/loader";
interface Arguments {
  readonly incidents: { readonly count: number };
  readonly minutes: { readonly count: number };
  readonly importSummary: { readonly total: number; readonly accepted: number; readonly rejected: number };
}
const cache = new Map<string, IntlMessageFormat>();
/** Only known keys and their numeric argument contracts reach the ICU formatter. */
export function message<K extends keyof Arguments>(locale: Locale, key: K, values: Arguments[K]): string {
  const cacheKey = `${locale}:${key}`;
  let formatter = cache.get(cacheKey);
  if (!formatter) {
    formatter = new IntlMessageFormat(getMessage(locale, key), locale);
    cache.set(cacheKey, formatter);
  }
  const result = formatter.format(values);
  return typeof result === "string" ? result : String(result);
}
export function formatNumber(locale: Locale, value: number, options?: Intl.NumberFormatOptions): string {
  return new Intl.NumberFormat(locale, options).format(value);
}
export function formatAbsolute(locale: Locale, value: number, timeZone: string): string {
  return new Intl.DateTimeFormat(locale, { timeZone, dateStyle: "medium", timeStyle: "medium" }).format(value);
}
export function formatRelative(locale: Locale, value: number, now: number): string {
  const seconds = (value - now) / 1000;
  const unit =
    Math.abs(seconds) < 60
      ? "second"
      : Math.abs(seconds) < 3600
        ? "minute"
        : Math.abs(seconds) < 86400
          ? "hour"
          : "day";
  const divisor = unit === "second" ? 1 : unit === "minute" ? 60 : unit === "hour" ? 3600 : 86400;
  return new Intl.RelativeTimeFormat(locale, { numeric: "auto" }).format(Math.round(seconds / divisor), unit);
}
export function formatList(locale: Locale, items: readonly string[]): string {
  return new Intl.ListFormat(locale, { style: "long", type: "conjunction" }).format(items);
}
export function sortLabels(locale: Locale, items: readonly string[]): string[] {
  const collator = new Intl.Collator(locale, { numeric: true, sensitivity: "base" });
  return [...items].sort(collator.compare);
}
export { pseudolocalize } from "@psim/design-system/pseudolocalize";
