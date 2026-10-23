/** @type {import('@ladle/react').UserConfig} */
export default {
  stories: "stories/**/*.stories.tsx",
  outDir: "dist",
  host: "0.0.0.0",
  port: 61000,
  previewHost: "0.0.0.0",
  previewPort: 61001,
  viteConfig: "ladle.vite.config.ts",
  addons: {
    a11y: { enabled: true },
    msw: { enabled: false },
    theme: { enabled: false },
  },
};
