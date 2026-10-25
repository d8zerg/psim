import { createHash } from "node:crypto";
import { readFileSync, readdirSync, writeFileSync } from "node:fs";
import { resolve } from "node:path";
import { gzipSync } from "node:zlib";
const output = resolve(import.meta.dirname, "../../build/web-quality");
const variants = JSON.parse(
  readFileSync(resolve(output, "variants.json"), "utf8"),
);
const loading = JSON.parse(
  readFileSync(resolve(output, "loading.json"), "utf8"),
);
if (!loading.passed)
  throw new Error("Browser loading qualification did not pass");
const result = { passed: true, variants: {} };
for (const variant of variants) {
  const root = variant.directory,
    manifest = JSON.parse(
      readFileSync(resolve(root, ".vite/manifest.json"), "utf8"),
    );
  const count = (files, level = 9) =>
    [...files].reduce(
      (sum, file) =>
        sum + gzipSync(readFileSync(resolve(root, file)), { level }).length,
      0,
    );
  function graph(key, dynamic = true, seen = new Set(), files = new Set()) {
    if (seen.has(key)) return files;
    seen.add(key);
    const entry = manifest[key];
    if (!entry) throw new Error(`Missing graph entry ${key}`);
    if (/\.(js|json)$/u.test(entry.file)) files.add(entry.file);
    for (const asset of entry.assets ?? [])
      if (asset.endsWith(".json")) files.add(asset);
    for (const dependency of [
      ...(entry.imports ?? []),
      ...(dynamic ? (entry.dynamicImports ?? []) : []),
    ])
      graph(dependency, dynamic, seen, files);
    return files;
  }
  const scripts = (entry) =>
    [
      ...readFileSync(resolve(root, entry), "utf8").matchAll(
        /<script\b[^>]*\bsrc="([^"]+)"/gu,
      ),
    ].map((m) => m[1].replace(/^\//u, ""));
  const entries = Object.keys(manifest).filter((k) => k.endsWith(".html"));
  const data = {
    entries: {},
    modules: {},
    initial: {},
    loading: [],
    navigation: [],
  };
  for (const entry of entries)
    data.entries[entry] = count(new Set([...graph(entry), ...scripts(entry)]));
  const unique = new Set(
    readdirSync(resolve(root, "assets"))
      .filter((f) => /\.(js|json)$/u.test(f))
      .map((f) => "assets/" + f),
  );
  for (const file of readdirSync(root))
    if (file.endsWith(".js")) unique.add(file);
  data.unique = count(unique);
  data.files = [...unique].sort();
  if (manifest["index.html"]) {
    for (const locale of ["en", "ru"])
      data.initial[locale] = count(
        new Set([
          ...graph("index.html", false),
          ...graph(`src/ui/locales/product-${locale}.ts`, false),
          ...scripts("index.html"),
        ]),
        6,
      );
    for (const [name, key] of Object.entries({
      operator: "src/operator/page.tsx",
      administration: "src/admin/page.tsx",
      insights: "src/insights/page.tsx",
    }))
      data.modules[name] = count(
        new Set([
          ...(manifest[key] ? graph(key) : graph("index.html")),
          ...graph("index.html", false),
          ...scripts("index.html"),
        ]),
      );
  }
  for (const [kind, rows] of Object.entries({
    loading: loading.runs,
    navigation: loading.navigations,
  }))
    for (const row of rows.filter((r) => r.variant === variant.name)) {
      const summarize = (key) => {
        const times = row.samples
          .map((s) => s[key].elapsed)
          .sort((a, b) => a - b);
        return {
          p50: times[Math.ceil(times.length * 0.5) - 1],
          p95: times[Math.ceil(times.length * 0.95) - 1],
          transfer: row.samples[0][key].transfer,
          files: row.samples[0][key].resources,
        };
      };
      data[kind].push({
        path: row.path,
        language: row.language,
        cold: summarize("cold"),
        warm: summarize("warm"),
        actions: row.samples.map((s) => s.actions ?? {}),
      });
    }
  if (
    variant.entry !== "design" &&
    (data.loading.length !== 10 || data.navigation.length !== 8)
  )
    throw new Error("Incomplete route and locale measurement matrix");
  if (["selected", "bilingual"].includes(variant.name)) {
    for (const row of loading.runs.filter(
      (value) => value.variant === variant.name && value.path !== "/",
    )) {
      const feature = ["/admin", "/integrations"].includes(row.path)
        ? "admin"
        : "insights";
      for (const locale of ["en", "ru"]) {
        const dictionary = Object.values(manifest).find(
          (value) =>
            value.file.endsWith(".json") &&
            value.src?.includes(`${feature}/locales/${locale}`),
        );
        if (!dictionary) throw new Error("Missing module dictionary asset");
        const required =
          variant.name === "bilingual" || locale === row.language;
        for (const sample of row.samples)
          if (
            sample.cold.resources.some(
              (value) => value.url === "/" + dictionary.file,
            ) !== required
          )
            throw new Error(
              "Module language fetching does not match its loading variant",
            );
      }
    }
  }
  data.withinBudget =
    data.unique <= 300000 &&
    (!data.entries["index.html"] || data.entries["index.html"] <= 250000) &&
    (!data.entries["design-system.html"] ||
      data.entries["design-system.html"] <= 260000) &&
    Object.values(data.initial).every((n) => n <= 150000);
  result.variants[variant.name] = data;
}
const selected = result.variants.inline;
if (!selected?.withinBudget)
  throw new Error("Default loading variant exceeds delivery budgets");
for (const row of [...selected.loading, ...selected.navigation])
  if (row.cold.p95 > 2000 || row.warm.p95 > 1500)
    throw new Error("Module loading exceeds latency budget");
const contents = new Map();
for (const name of ["application", "design"])
  for (const file of result.variants[name].files) {
    const bytes = readFileSync(
      resolve(variants.find((v) => v.name === name).directory, file),
    );
    contents.set(
      createHash("sha256").update(bytes).digest("hex"),
      gzipSync(bytes, { level: 9 }).length,
    );
  }
result.separateArtifacts = {
  sum: result.variants.application.unique + result.variants.design.unique,
  uniqueContent: [...contents.values()].reduce((sum, n) => sum + n, 0),
};
writeFileSync(
  resolve(output, "comparison.json"),
  JSON.stringify(result, null, 2) + "\n",
);
console.log(
  JSON.stringify(
    Object.fromEntries(
      Object.entries(result.variants).map(([k, v]) => [
        k,
        {
          initial: v.initial,
          entries: v.entries,
          unique: v.unique,
          withinBudget: v.withinBudget,
        },
      ]),
    ),
    null,
    2,
  ),
);
