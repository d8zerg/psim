import type { CommandState, IncidentState, Priority } from "@psim/realtime-client";
/** Exhaustive maps intentionally fail compilation when a displayed contract enum grows. */
const priorityEn = {
  0: "Unknown priority",
  1: "P1 · Critical",
  2: "P2 · High",
  3: "P3 · Normal",
  4: "P4 · Low",
} satisfies Record<Priority, string>;
const priorityRu = {
  0: "Приоритет неизвестен",
  1: "P1 · Критичный",
  2: "P2 · Высокий",
  3: "P3 · Обычный",
  4: "P4 · Низкий",
} satisfies Record<Priority, string>;
const incidentEn = {
  0: "Unknown",
  1: "New",
  2: "Acknowledged",
  3: "In progress",
  4: "Resolved",
  5: "Closed",
} satisfies Record<IncidentState, string>;
const incidentRu = {
  0: "Неизвестно",
  1: "Новый",
  2: "Принят",
  3: "В работе",
  4: "Разрешён",
  5: "Закрыт",
} satisfies Record<IncidentState, string>;
const commandEn = {
  0: "Unknown state",
  1: "Requested",
  2: "Awaiting approval",
  3: "Sent",
  4: "Acknowledged",
  5: "Executed",
  6: "Failed",
  7: "Rejected",
  8: "Timed out",
  9: "Cancelled",
  10: "Outcome unknown",
} satisfies Record<CommandState, string>;
const commandRu = {
  0: "Состояние неизвестно",
  1: "Запрошена",
  2: "Ожидает подтверждения",
  3: "Отправлена",
  4: "Получена устройством",
  5: "Выполнена",
  6: "Ошибка",
  7: "Отклонена",
  8: "Время истекло",
  9: "Отменена",
  10: "Исход неизвестен",
} satisfies Record<CommandState, string>;
export const protoLabels = {
  ru: { priority: priorityRu, incident: incidentRu, command: commandRu },
  en: { priority: priorityEn, incident: incidentEn, command: commandEn },
};
