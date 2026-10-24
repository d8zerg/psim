import "./styles.css";
import { StrictMode } from "react";
import { createRoot } from "react-dom/client";
import { RouterProvider } from "@tanstack/react-router";
import { auth } from "./auth";
import { ShellController } from "./shell/controller";
import { createAppRouter } from "./router";
import { startWithLocale } from "./ui/bootstrap";
const root = document.getElementById("root");
if (root === null) throw new Error("missing #root element");
const application = new ShellController(auth, async () => {
  const { createFeed } = await import("./feed/controller");
  return createFeed(auth.token, () => {
    auth.invalidate();
  });
});
void startWithLocale(root, () => {
  createRoot(root).render(
    <StrictMode>
      <RouterProvider router={createAppRouter(application)} />
    </StrictMode>,
  );
});
