import { expect, it } from "vitest";
import { contractLabels } from "./contract-labels";
it("keeps every enum label nonempty and keys identical across languages", () => {
  expect(Object.keys(contractLabels.en)).toEqual(Object.keys(contractLabels.ru));
  for (const key of Object.keys(contractLabels.en) as (keyof typeof contractLabels.en)[]) {
    // The source is the exhaustive object above.
    expect(Object.keys(contractLabels.en[key])).toEqual(Object.keys(contractLabels.ru[key]));
    for (const language of ["en", "ru"] as const)
      expect(Object.values(contractLabels[language][key]).every((value) => value.trim().length > 0)).toBe(true);
  }
});
