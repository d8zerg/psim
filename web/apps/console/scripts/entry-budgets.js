import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { resolve } from "node:path";
import { gzipSync } from "node:zlib";
import process from "node:process";
const root = resolve(import.meta.dirname, "../dist");
const manifest = JSON.parse(readFileSync(resolve(root, ".vite/manifest.json"), "utf8"));
const consoleLimit = Number(process.argv[2] ?? 250000);
if (!Number.isFinite(consoleLimit) || consoleLimit <= 0) throw new Error("Invalid console budget");
const measurements = {};
for (const [entry, limit] of [
  ["index.html", consoleLimit],
  ["design-system.html", 260000],
]) {
  const keys = new Set();
  const files = new Set();
  function collect(key) {
    if (keys.has(key)) return;
    keys.add(key);
    const asset = manifest[key];
    if (!asset) throw new Error(`Missing entry asset: ${key}`);
    if (asset.file.endsWith(".js")) files.add(asset.file);
    for (const file of asset.assets ?? []) if (file.endsWith(".json")) files.add(file);
    for (const dependency of [...(asset.imports ?? []), ...(asset.dynamicImports ?? [])]) collect(dependency);
  }
  collect(entry);
  const html = readFileSync(resolve(root, entry), "utf8");
  for (const match of html.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gu)) files.add(match[1].replace(/^\//u, ""));
  const bytes = [...files].reduce(
    (sum, file) => sum + gzipSync(readFileSync(resolve(root, file)), { level: 9 }).length,
    0,
  );
  if (!bytes) throw new Error("Empty entry graph");
  measurements[entry] = { bytes, limit, files: [...files].sort() };
  console.log(`Complete JavaScript graph ${entry}: ${bytes} bytes gzip; limit ${limit}`);
  if (bytes > limit) process.exitCode = 1;
}
const artifacts = resolve(import.meta.dirname, "../../../../build");
mkdirSync(artifacts, { recursive: true });
writeFileSync(resolve(artifacts, "shell-assets.json"), JSON.stringify(measurements, null, 2) + "\n");
