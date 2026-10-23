import { lazy, Suspense, useState } from "react";
import { Button, Notice } from "@psim/design-system";
import { useDemoStrings } from "../ui/strings";
const Operations = lazy(() => import("./operations"));
/** Heavy simulation widgets load only after an explicit request. */
export function OperationalDemo() {
  const t = useDemoStrings();
  const [open, setOpen] = useState(false);
  return (
    <section id="operations" aria-label={t.opTitle} className="scenario">
      <h2>{t.opTitle}</h2>
      <p>{t.opHint}</p>
      {open ? (
        <Suspense fallback={<Notice>{t.loading}</Notice>}>
          <Operations />
        </Suspense>
      ) : (
        <Button
          variant="secondary"
          onClick={() => {
            setOpen(true);
          }}
        >
          {t.opLoad}
        </Button>
      )}
    </section>
  );
}
