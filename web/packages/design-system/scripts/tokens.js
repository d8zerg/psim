import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { resolve } from "node:path";
import process from "node:process";
import {
  converter,
  formatHex,
  formatHex8,
  wcagContrast,
  interpolate,
  filterDeficiencyProt,
  filterDeficiencyDeuter,
  filterDeficiencyTrit,
  filterGrayscale,
} from "culori";

const root = resolve(import.meta.dirname, "..");
const sourceFlag = process.argv.indexOf("--source");
const sourceFile = sourceFlag < 0 ? resolve(root, "tokens/foundation.tokens.json") : process.argv[sourceFlag + 1];
if (!sourceFile) throw new Error("Missing token source");
const source = JSON.parse(readFileSync(sourceFile, "utf8"));
const check = process.argv.includes("--check");
const rgb = converter("rgb");
function tokenAt(path) {
  return path.split(".").reduce((node, key) => node?.[key], source);
}
function value(token, seen = new Set()) {
  if (!token || !token.$type || !("$value" in token)) throw new Error("Invalid token");
  const raw = token.$value;
  if (typeof raw === "string" && raw.startsWith("{")) {
    const path = raw.slice(1, -1);
    if (seen.has(path)) throw new Error(`Cyclic token: ${path}`);
    if (path.startsWith("semantic.$theme.")) {
      const role = path.slice("semantic.$theme.".length);
      for (const theme of Object.keys(source.semantic))
        value(tokenAt(`semantic.${theme}.${role}`), new Set([...seen, path]));
      return `var(--${role})`;
    }
    if (!tokenAt(path)) throw new Error(`Invalid token: ${path}`);
    if (tokenAt(path).$type !== token.$type) throw new Error(`Token type mismatch: ${path}`);
    return value(tokenAt(path), new Set([...seen, path]));
  }
  switch (token.$type) {
    case "color": {
      if (raw.colorSpace !== "oklch" || raw.components.length !== 3) throw new Error("Expected OKLCH color");
      const [l, c, h] = raw.components;
      if (!(
        raw.components.every(Number.isFinite) &&
        l >= 0 &&
        l <= 1 &&
        c >= 0 &&
        h >= 0 &&
        h <= 360 &&
        raw.alpha >= 0 &&
        raw.alpha <= 1
      ))
        throw new Error("Invalid color range");
      const converted = rgb({ mode: "oklch", l, c, h, alpha: raw.alpha });
      return raw.alpha === 1 ? formatHex(converted) : formatHex8(converted);
    }
    case "dimension":
    case "duration":
      if (!Number.isFinite(raw.value) || raw.unit !== (token.$type === "dimension" ? "px" : "ms"))
        throw new Error("Invalid unit");
      return `${raw.value}${raw.unit}`;
    case "fontFamily":
      if (!Array.isArray(raw) || !raw.length || raw.some((item) => typeof item !== "string" || !item))
        throw new Error("Invalid font family");
      return raw.map((family) => (family.includes(" ") ? JSON.stringify(family) : family)).join(", ");
    case "cubicBezier":
      if (
        !Array.isArray(raw) ||
        raw.length !== 4 ||
        !raw.every(Number.isFinite) ||
        raw[0] < 0 ||
        raw[0] > 1 ||
        raw[2] < 0 ||
        raw[2] > 1
      )
        throw new Error("Invalid easing");
      return `cubic-bezier(${raw.join(", ")})`;
    case "shadow":
      return (
        ["offsetX", "offsetY", "blur", "spread"]
          .map((key) => value({ $type: "dimension", $value: raw[key] }, seen))
          .join(" ") +
        " " +
        value({ $type: "color", $value: raw.color }, seen)
      );
    case "fontWeight":
    case "number":
      if (!Number.isFinite(raw)) throw new Error("Invalid numeric token");
      return String(raw);
    default:
      throw new Error(`Unsupported token type: ${token.$type}`);
  }
}
function validateGroup(group) {
  for (const [key, node] of Object.entries(group)) {
    if (key.startsWith("$")) continue;
    if (!node || typeof node !== "object") throw new Error(`Invalid group: ${key}`);
    if ("$value" in node) {
      if (Object.keys(node).some((name) => !name.startsWith("$"))) throw new Error(`Mixed token and group: ${key}`);
      value(node);
    } else validateGroup(node);
  }
}
validateGroup(source);
function block(selector, tokens, prefix = "") {
  return `${selector} {\n${Object.entries(tokens)
    .map(([key, token]) => `  --${prefix}${key}: ${value(token)};`)
    .join("\n")}\n}\n`;
}
let css = "/* Generated presentation tokens. */\n";
const themes = {};
for (const [theme, tokens] of Object.entries(source.semantic)) {
  const selector = theme === "dark" ? ':root, [data-theme="dark"]' : `[data-theme="${theme}"]`;
  css += block(selector, tokens);
  themes[theme] = Object.fromEntries(Object.entries(tokens).map(([key, token]) => [key, value(token)]));
}
for (const [name, tokens] of Object.entries(source.density))
  css += block(name === "comfortable" ? ':root, [data-density="comfortable"]' : `[data-density="${name}"]`, tokens);
