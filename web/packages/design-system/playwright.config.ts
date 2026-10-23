import { defineConfig, devices } from "@playwright/test";
export default defineConfig({
  testDir: "e2e",
  fullyParallel: true,
  forbidOnly: true,
  retries: 0,
  workers: 4,
  reporter: [["list"]],
  use: { baseURL: "http://localhost:61001", trace: "retain-on-failure", reducedMotion: "reduce" },
  projects: [{ name: "chromium", use: { ...devices["Desktop Chrome"], viewport: { width: 1280, height: 1000 } } }],
  webServer: {
    command: "pnpm catalogue:preview",
    url: "http://localhost:61001",
    reuseExistingServer: false,
    timeout: 60_000,
  },
});
