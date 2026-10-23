import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
const root = resolve(import.meta.dirname, "..");
const meta = JSON.parse(readFileSync(resolve(root, "dist/meta.json"), "utf8"));
const represented = new Set(Object.values(meta.stories).flatMap((story) => story.meta.components ?? []));
for (const file of readdirSync(resolve(root, "src/components"))) {
  if (!file.endsWith(".tsx") || file.endsWith(".test.tsx")) continue;
  for (const match of readFileSync(resolve(root, "src/components", file), "utf8").matchAll(
    /export function ([A-Z]\w*)/gu,
  )) {
    if (!represented.has(match[1])) throw new Error(`Missing component story: ${match[1]}`);
  }
}
console.log(`Catalogue: ${Object.keys(meta.stories).length} stories, ${represented.size} component names`);