for (const [name, tokens] of Object.entries(source.mode))
  css += block(name === "normal" ? ':root, [data-mode="normal"]' : `[data-mode="${name}"]`, tokens, "mode-");
for (const [name, tokens] of Object.entries(source.base)) {
  if (!["palette", "color"].includes(name)) css += block(":root", tokens, `${name}-`);
}
for (const [name, tokens] of Object.entries(source.component)) css += block(":root", tokens, `${name}-`);
css += ":root { --selected: color-mix(in srgb, var(--primary) 14%, var(--card));  }\n";
css += '[data-theme="light"] { color-scheme: light; }\n';
css += ':root, [data-theme="dark"], [data-theme="contrast"], [data-theme="night"] { color-scheme: dark; }\n';

// All text roles are checked on every surface, including tinted badge backgrounds.
let pairs = 0;
for (const [theme, tokens] of Object.entries(themes)) {
  const minimum = theme === "contrast" ? 7 : 4.5;
  for (const mode of Object.keys(source.mode)) {
    for (const foreground of [
      "foreground",
      "muted-foreground",
      "neutral",
      "danger",
      "warning",
      "success",
      "info",
      "link",
    ]) {
      for (const surface of ["background", "card", "muted", "elevated"]) {
        const ratio = wcagContrast(tokens[foreground], tokens[surface]);
        if (!Number.isFinite(ratio) || ratio + 0.001 < minimum)
          throw new Error(`${theme}/${mode} ${foreground}/${surface}: ${ratio.toFixed(2)} < ${minimum}`);
        pairs++;
      }
    }
    for (const foreground of ["primary", "danger-solid", "warning-solid", "success-solid", "info-solid"]) {
      const ratio = wcagContrast(tokens[foreground], tokens[`${foreground}-foreground`]);
      if (!Number.isFinite(ratio) || ratio + 0.001 < minimum)
        throw new Error(`${theme}/${mode} ${foreground}: ${ratio.toFixed(2)} < ${minimum}`);
      pairs++;
    }
    for (const edge of ["input", "ring"]) {
      if (wcagContrast(tokens[edge], tokens.card) < 3) throw new Error(`${theme}/${mode} ${edge} boundary`);
      pairs++;
    }
  }
}
// Contrast of composed component surfaces, including retained selection and tinted messages.
for (const [theme, tokens] of Object.entries(themes)) {
  const minimum = theme === "contrast" ? 7 : 4.5;
  const verify = (foreground, background, label) => {
    const ratio = wcagContrast(foreground, background);
    if (!Number.isFinite(ratio) || ratio + 0.001 < minimum)
      throw new Error(`${theme} ${label}: ${ratio.toFixed(2)} < ${minimum}`);
    pairs++;
  };
  for (const mode of Object.keys(source.mode)) {
    const selected = interpolate([tokens.card, tokens.primary], "rgb")(0.14);
    for (const foreground of [
      "foreground",
      "muted-foreground",
      "danger",
      "warning",
      "info",
      "success",
      "neutral",
      "link",
    ])
      verify(tokens[foreground], selected, `${mode}/${foreground}/selected`);
    for (const tone of ["neutral", "danger", "warning", "info", "success"]) {
      verify(tokens[tone], interpolate([tokens.card, tokens[tone]], "rgb")(0.14), `${mode}/${tone}/badge`);
      const notice = interpolate([tokens.card, tokens[tone]], "rgb")(0.08);
      for (const foreground of ["foreground", "muted-foreground"])
        verify(tokens[foreground], notice, `${mode}/${foreground}/${tone}/notice`);
    }
    for (const edge of ["input", "ring"])
      for (const surface of ["background", "muted", "elevated"]) {
        if (wcagContrast(tokens[edge], tokens[surface]) < 3)
          throw new Error(`${theme}/${mode}/${edge}/${surface} boundary`);
        pairs++;
      }
  }
  // CVD simulations validate readable priority text, not discrimination by hue.
  for (const simulate of [
    filterDeficiencyProt(),
    filterDeficiencyDeuter(),
    filterDeficiencyTrit(),
    filterGrayscale(),
  ]) {
    for (const role of ["danger", "warning", "neutral"]) {
      const background = interpolate([tokens.card, tokens[role]], "rgb")(0.14);
      if (wcagContrast(simulate(tokens[role]), simulate(background)) < 3)
        throw new Error(`${theme}/${role} CVD text contrast`);
      pairs++;
    }
    if (wcagContrast(simulate(tokens["danger-solid-foreground"]), simulate(tokens["danger-solid"])) < 3)
      throw new Error(`${theme} critical CVD text contrast`);
    pairs++;
  }
}
const presentation = Object.fromEntries(
  Object.entries(themes).map(([key, tokens]) => [
    key,
    { background: tokens.background, colorScheme: key === "light" ? "light" : "dark" },
  ]),
);
const meta = { themes: presentation, densities: Object.keys(source.density), modes: Object.keys(source.mode) };
const bootstrap = `// Generated early presentation.\n(() => {\n  const choices = ${JSON.stringify(meta)};\n  let saved = {};\n  try { saved = JSON.parse(localStorage.getItem("psim.ui") ?? "{}"); } catch { /* Use defaults. */ }\n  if (saved === null || typeof saved !== "object") saved = {};\n  const theme = typeof saved.theme === "string" && Object.hasOwn(choices.themes, saved.theme) ? saved.theme : "dark";\n  const root = document.documentElement;\n  root.dataset.theme = theme;\n  root.dataset.density = choices.densities.includes(saved.density) ? saved.density : "comfortable";\n  root.dataset.mode = choices.modes.includes(saved.mode) ? saved.mode : "normal";\n  root.lang = saved.language === "en" ? "en" : "ru";\n  Object.assign(root.style, { backgroundColor: choices.themes[theme].background, colorScheme: choices.themes[theme].colorScheme });\n})();\n`;
const outputs = new Map([
  [resolve(root, "src/gen/tokens.css"), css],
  [
    resolve(root, "src/gen/tokens.ts"),
    `// Generated presentation metadata.\nexport const presentation = ${JSON.stringify(meta)} as const;\nexport type Theme = keyof typeof presentation.themes;\nexport type Density = (typeof presentation.densities)[number];\nexport type Mode = (typeof presentation.modes)[number];\n`,
  ],
  [resolve(root, "../../apps/console/public/theme-init.js"), bootstrap],
]);
for (const [packageName, names] of [
  [
    "@fontsource-variable/ibm-plex-sans",
    ["ibm-plex-sans-cyrillic-standard-normal.woff2", "ibm-plex-sans-latin-standard-normal.woff2"],
  ],
  ["@fontsource/ibm-plex-mono", ["ibm-plex-mono-cyrillic-400-normal.woff2", "ibm-plex-mono-latin-400-normal.woff2"]],
]) {
  for (const name of names)
    outputs.set(
      resolve(root, "src/gen/fonts", name),
      readFileSync(resolve(root, "node_modules", packageName, "files", name)),
    );
}
for (const [file, content] of outputs) {
  if (check) {
    if (typeof content === "string" ? readFileSync(file, "utf8") !== content : !readFileSync(file).equals(content))
      throw new Error(`Stale generated presentation: ${file}`);
  } else {
    mkdirSync(resolve(file, ".."), { recursive: true });
    writeFileSync(file, content);
  }
}
console.log(
  `Presentation: ${pairs} contrast pairs across ${Object.keys(themes).length} themes and ${Object.keys(source.mode).length} modes`,
);
