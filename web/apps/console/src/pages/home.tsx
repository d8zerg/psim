import { Button } from "@psim/design-system";

import { connectionLabel, useConnectionStore } from "../stores/connection";

export function HomePage() {
  const status = useConnectionStore((state) => state.status);
  return (
    <main className="mx-auto flex max-w-3xl flex-col gap-6 p-8">
      <h1 className="text-2xl font-semibold">PSIM Platform</h1>
      <p className="text-muted-foreground">
        Console shell. The incident feed, response scenarios and administration arrive in phase F8.
      </p>
      <p>
        Realtime channel: <span data-testid="connection-status">{connectionLabel(status)}</span>
      </p>
      <div>
        <Button variant="secondary" disabled>
          Incident feed
        </Button>
      </div>
    </main>
  );
}
