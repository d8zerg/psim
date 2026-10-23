import { readFileSync } from "node:fs";
import { resolve } from "node:path";
import process from "node:process";
import { gzipSync } from "node:zlib";
const root = resolve(import.meta.dirname, "../dist");
const manifest = JSON.parse(readFileSync(resolve(root, ".vite/manifest.json"), "utf8"));
const files = new Set();
function collect(key, target = files) {
  const entry = manifest[key];
  if (!entry) throw new Error(`Missing initial asset: ${key}`);
  if (target.has(entry.file)) return;
  target.add(entry.file);
  for (const dependency of entry.imports ?? []) collect(dependency, target);
}
collect("index.html");
const html = readFileSync(resolve(root, "index.html"), "utf8");
for (const match of html.matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gu)) {
  const file = match[1].replace(/^\//u, "");
  if (file.endsWith(".js")) files.add(file);
}
const limit = Number(process.argv[2] ?? 150000);
if (files.size === 0 || !Number.isFinite(limit) || limit <= 0) throw new Error("Invalid shell budget or missing entry");
for (const locale of ["en", "ru"]) {
  const initial = new Set(files);
  collect(`src/ui/locales/product-${locale}.ts`, initial);
  let bytes = 0;
  for (const file of initial) bytes += gzipSync(readFileSync(resolve(root, file))).length;
  console.log(`Initial console JavaScript (${locale}, including dictionary): ${bytes} bytes gzip; limit ${limit}`);
  if (bytes > limit) process.exitCode = 1;
}
