import { createRootRoute, createRoute, createRouter, Outlet } from "@tanstack/react-router";
import { ShellContext } from "./shell/context";
import { ShellView } from "./shell/view";
import { ModulePage, RouteError } from "./shell/pages";
import { modules } from "./shell/modules";
import type { ShellController } from "./shell/controller";
export function createAppRouter(shell: ShellController) {
  const rootRoute = createRootRoute({
    component: () => (
      <ShellContext value={shell}>
        <ShellView>
          <Outlet />
        </ShellView>
      </ShellContext>
    ),
    notFoundComponent: ModulePage,
    errorComponent: ({ reset }) => <RouteError reset={reset} />,
  });
  return createRouter({
    routeTree: rootRoute.addChildren(
      modules.map((module) =>
        createRoute({ getParentRoute: () => rootRoute, path: module.path, component: ModulePage }),
      ),
    ),
    defaultPreload: false,
  });
}
declare module "@tanstack/react-router" {
  interface Register {
    router: ReturnType<typeof createAppRouter>;
  }
}
