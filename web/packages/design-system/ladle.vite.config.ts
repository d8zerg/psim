import { resolve } from "node:path";
import tailwindcss from "@tailwindcss/vite";
import { defineConfig } from "vite";
export default defineConfig({
  plugins: [
    tailwindcss(),
    {
      name: "catalogue-font-preloads",
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
                .map((file) => `/@fs${resolve(import.meta.dirname, "src/gen/fonts", file)}`);
          return fonts.map((href) => ({
            tag: "link",
            attrs: { rel: "preload", href, as: "font", type: "font/woff2", crossorigin: "anonymous" },
            injectTo: "head-prepend" as const,
          }));
        },
      },
    },
  ],
});
