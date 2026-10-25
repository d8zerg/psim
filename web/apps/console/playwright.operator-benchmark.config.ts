import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "e2e",
  testMatch: "operator-performance.spec.ts",
  timeout: 90000,
  forbidOnly: true,
  retries: 0,
  workers: 1,
  fullyParallel: false,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:4173", trace: "retain-on-failure" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  webServer: { command: "pnpm preview", url: "http://localhost:4173", reuseExistingServer: false, timeout: 60000 },
});
