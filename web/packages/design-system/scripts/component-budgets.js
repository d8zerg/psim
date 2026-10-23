import { mkdtempSync, writeFileSync, rmSync, readdirSync, statSync, readFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { gzipSync } from "node:zlib";
import process from "node:process";
import { build } from "vite";
const root = resolve(import.meta.dirname, "..");
const directory = mkdtempSync(resolve(tmpdir(), "psim-components-"));
const cases = [
  ["cardRecipe", "components/recipes.ts", 2000],
  ["PriorityBadge", "components/domain.tsx", 12500],
  ["AlarmStateBadge", "components/domain.tsx", 12500],
  ["ConnectionIndicator", "components/domain.tsx", 12000],
  ["SlaTimer", "components/domain.tsx", 1000],
  ["AckButton", "components/domain.tsx", 14000],
  ["CommandLifecycle", "components/domain.tsx", 12500],
  ["AuditReasonField", "components/domain.tsx", 12500],
  ["DeviceMarker", "components/domain.tsx", 14500],
  ["AlarmBanner", "components/domain.tsx", 2500],
  ["CameraTile", "components/domain.tsx", 14000],
  ["Provenance", "components/domain.tsx", 43500],
  ["Timestamp", "components/domain.tsx", 1000],

  ["Dialog", "components/overlays.tsx", 30000],
  ["Sheet", "components/overlays.tsx", 30000],
  ["Popover", "components/overlays.tsx", 40500],
  ["Tooltip", "components/overlays.tsx", 25000],
  ["DropdownMenu", "components/overlays.tsx", 38000],
  ["ContextMenu", "components/overlays.tsx", 38000],
  ["NotificationCenter", "components/overlays.tsx", 14000],
  ["Toast", "components/overlays.tsx", 14000],
  ["Tabs", "components/navigation.tsx", 12000],
  ["Breadcrumbs", "components/navigation.tsx", 500],
  ["SideNav", "components/navigation.tsx", 500],
  ["Pagination", "components/navigation.tsx", 14000],
  ["CommandPalette", "components/navigation.tsx", 31000],
  ["Combobox", "components/forms.tsx", 25000],
  ["MultiSelect", "components/forms.tsx", 11500],
  ["DatePeriodPicker", "components/forms.tsx", 12000],
  ["FileUpload", "components/forms.tsx", 14500],
  ["ValidatedForm", "components/forms.tsx", 57000],
  ["DataTable", "components/collections.tsx", 44500],
  ["Tree", "components/collections.tsx", 15000],
  ["FloorPlan", "components/collections.tsx", 14500],
  ["TrendChart", "components/collections.tsx", 1000],
  ["Button", "components/button.tsx", 13000],
  ...["Badge", "Card", "Field", "SelectField"].map((name) => [name, "components/patterns.tsx", 12000]),
  ...["Notice", "Switch", "DataRegion", "TableContainer", "EmptyState"].map((name) => [
    name,
    "components/patterns.tsx",
    13000,
  ]),
  ["ConfirmAction", "components/confirm-action.tsx", 35000],
  ["Textarea", "components/controls.tsx", 13000],
  ["IconButton", "components/controls.tsx", 14000],
  ...[
    "Checkbox",
    "RadioGroup",
    "NumberInput",
    "Slider",
    "ButtonGroup",
    "Progress",
    "Spinner",
    "Skeleton",
    "Kbd",
    "Tag",
    "Avatar",
    "DescriptionList",
    "Stat",
    "Timeline",
    "Stepper",
  ].map((name) => [name, "components/controls.tsx", 2000]),
  ["Icon", "components/icons.tsx", 2000],
  ["domainIcons", "components/icons.tsx", 5000],
  ["transitionAlarm", "alarm-model.ts", 2000],
  ["transitionCommand", "alarm-model.ts", 2500],
];
const budgeted = new Set(cases.map(([name]) => name));
for (const file of readdirSync(resolve(root, "src/components"))) {
  if (!file.endsWith(".tsx") || file.endsWith(".test.tsx")) continue;
  for (const match of readFileSync(resolve(root, "src/components", file), "utf8").matchAll(
    /export function ([A-Z]\w*)/gu,
  )) {
    if (!budgeted.has(match[1])) throw new Error(`Missing component budget: ${match[1]}`);
  }
}
try {
  for (const [name, file, limit] of cases) {
    const entry = resolve(directory, "entry.ts");
    writeFileSync(entry, `export { ${name} } from ${JSON.stringify(resolve(root, "src", file))};\n`);
    const result = await build({
      configFile: false,
      logLevel: "silent",
      build: {
        write: false,
        minify: true,
        lib: { entry, formats: ["es"] },
        rolldownOptions: { external: ["react", "react-dom", "react/jsx-runtime", "react/jsx-dev-runtime"] },
      },
    });
    const outputs = Array.isArray(result) ? result : [result];
    let bytes = 0;
    for (const output of outputs)
      for (const chunk of output.output) if (chunk.type === "chunk") bytes += gzipSync(chunk.code).length;
    console.log(`${name}: ${bytes} bytes gzip; limit ${limit}`);
    if (!bytes || bytes > limit) process.exitCode = 1;
  }
  const fonts = resolve(root, "src/gen/fonts");
  const bytes = readdirSync(fonts)
    .filter((file) => file.endsWith(".woff2"))
    .reduce((sum, file) => sum + statSync(resolve(fonts, file)).size, 0);
  console.log(`Local WOFF2: ${bytes} bytes; limit 150000`);
  if (!bytes || bytes > 150000) process.exitCode = 1;
} finally {
  rmSync(directory, { recursive: true, force: true });
}
