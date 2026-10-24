import { useEffect, useState, type ReactNode } from "react";
import { Link, useLocation } from "@tanstack/react-router";
import { Badge, Button, cardVariants } from "@psim/design-system";
import { AppLayout } from "../ui/layout";
import { useStrings } from "../ui/strings";
import { allowedModules } from "./modules";
import { useShell } from "./context";
export function ShellView({ children }: { readonly children: ReactNode }) {
  const { shell, session, feed } = useShell();
  const t = useStrings();
  const pathname = useLocation({ select: (location) => location.pathname });
  const [loginError, setLoginError] = useState(false);
  useEffect(() => {
    shell.start();
    const online = () => {
      shell.reconnect();
    };
    window.addEventListener("online", online);
    const logout = (event: StorageEvent) => {
      if (event.key === "psim.logout") shell.auth.invalidate(false);
    };
    window.addEventListener("storage", logout);
    return () => {
      window.removeEventListener("online", online);
      window.removeEventListener("storage", logout);
      shell.stop();
    };
  }, [shell]);
  useEffect(() => {
    document.getElementById("main-content")?.focus();
  }, [pathname, session.status]);
  useEffect(() => {
    const module = allowedModules(session.profile).find((entry) => entry.path === pathname);
    document.title = module ? `${t[module.key]} | PSIM` : "PSIM";
  }, [pathname, session.profile, t]);
  const available = allowedModules(session.profile);
  const status =
    session.status !== "authenticated" || !available.some((module) => module.key === "console")
      ? "offline"
      : feed.status;
  const labels: Record<string, string> = {
    open: t.online,
    offline: t.offline,
    idle: t.offline,
    stopped: t.offline,
    connecting: t.connecting,
    authenticating: t.connecting,
    reconnecting: t.reconnecting,
  };
  return (
    <AppLayout
      navigation={
        <>
          {available.map((module) => (
            <Link
              key={module.path}
              to={module.path}
              activeOptions={{ exact: true }}
              activeProps={{ "aria-current": "page" }}
            >
              {t[module.key]}
            </Link>
          ))}
        </>
      }
      account={
        <div className="shell-account">
          {(session.status !== "authenticated" || available.some((module) => module.key === "console")) && (
            <span
              className="shell-connection"
              data-testid="connection-status"
              data-status={status}
              role="status"
              aria-live="polite"
            >
              {[t.online, t.offline, t.connecting, t.reconnecting].map((label) => (
                <span className="shell-connection-reserve" aria-hidden="true" key={label}>
                  <Badge>{label}</Badge>
                </span>
              ))}
              <Badge tone={status === "open" ? "neutral" : "warning"}>{labels[status] ?? t.offline}</Badge>
            </span>
          )}
          {session.profile && (
            <>
              <span>{session.profile.name}</span>
              <Button
                variant="secondary"
                onClick={() => {
                  void shell.auth.logout();
                  try {
                    localStorage.setItem("psim.logout", String(Date.now()));
                  } catch {
                    /* Local session is already cleared. */
                  }
                }}
              >
                {t.signOut}
              </Button>
            </>
          )}
        </div>
      }
    >
      {session.status === "authenticated" ? (
        children
      ) : (
        <main id="main-content" tabIndex={-1} className="workbench max-w-5xl">
          <h1>{t.title}</h1>
          {session.status === "checking" ? (
            <p role="status">{t.checkingSession}</p>
          ) : (
            <section className={cardVariants()}>
              <h2>{t.workspace}</h2>
              <p>{t.introduction}</p>
              <Button
                onClick={() => {
                  setLoginError(false);
                  void shell.auth.login().catch(() => {
                    setLoginError(true);
                  });
                }}
              >
                {t.signIn}
              </Button>
              {(session.error || loginError) && <p role="alert">{t.signInError}</p>}
            </section>
          )}
        </main>
      )}
    </AppLayout>
  );
}
