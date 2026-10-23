import process from "node:process";
import { resolve } from "node:path";
import tailwindcss from "@tailwindcss/vite";
import react from "@vitejs/plugin-react";
import { defineConfig } from "vite";
const proxy = { "/v1": { target: process.env.PSIM_API_UPSTREAM ?? "http://localhost:8088", ws: true } };
export default defineConfig({
  plugins: [
    react(),
    tailwindcss(),
    {
      name: "local-font-preloads",
      transformIndexHtml: {
        order: "post",
        handler(_html, context) {
          const fonts = context.bundle
            ? Object.values(context.bundle)
                .filter((asset) => asset.fileName.includes("ibm-plex-") && asset.fileName.endsWith(".woff2"))
                .map((asset) => `/${asset.fileName}`)
            : ["cyrillic", "latin"]
                .flatMap((subset) => [
                  `ibm-plex-sans-${subset}-standard-normal.woff2`,
                  `ibm-plex-mono-${subset}-400-normal.woff2`,
                ])
                .map(
                  (file) => `/@fs${resolve(import.meta.dirname, "../../packages/design-system/src/gen/fonts", file)}`,
                );
          return fonts.map((href) => ({
            tag: "link",
            attrs: { rel: "preload", href, as: "font", type: "font/woff2", crossorigin: "anonymous" },
            injectTo: "head-prepend" as const,
          }));
        },
      },
    },
  ],
  server: { proxy },
  preview: { proxy, allowedHosts: ["console"] },
  build: {
    target: "es2023",
    sourcemap: true,
    manifest: true,
    rolldownOptions: { input: { console: "index.html", design: "design-system.html" } },
  },
});
