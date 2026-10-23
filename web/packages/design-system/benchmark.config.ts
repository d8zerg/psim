import { defineConfig } from "@playwright/test";
import base from "./playwright.config";
export default defineConfig({ ...base, testMatch: "**/benchmark.ts", workers: 1, fullyParallel: false });
