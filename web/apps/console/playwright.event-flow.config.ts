import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "e2e-event-flow",
  outputDir: "../../../build/event-flow-test/playwright-results",
  timeout: 120_000 + Number(process.env.PSIM_SHELL_OUTAGE_MS ?? 0),
  forbidOnly: true,
  retries: 0,
  reporter: [["list"]],
  use: {
    baseURL: "http://localhost:4173",
    actionTimeout: 15_000,
    trace: "retain-on-failure",
  },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"] } }],
});
