import { Navigate, useLocation } from "@tanstack/react-router";
import { lazy, Suspense } from "react";
import { Button } from "@psim/design-system";
import { useShell } from "./context";
import { allowedModules, modules } from "./modules";
import { useStrings } from "../ui/strings";
const Home = lazy(async () => ({ default: (await import("../pages/home")).HomePage }));
export function ModulePage() {
  const { session, feed, shell } = useShell();
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
        <Home state={feed} retry={shell.reconnect} />
      </Suspense>
    );
  const descriptions = {
    administration: t.administrationHint,
    integrations: t.integrationsHint,
    audit: t.auditHint,
    reports: t.reportsModuleHint,
  };
  return (
    <main id="main-content" tabIndex={-1} className="workbench">
      <p className="eyebrow">{t.title}</p>
      <h1>{t[module.key]}</h1>
      <p>{descriptions[module.key]}</p>
    </main>
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
