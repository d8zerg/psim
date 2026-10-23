import "./styles.css";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { HomePage } from "./pages/home";
import { AppLayout } from "./ui/layout";
import { startWithLocale } from "./ui/bootstrap";
const root = document.getElementById("root");
if (root === null) throw new Error("missing #root element");
void startWithLocale(root, () => {
  createRoot(root).render(
    <StrictMode>
      <AppLayout>
        <HomePage />
      </AppLayout>
    </StrictMode>,
  );
});
