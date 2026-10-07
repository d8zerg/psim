import react from "@vitejs/plugin-react";
import { defineConfig } from "vitest/config";

// Logic of the console (stores, routing glue) is covered at 80% (strategy.md section 6).
export default defineConfig({
  plugins: [react()],
  test: {
    environment: "jsdom",
    include: ["src/**/*.test.{ts,tsx}"],
    coverage: {
      provider: "v8",
      include: ["src/**/*.{ts,tsx}"],
      exclude: ["src/**/*.test.{ts,tsx}", "src/main.tsx"],
      thresholds: { lines: 80, branches: 80, functions: 80, statements: 80 },
    },
  },
});
