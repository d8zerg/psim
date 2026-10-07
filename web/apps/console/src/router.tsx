import { createRootRoute, createRoute, createRouter, Outlet } from "@tanstack/react-router";

import { HomePage } from "./pages/home";

const rootRoute = createRootRoute({
  component: () => (
    <div className="min-h-screen">
      <header className="border-b border-border px-8 py-3 text-sm font-medium">PSIM</header>
      <Outlet />
    </div>
  ),
});

const homeRoute = createRoute({ getParentRoute: () => rootRoute, path: "/", component: HomePage });

export function createAppRouter() {
  return createRouter({ routeTree: rootRoute.addChildren([homeRoute]) });
}

declare module "@tanstack/react-router" {
  interface Register {
    router: ReturnType<typeof createAppRouter>;
  }
}
