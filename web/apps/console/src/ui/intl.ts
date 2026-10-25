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
export { formatNumber, formatAbsolute, formatRelative, formatList, sortLabels } from "./format";
export { pseudolocalize } from "@psim/design-system/pseudolocalize";
