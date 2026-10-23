import type { strings as productStrings, messages as productMessages } from "./product-en";
import type { strings as demoStrings, messages as demoMessages } from "./demo-en";
export type Locale = "ru" | "en";
export type ProductStrings = typeof productStrings;
export type DemoStrings = ProductStrings & typeof demoStrings;
interface ProductDictionary {
  readonly strings: ProductStrings;
  readonly messages: typeof productMessages;
}
interface DemoDictionary {
  readonly strings: typeof demoStrings;
  readonly messages: typeof demoMessages;
}
const productImports = { en: () => import("./product-en"), ru: () => import("./product-ru") };
const demoImports = { en: () => import("./demo-en"), ru: () => import("./demo-ru") };
const products = new Map<Locale, ProductDictionary>();
const demos = new Map<Locale, DemoDictionary>();
const previews = new Map<Locale, DemoStrings>();
/** The selected locale is loaded before the first render or an atomic language change. */
export async function loadStrings(locale: Locale, design = false): Promise<void> {
  const [product, demo] = await Promise.all([productImports[locale](), design ? demoImports[locale]() : undefined]);
  products.set(locale, product);
  if (demo) {
    demos.set(locale, demo);
    previews.set(locale, { ...product.strings, ...demo.strings });
  }
}
export function getStrings(locale: Locale): ProductStrings {
  const dictionary = products.get(locale);
  if (!dictionary) throw new Error(`Locale is not loaded: ${locale}`);
  return dictionary.strings;
}
export function getDemoStrings(locale: Locale): DemoStrings {
  const dictionary = previews.get(locale);
  if (!dictionary) throw new Error(`Preview locale is not loaded: ${locale}`);
  return dictionary;
}
export function getMessage(locale: Locale, key: keyof typeof productMessages | keyof typeof demoMessages): string {
  const value = key === "importSummary" ? demos.get(locale)?.messages[key] : products.get(locale)?.messages[key];
  if (!value) throw new Error(`Message locale is not loaded: ${locale}`);
  return value;
}
