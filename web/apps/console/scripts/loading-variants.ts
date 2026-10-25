import ts from "typescript";
import type { Plugin } from "vite";
export function loadingVariant(name: string): Plugin {
  if (!["inline", "selected", "eager", "bilingual", "widgets"].includes(name))
    throw new Error("Invalid loading variant");
  return {
    name: "loading-comparison",
    enforce: "pre",
    config() {
      return ["selected", "bilingual"].includes(name)
        ? { build: { assetsInlineLimit: (file: string) => (file.endsWith(".json") ? false : undefined) } }
        : undefined;
    },
    generateBundle(_options, bundle) {
      for (const asset of Object.values(bundle)) {
        if (asset.type === "asset" && asset.fileName.startsWith("assets/") && asset.fileName.endsWith(".json")) {
          const source = typeof asset.source === "string" ? asset.source : new TextDecoder().decode(asset.source);
          asset.source = JSON.stringify(JSON.parse(source));
        }
      }
    },
    transform(code, id) {
      const file = id.split("?")[0] ?? "";
      const transformed = (value: string) => {
        const output = ts.transpileModule(value, {
          fileName: file,
          compilerOptions: {
            sourceMap: true,
            inlineSources: true,
            jsx: ts.JsxEmit.Preserve,
            target: ts.ScriptTarget.ESNext,
            module: ts.ModuleKind.ESNext,
          },
        });
        const map = JSON.parse(output.sourceMapText ?? "{}") as {
          version: number;
          names: string[];
          mappings: string;
          sources: string[];
          sourcesContent: string[];
        };
        map.sources = [file + "." + name];
        map.sourcesContent = [value];
        return { code: output.outputText, map };
      };
      if (["selected", "bilingual"].includes(name) && /\/src\/(admin|insights)\/strings\.ts$/u.test(file)) {
        const type = file.endsWith("/admin/strings.ts") ? "Dictionary" : "Strings";
        code = code
          .replace('import en from "./locales/en.json";', 'import type en from "./locales/en.json";')
          .replace(
            'import ru from "./locales/ru.json";',
            'import { moduleDictionary, readDictionary } from "../ui/locales/modules";',
          );
        return transformed(
          code.replace(
            /const read =[\s\S]*?;\n/u,
            `const read = moduleDictionary<${type}>({ en: () => readDictionary<${type}>(new URL("./locales/en.json", import.meta.url)), ru: () => readDictionary<${type}>(new URL("./locales/ru.json", import.meta.url)) });\n`,
          ),
        );
      }
      if (name === "eager" && file.endsWith("/src/shell/pages.tsx")) {
        const source = ts.createSourceFile(file, code, ts.ScriptTarget.Latest, true, ts.ScriptKind.TSX);
        const imports: Record<string, string> = {
          Home: 'import { OperatorPage } from "../operator/page"; const Home = () => Promise.resolve({ default: OperatorPage });',
          Administration:
            'import { AdministrationPage } from "../admin/page"; const Administration = () => Promise.resolve({ default: AdministrationPage });',
          Insights:
            'import { InsightsPage } from "../insights/page"; const Insights = () => Promise.resolve({ default: InsightsPage });',
        };
        for (const statement of [...source.statements].reverse()) {
          if (!ts.isVariableStatement(statement)) continue;
          const declaration = statement.declarationList.declarations[0];
          const replacement =
            declaration && ts.isIdentifier(declaration.name) ? imports[declaration.name.text] : undefined;
          if (replacement) code = code.slice(0, statement.getStart(source)) + replacement + code.slice(statement.end);
        }
        return transformed(code);
      }
      if (name === "bilingual" && file.endsWith("/src/ui/locales/modules.ts"))
        return transformed(
          code.replace("active.add(load);", 'active.add(async () => Promise.all([load("en"), load("ru")]));'),
        );
      if (name === "widgets" && file.endsWith("/src/admin/editor.tsx")) {
        code = code.replace(
          'import { FormFields } from "./form";',
          'import { lazy, Suspense } from "react";\nimport { getStrings as loadingStrings } from "../ui/locales/loader";\nimport { usePreferences as loadingPreferences } from "../ui/preferences";\nconst FormFields = lazy(async () => ({default: (await import("./form")).FormFields}));',
        );
        return transformed(
          code.replace(
            /<FormFields[\s\S]*?\n\s*\/>/u,
            (match) =>
              `<Suspense fallback={<p role="status">{loadingStrings(loadingPreferences.getState().language).loading}</p>}>${match}</Suspense>`,
          ),
        );
      }
      return undefined;
    },
  };
}
