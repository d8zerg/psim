import "./ui/locales/demo-loader";
import { loadStrings } from "./ui/locales/loader";
await Promise.all([loadStrings("en", true), loadStrings("ru", true)]);
