import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "e2e-insights",
  outputDir: "../../../build/insights/playwright-results",
  timeout: 240000,
  forbidOnly: true,
  retries: 0,
  maxFailures: 1,
  workers: 1,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:4173", actionTimeout: 15000, trace: "retain-on-failure" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
});
