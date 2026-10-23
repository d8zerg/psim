import { DomainStatus } from "./domain";
import type { Story, StoryDefault } from "@ladle/react";
import { Badge, Notice } from "../src";
import { Avatar, Progress, Skeleton, Spinner, Tag } from "../src/components/controls";
import { Example, useStoryText } from "./presentation";
export default {
  meta: {
    components: [
      "PriorityBadge",
      "AlarmStateBadge",
      "ConnectionIndicator",
      "SlaTimer",
      "AckButton",
      "CommandLifecycle",
      "AuditReasonField",
      "DeviceMarker",
      "AlarmBanner",
      "CameraTile",
      "Provenance",
      "Timestamp",
      "Badge",
      "Notice",
      "Avatar",
      "Progress",
      "Skeleton",
      "Spinner",
      "Tag",
    ],
  },
} satisfies StoryDefault;
export const States: Story = () => {
  const t = useStoryText();
  return (
    <>
      <Example name="Badge">
        <div className="catalogue-examples">
          {(["neutral", "info", "success", "warning", "danger"] as const).map((tone) => (
            <Badge key={tone} tone={tone}>
              {t("State", "Состояние")} {tone}
            </Badge>
          ))}
          <Badge tone="danger" emphasis="solid">
            P1 {t("Critical", "Критический")}
          </Badge>
        </div>
      </Example>
      <Example name="Notice">
        <Notice title={t("Current data", "Актуальные данные")} live={false}>
          {t("Connected to the incident service", "Есть связь с сервисом инцидентов")}
        </Notice>
        <Notice tone="success" live={false}>
          {t("Changes saved", "Изменения сохранены")}
        </Notice>
        <Notice tone="warning" title={t("Stale data", "Устаревшие данные")} live={false}>
          {t("Review the connection before sending a command", "Проверьте связь перед отправкой команды")}
        </Notice>
        <Notice tone="danger" live={false}>
          {t("Command failed", "Ошибка команды")}
        </Notice>
      </Example>
      <Example name="Avatar">
        <Avatar name={t("Duty operator", "Дежурный оператор")} />
      </Example>
      <Example name="Progress">
        <Progress label={t("Import progress", "Ход импорта")} value={64} />
        <Progress label={t("Preparing import", "Подготовка импорта")} />
      </Example>
      <Example name="Skeleton">
        <Skeleton label={t("Loading rows", "Загрузка строк")} />
        <Skeleton variant="card" label={t("Loading card", "Загрузка карточки")} />
        <Skeleton variant="text" label={t("Loading text", "Загрузка текста")} />
      </Example>
      <Example name="Spinner">
        <Spinner label={t("Updating data", "Обновление данных")} />
      </Example>
      <Example name="Tag">
        <Tag>{t("Access control", "Контроль доступа")}</Tag>
      </Example>
      <DomainStatus />
    </>
  );
};
