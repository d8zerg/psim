// ESLint of the PSIM web monorepo (step 1.5, ADR-037; TypeScript standard). Zero warnings is the
// gate: every package runs `eslint --max-warnings 0`. Type-aware rules use each package tsconfig.
import js from "@eslint/js";
import jsxA11y from "eslint-plugin-jsx-a11y";
import reactHooks from "eslint-plugin-react-hooks";
import globals from "globals";
import tseslint from "typescript-eslint";

export default tseslint.config(
  { ignores: ["**/dist/**", "**/coverage/**", "**/src/gen/**", "**/playwright-report/**", "**/test-results/**"] },
  js.configs.recommended,
  ...tseslint.configs.strictTypeChecked,
  ...tseslint.configs.stylisticTypeChecked,
  {
    languageOptions: {
      globals: { ...globals.browser },
      parserOptions: { projectService: true, tsconfigRootDir: import.meta.dirname },
    },
    plugins: { "react-hooks": reactHooks, "jsx-a11y": jsxA11y },
    rules: {
      ...reactHooks.configs.recommended.rules,
      ...jsxA11y.flatConfigs.recommended.rules,
      // Standard, section 3: no enums, explicit type imports, every promise handled.
      "no-restricted-syntax": [
        "error",
        { selector: "TSEnumDeclaration", message: "Use a union of literals or an `as const` object" },
      ],
      "@typescript-eslint/consistent-type-imports": "error",
      "@typescript-eslint/no-floating-promises": "error",
      "@typescript-eslint/switch-exhaustiveness-check": "error",
    },
    linterOptions: { reportUnusedDisableDirectives: "error" },
  },
  {
    files: ["**/*.config.{js,ts}", "**/vite.config.ts", "**/playwright.config.ts"],
    languageOptions: { globals: { ...globals.node } },
  },
  {
    files: ["**/*.js"],
    ...tseslint.configs.disableTypeChecked,
  },
);
