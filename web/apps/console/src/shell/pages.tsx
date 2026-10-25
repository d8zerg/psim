import { Navigate, useLocation } from "@tanstack/react-router";
import { lazy, Suspense } from "react";
import { Button } from "@psim/design-system";
import { useShell } from "./context";
import { allowedModules, modules } from "./modules";
import { useStrings } from "../ui/strings";
const Home = lazy(async () => ({ default: (await import("../operator/page")).OperatorPage }));
const Administration = lazy(async () => ({ default: (await import("../admin/page")).AdministrationPage }));
const Insights = lazy(async () => ({ default: (await import("../insights/page")).InsightsPage }));
export function ModulePage() {
  const { session } = useShell();
  const t = useStrings();
  const path = useLocation({ select: (location) => location.pathname });
  const available = allowedModules(session.profile);
  const module = modules.find((entry) => entry.path === path);
  if (path === "/" && available[0] && !available.some((entry) => entry.path === "/"))
    return <Navigate to={available[0].path} replace />;
  if (!module || !available.some((entry) => entry.path === path))
    return (
      <main id="main-content" tabIndex={-1} className="workbench">
        <h1>{module ? t.forbidden : t.notFound}</h1>
        <p>{module ? t.forbiddenHint : t.notFoundHint}</p>
      </main>
    );
  if (module.key === "console")
    return (
      <Suspense
        fallback={
          <main id="main-content" tabIndex={-1} className="workbench">
            <p role="status">{t.loading}</p>
          </main>
        }
      >
        <Home key={session.profile?.context} />
      </Suspense>
    );
  if (module.key === "administration" || module.key === "integrations")
    return (
      <Suspense
        fallback={
          <main id="main-content" tabIndex={-1} className="workbench">
            <p role="status">{t.loading}</p>
          </main>
        }
      >
        <Administration integration={module.key === "integrations"} />
      </Suspense>
    );
  return (
    <Suspense
      fallback={
        <main id="main-content" tabIndex={-1} className="workbench">
          <p role="status">{t.loading}</p>
        </main>
      }
    >
      <Insights audit={module.key === "audit"} />
    </Suspense>
  );
}
export function RouteError({ reset }: { readonly reset: () => void }) {
  const t = useStrings();
  return (
    <main id="main-content" tabIndex={-1} className="workbench">
      <h1>{t.appError}</h1>
      <p>{t.appErrorHint}</p>
      <Button
        onClick={() => {
          reset();
          location.reload();
        }}
      >
        {t.refresh}
      </Button>
    </main>
  );
}
