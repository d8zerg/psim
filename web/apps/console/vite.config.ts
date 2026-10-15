import process from "node:process";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
const proxy = { "/v1": { target: process.env.PSIM_API_UPSTREAM ?? "http://localhost:8088", ws: true } };
export default defineConfig({
  plugins: [react(), tailwindcss()],
  server: { proxy },
  preview: { proxy, allowedHosts: ["console"] },
  build: { target: "es2023", sourcemap: true },
});
