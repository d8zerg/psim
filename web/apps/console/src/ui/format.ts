import type { Locale } from "./locales/loader";
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
