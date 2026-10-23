import { loadStrings } from "./ui/locales/loader";
await Promise.all([loadStrings("en", true), loadStrings("ru", true)]);
