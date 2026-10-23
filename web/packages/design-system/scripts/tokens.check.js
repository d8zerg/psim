import { readFileSync, mkdtempSync, writeFileSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
import process from "node:process";
import { test } from "node:test";
import assert from "node:assert/strict";
const root = resolve(import.meta.dirname, "..");
for (const [name, change, expected] of [
  [
    "rejects invalid unused palette colors",
    (tokens) => {
      tokens.base.palette.red["01"].$value.components[2] = 999;
    },
    /Invalid color range/,
  ],
  [
    "rejects insufficient text contrast",
    (tokens) => {
      tokens.semantic.dark.foreground.$value = tokens.semantic.dark.background.$value;
    },
    /foreground\/background/,
  ],
  [
    "rejects cyclic aliases",
    (tokens) => {
      tokens.semantic.dark.foreground.$value = "{semantic.dark.foreground}";
    },
    /Cyclic token/,
  ],
  [
    "rejects missing palette references",
    (tokens) => {
      tokens.semantic.dark.foreground.$value = "{base.color.absent}";
    },
    /Invalid token/,
  ],
  [
    "rejects mismatched alias types",
    (tokens) => {
      tokens.component.button.background.$value = "{base.radius.2}";
    },
    /Token type mismatch/,
  ],
  [
    "rejects incomplete component themes",
    (tokens) => {
      delete tokens.semantic.night.primary;
    },
    /Invalid token/,
  ],
  [
    "rejects easing outside the time domain",
    (tokens) => {
      tokens.base.easing.standard.$value[0] = 2;
    },
    /Invalid easing/,
  ],
  [
    "rejects invalid shadow dimensions",
    (tokens) => {
      tokens.base.shadow["3"].$value.blur.unit = "rem";
    },
    /Invalid unit/,
  ],
]) {
  test(name, () => {
    const directory = mkdtempSync(resolve(tmpdir(), "psim-tokens-"));
    try {
      const tokens = JSON.parse(readFileSync(resolve(root, "tokens/foundation.tokens.json"), "utf8"));
      change(tokens);
      const file = resolve(directory, "tokens.json");
      writeFileSync(file, JSON.stringify(tokens));
      const result = spawnSync(process.execPath, [resolve(root, "scripts/tokens.js"), "--source", file, "--check"], {
        encoding: "utf8",
      });
      assert.equal(result.status, 1);
      assert.match(result.stderr, expected);
    } finally {
      rmSync(directory, { recursive: true, force: true });
    }
  });
}
