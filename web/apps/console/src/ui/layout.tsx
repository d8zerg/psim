import { useLayoutEffect, type ReactNode } from "react";
import { DisplaySettings } from "./settings";
import { usePreferences } from "./preferences";
import { useStrings } from "./strings";
export function AppLayout({ children, design = false }: { readonly children: ReactNode; readonly design?: boolean }) {
  const t = useStrings();
  const mode = usePreferences((state) => state.mode);
  useLayoutEffect(() => {
    if (!design) return;
    const navigation = document.querySelector<HTMLElement>(".section-nav");
    if (!navigation) return;
    const update = () => {
      const gap = Number.parseFloat(getComputedStyle(navigation).getPropertyValue("--navigation-focus-gap"));
      document.documentElement.style.setProperty(
        "--navigation-clearance",
        `${String(navigation.getBoundingClientRect().height + (Number.isFinite(gap) ? gap : 0))}px`,
      );
    };
    update();
    const observer = typeof ResizeObserver === "undefined" ? undefined : new ResizeObserver(update);
    observer?.observe(navigation);
    return () => {
      observer?.disconnect();
      document.documentElement.style.removeProperty("--navigation-clearance");
    };
  }, [design]);
  return (
    <>
      <a className="skip-link" href="#main-content">
        {t.skip}
      </a>
      <header className="app-header">
        <a href="/" className="brand" aria-label={t.title}>
          <span className="brand-mark" aria-hidden="true">
            <svg viewBox="0 0 16 16" width="1em" height="1em" fill="none" stroke="currentColor" aria-hidden="true">
              <path d="M5 12V4h4a2 2 0 0 1 0 4H5" strokeWidth="2" />
            </svg>
          </span>
          PSIM
        </a>
        <nav className="app-nav" aria-label={t.console}>
          <a href="/" aria-current={design ? undefined : "page"}>
            {t.console}
          </a>
          {design && (
            <a href="/design-system.html" aria-current="page">
              {t.library}
            </a>
          )}
        </nav>
        {mode !== "normal" && (
          <span className="mode-label">{{ crisis: t.crisis, training: t.training, wall: t.wall }[mode]}</span>
        )}
        <DisplaySettings design={design} />
      </header>
      {children}
    </>
  );
}
