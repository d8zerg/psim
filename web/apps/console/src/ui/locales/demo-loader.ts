import { registerDemoImports } from "./loader";
registerDemoImports({ en: () => import("./demo-en"), ru: () => import("./demo-ru") });
