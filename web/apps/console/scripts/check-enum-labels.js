import { readFileSync, readdirSync } from "node:fs";
import { resolve } from "node:path";
const root = resolve(import.meta.dirname, "..");
const directory = resolve(root, "../../packages/realtime-client/src/gen/psim");
const labels = readFileSync(resolve(root, "src/ui/contract-labels.ts"), "utf8");
let count = 0;
for (const file of readdirSync(directory, { recursive: true })) {
  if (!file.endsWith(".ts")) continue;
  for (const match of readFileSync(resolve(directory, file), "utf8").matchAll(/export enum (\w+) \{/gu)) {
    const name = match[1];
    if ([...labels.matchAll(new RegExp(`satisfies Record<${name}, string>`, "gu"))].length !== 2)
      throw new Error(`Missing localized contract enum: ${name}`);
    count++;
  }
}
if (!count) throw new Error("No contract enums found");
console.log(`Contract labels: ${count} enums, two languages; TypeScript validates every value`);
