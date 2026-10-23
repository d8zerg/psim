import { readdirSync, readFileSync } from "node:fs";
import { resolve } from "node:path";
import process from "node:process";
function check(directory) {
  for (const item of readdirSync(directory, { withFileTypes: true })) {
    const path = resolve(directory, item.name);
    if (item.isDirectory()) check(path);
    else if (item.name.endsWith(".css")) {
      const text = readFileSync(path, "utf8").replace(/\/\*[\s\S]*?\*\//gu, "");
      if (
        /(?:#[\da-f]{3,8}\b|(?:rgb|hsl|oklch|oklab)\s*\(|(?:font-size|border-radius|padding|gap|z-index)\s*:\s*\d)/iu.test(
          text,
        )
      ) {
        console.error(`Application styles must use shared tokens: ${path}`);
        process.exitCode = 1;
      }
    }
  }
}
check(resolve(import.meta.dirname, "../src"));
