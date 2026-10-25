import { useInfiniteQuery } from "@tanstack/react-query";
import { Button, Card, Notice } from "@psim/design-system";
import { statusOf, type OperatorApi } from "./api";
import { queryDefaults, type Actions } from "./queries";
import { useStrings } from "../ui/strings";
export function NotificationsPanel({
  api,
  live,
  writable,
  actions,
  choose,
}: {
  readonly api: OperatorApi;
  readonly live: boolean;
  readonly writable: boolean;
  readonly actions: Actions;
  readonly choose: (id: string) => void;
}) {
  const t = useStrings();
  const notifications = useInfiniteQuery({
    ...queryDefaults,
    queryKey: ["notifications"],
    queryFn: ({ signal, pageParam }) => api.notifications(signal, pageParam),
    initialPageParam: "",
    getNextPageParam: (page) => (page.nextPageToken?.length ? page.nextPageToken : undefined),
    enabled: live,
  });
  if ([401, 403].includes(statusOf(notifications.error))) return <Notice tone="warning">{t.forbidden}</Notice>;
  return (
    <Card title={t.notified}>
      {notifications.isError && <Notice tone="warning">{t.detailsError}</Notice>}
      <ul>
        {notifications.data?.pages
          .flatMap((page) => page.notifications ?? [])
          .map((item) => (
            <li key={item.notificationId}>
              <p>
                {item.template} · {item.createdAt} · {item.incidentId}
              </p>
              {item.incidentId && (
                <Button
                  variant="secondary"
                  onClick={() => {
                    choose(item.incidentId ?? "");
                  }}
                >
                  {t.openIncident}
                </Button>
              )}
              <Button
                variant="secondary"
                disabled={!writable || notifications.isError || item.read}
                onClick={() => {
                  void actions.perform(() => api.readNotification(item.notificationId ?? "", item.version ?? "0"));
                }}
              >
                {t.markRead}
              </Button>
            </li>
          ))}
      </ul>
      {notifications.hasNextPage && (
        <Button
          disabled={!live || notifications.isFetchingNextPage}
          onClick={() => {
            void notifications.fetchNextPage();
          }}
        >
          {t.loadMore}
        </Button>
      )}
    </Card>
  );
}
