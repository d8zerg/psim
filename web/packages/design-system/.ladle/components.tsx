import "./styles.css";
import type { GlobalProvider } from "@ladle/react";
import { CataloguePresentation } from "../stories/presentation";
export const Provider: GlobalProvider = ({ children }) => <CataloguePresentation>{children}</CataloguePresentation>;
