import { Linter } from "eslint";
import assert from "node:assert/strict";
import { test } from "node:test";
import tokensOnly from "./tokens-only.js";
const config = [{ plugins: { psim: { rules: { tokens: tokensOnly } } }, rules: { "psim/tokens": "error" } }];
for (const source of [
  'const value = "#ef4444";',
  'const value = "text-red-500";',
  "const value = `color: rgb(0,0,0)`;",
]) {
  test(`rejects literal application styling: ${source}`, () => {
    assert.equal(new Linter().verify(source, config).filter((item) => item.ruleId === "psim/tokens").length, 1);
  });
}
test("permits semantic tokens and non-color identifiers", () => {
  assert.deepEqual(
    new Linter().verify(
      'const value = "text-danger bg-card"; const fill = "var(--danger)"; const id = "INC-1042";',
      config,
    ),
    [],
  );
});
