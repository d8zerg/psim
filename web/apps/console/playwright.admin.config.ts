import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "e2e-admin",
  outputDir: "../../../build/admin/playwright-results",
  timeout: 420000,
  forbidOnly: true,
  retries: 0,
  workers: 1,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:4173", actionTimeout: 15000, trace: "retain-on-failure" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
});
