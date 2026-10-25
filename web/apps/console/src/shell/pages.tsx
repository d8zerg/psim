import { moduleWithLocale } from "../ui/locales/modules";
import { Navigate, useLocation } from "@tanstack/react-router";
import { QueryClient, QueryClientProvider, useQuery } from "@tanstack/react-query";
import type { ComponentType } from "react";
import { Button } from "@psim/design-system";
import { useShell } from "./context";
import { allowedModules, modules } from "./modules";
import { useStrings } from "../ui/strings";
interface ModuleProps {
  readonly integration: boolean;
  readonly audit: boolean;
}
const Home = () => moduleWithLocale(async () => ({ default: (await import("../operator/page")).OperatorPage }));
const Administration = () =>
  moduleWithLocale(async () => ({ default: (await import("../admin/page")).AdministrationPage }));
const Insights = () => moduleWithLocale(async () => ({ default: (await import("../insights/page")).InsightsPage }));
const code = new QueryClient();
function Module({
  name,
  context,
}: {
  readonly name: "console" | "administration" | "integrations" | "audit" | "reports";
  readonly context: string;
}) {
  const t = useStrings();
  const key =
    name === "console"
      ? "console"
      : name === "administration" || name === "integrations"
        ? "administration"
        : "insights";
  const loaders: Record<
    "console" | "administration" | "insights",
    () => Promise<{ default: ComponentType<ModuleProps> }>
  > = {
    console: Home,
    administration: Administration,
    insights: Insights,
  };
  const query = useQuery({
    queryKey: ["module", key],
    queryFn: loaders[key],
    staleTime: Infinity,
    gcTime: Infinity,
    retry: false,
    refetchOnWindowFocus: false,
  });
  if (query.isError) throw query.error;
  if (!query.data)
    return (
      <main id="main-content" tabIndex={-1} className="workbench">
        <p role="status">{t.loading}</p>
      </main>
    );
  const Page = query.data.default;
  return <Page key={context} integration={name === "integrations"} audit={name === "audit"} />;
}
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
  return (
    <QueryClientProvider client={code}>
      <Module name={module.key} context={session.profile?.context ?? ""} />
    </QueryClientProvider>
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
