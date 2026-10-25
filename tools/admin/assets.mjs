import { readFileSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { gzipSync } from "node:zlib";
const result = {};
for (const name of ["lazy", "eager"]) {
  const root = resolve(import.meta.dirname, "../../build/admin-assets", name);
  const manifest = JSON.parse(readFileSync(resolve(root, ".vite/manifest.json"), "utf8"));
  function graph(entry, dynamic = true, files = new Set(), visited = new Set()) {
    if (visited.has(entry)) return files;
    visited.add(entry);
    const asset = manifest[entry];
    if (!asset) throw new Error(`Missing ${entry}`);
    if (asset.file.endsWith(".js")) files.add(asset.file);
    for (const key of [...(asset.imports ?? []), ...(dynamic ? (asset.dynamicImports ?? []) : [])]) graph(key, dynamic, files, visited);
    return files;
  }
  const count = (files) => [...files].reduce((sum, file) => sum + gzipSync(readFileSync(resolve(root, file)), { level: 9 }).length, 0);
  const scripts = (entry) => [...readFileSync(resolve(root, entry), "utf8").matchAll(/<script\b[^>]*\bsrc="([^"]+)"/gu)].map((match) => match[1].replace(/^\//u, ""));
  const app = new Set([...graph("index.html"), ...scripts("index.html")]);
  const design = new Set([...graph("design-system.html"), ...scripts("design-system.html")]);
  const initial = {};
  for (const locale of ["en", "ru"]) initial[locale] = count(new Set([...graph("index.html", false), ...graph(`src/ui/locales/product-${locale}.ts`, false), ...scripts("index.html")]));
  result[name] = { initial, application: count(app), design: count(design), productionOnly: count(app), uniqueDelivery: count(new Set([...app, ...design])), applicationFiles: [...app].sort() };
}
writeFileSync(resolve(import.meta.dirname, "../../build/admin-assets/comparison.json"), JSON.stringify(result, null, 2) + "\n");
console.log(JSON.stringify(result, null, 2));
