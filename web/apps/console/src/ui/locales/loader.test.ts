import { expect, it } from "vitest";
import { getDemoStrings, getMessage, getStrings, loadStrings } from "./loader";
import * as productEn from "./product-en";
import * as productRu from "./product-ru";
import * as demoEn from "./demo-en";
import * as demoRu from "./demo-ru";
it("keeps product and example namespaces separate with matching nonempty language keys", async () => {
  for (const [source, translated] of [
    [productEn, productRu],
    [demoEn, demoRu],
  ] as const) {
    expect(Object.keys(translated.strings).sort()).toEqual(Object.keys(source.strings).sort());
    expect(Object.keys(translated.messages).sort()).toEqual(Object.keys(source.messages).sort());
    expect(Object.values(translated.strings).every((value) => value.trim().length > 0)).toBe(true);
  }
  expect(Object.keys(demoEn.strings).filter((key) => Object.hasOwn(productEn.strings, key))).toEqual([]);
  await loadStrings("en");
  expect(getStrings("en")).toBe(productEn.strings);
  expect(Object.hasOwn(getStrings("en"), "site")).toBe(false);
  await loadStrings("ru", true);
  expect(getDemoStrings("ru").site).toBe("Северная площадка");
  expect(getMessage("en", "incidents")).toContain("plural");
  expect(getMessage("ru", "importSummary")).toContain("plural");
});
