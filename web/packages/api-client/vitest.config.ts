import { defineConfig } from "vitest/config";

// Coverage threshold of client logic (strategy.md section 6); generated types carry no code.
export default defineConfig({
  test: {
    coverage: {
      provider: "v8",
      include: ["src/**/*.ts"],
      exclude: ["src/gen/**", "src/**/*.test.ts"],
      thresholds: { lines: 80, branches: 80, functions: 80, statements: 80 },
    },
  },
});
