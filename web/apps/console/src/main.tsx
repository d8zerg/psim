import "./styles.css";

import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { HomePage } from "./pages/home";

const root = document.getElementById("root");
if (root === null) {
  throw new Error("missing #root element");
}

createRoot(root).render(
  <StrictMode>
    <div className="min-h-screen">
      <header className="border-b border-border px-8 py-3 text-sm font-medium">PSIM</header>
      <HomePage />
    </div>
  </StrictMode>,
);
