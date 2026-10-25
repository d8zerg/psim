import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

// Logic of the console (stores, routing glue) is covered at 80%.
export default defineConfig({
  plugins: [react()],
  test: {
    environment: "jsdom",
    maxWorkers: 4,
    include: ["src/**/*.test.{ts,tsx}"],
    setupFiles: ["src/test-setup.ts"],
    coverage: {
      provider: "v8",
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/**/*.test.{ts,tsx}", "src/main.tsx", "src/design-main.tsx", "src/test-setup.ts"],
      thresholds: { lines: 80, branches: 80, functions: 80, statements: 80 },
    },
  },
});
