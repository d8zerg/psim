import { defineConfig, devices } from "@playwright/test";

// End-to-end smoke of the built console (step 1.5); scenario tests arrive with step 8.6.
export default defineConfig({
  testDir: "e2e",
  forbidOnly: true,
  retries: 0,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:4173", trace: "retain-on-failure" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
  webServer: { command: "pnpm preview", url: "http://localhost:4173", reuseExistingServer: false, timeout: 60_000 },
});
