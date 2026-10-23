import { createRootRoute, createRoute, createRouter, Outlet } from "@tanstack/react-router";
import { HomePage } from "./pages/home";
import { AppLayout } from "./ui/layout";
const rootRoute = createRootRoute({
  component: () => (
    <AppLayout>
      <Outlet />
    </AppLayout>
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
