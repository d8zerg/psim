import { defineConfig, devices } from "@playwright/test";

// End-to-end smoke tests of the built console.
export default defineConfig({
  testDir: "e2e",
  testIgnore: "**/operator-performance.spec.ts",
  forbidOnly: true,
  retries: 0,
  workers: 4,
  fullyParallel: true,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:4173", trace: "retain-on-failure" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  webServer: { command: "pnpm preview", url: "http://localhost:4173", reuseExistingServer: false, timeout: 60_000 },
});
