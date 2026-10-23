import type {
  DataQuality,
  ResourceState,
  LocationKind,
  ParameterType,
  MaintenanceState,
  ActorKind,
  Severity,
  Priority,
  EventClass,
  IncidentState,
  PrioritySource,
  Resolution,
  TimelineEntryKind,
  Stream,
  CommandState,
  RunChange,
  NotificationState,
  PlanState,
  NotificationChannel,
  RunState,
  StepState,
} from "@psim/realtime-client/enums";
/** Exhaustive localized labels are maintained explicitly: contract additions must be reviewed. */
const DataQualityen = {
  0: "Unspecified",
  1: "Actual",
  2: "Stale",
  3: "Unknown",
} satisfies Record<DataQuality, string>;
const DataQualityru = {
  0: "Не указано",
  1: "Актуально",
  2: "Устарело",
  3: "Неизвестно",
} satisfies Record<DataQuality, string>;
const ResourceStateen = {
  0: "Unspecified",
  1: "Active",
  2: "Disabled",
  3: "Archived",
} satisfies Record<ResourceState, string>;
const ResourceStateru = {
  0: "Не указано",
  1: "Активно",
  2: "Отключено",
  3: "Архив",
} satisfies Record<ResourceState, string>;
const LocationKinden = {
  0: "Unspecified",
  1: "Site",
  2: "Building",
  3: "Floor",
  4: "Zone",
} satisfies Record<LocationKind, string>;
const LocationKindru = {
  0: "Не указано",
  1: "Площадка",
  2: "Здание",
  3: "Этаж",
  4: "Зона",
} satisfies Record<LocationKind, string>;
const ParameterTypeen = {
  0: "Unspecified",
  1: "String",
  2: "Integer",
  3: "Number",
  4: "Boolean",
  5: "Duration",
} satisfies Record<ParameterType, string>;
const ParameterTyperu = {
  0: "Не указано",
  1: "Строка",
  2: "Целое число",
  3: "Число",
  4: "Логическое значение",
  5: "Длительность",
} satisfies Record<ParameterType, string>;
const MaintenanceStateen = {
  0: "Unspecified",
  1: "Scheduled",
  2: "Active",
  3: "Completed",
  4: "Cancelled",
} satisfies Record<MaintenanceState, string>;
const MaintenanceStateru = {
  0: "Не указано",
  1: "Запланировано",
  2: "Активно",
  3: "Завершено",
  4: "Отменено",
} satisfies Record<MaintenanceState, string>;
const ActorKinden = {
  0: "Unspecified",
  1: "User",
  2: "Service",
  3: "Connector",
  4: "System",
} satisfies Record<ActorKind, string>;
const ActorKindru = {
  0: "Не указано",
  1: "Пользователь",
  2: "Сервис",
  3: "Коннектор",
  4: "Система",
} satisfies Record<ActorKind, string>;
const Severityen = {
  0: "Unspecified",
  1: "Info",
  2: "Low",
  3: "Medium",
  4: "High",
  5: "Critical",
} satisfies Record<Severity, string>;
const Severityru = {
  0: "Не указано",
  1: "Информация",
  2: "Низкий",
  3: "Средний",
  4: "Высокий",
  5: "Критично",
} satisfies Record<Severity, string>;
const Priorityen = {
  0: "Unspecified",
  1: "P1",
  2: "P2",
  3: "P3",
  4: "P4",
} satisfies Record<Priority, string>;
const Priorityru = {
  0: "Не указано",
  1: "P1",
  2: "P2",
  3: "P3",
  4: "P4",
} satisfies Record<Priority, string>;
const EventClassen = {
  0: "Unspecified",
  1: "Alarm",
  2: "Warning",
  3: "Fault",
  4: "Restore",
  5: "State",
  6: "Info",
} satisfies Record<EventClass, string>;
const EventClassru = {
  0: "Не указано",
  1: "Тревога",
  2: "Предупреждение",
  3: "Неисправность",
  4: "Восстановление",
  5: "Состояние",
  6: "Информация",
} satisfies Record<EventClass, string>;
const IncidentStateen = {
  0: "Unspecified",
  1: "New",
  2: "Acknowledged",
  3: "In progress",
  4: "Resolved",
  5: "Closed",
} satisfies Record<IncidentState, string>;
const IncidentStateru = {
  0: "Не указано",
  1: "Новое",
  2: "Принято",
  3: "В работе",
  4: "Разрешено",
  5: "Закрыто",
} satisfies Record<IncidentState, string>;
const PrioritySourceen = {
  0: "Unspecified",
  1: "Computed",
  2: "Manual",
} satisfies Record<PrioritySource, string>;
const PrioritySourceru = {
  0: "Не указано",
  1: "Вычислено",
  2: "Вручную",
} satisfies Record<PrioritySource, string>;
const Resolutionen = {
  0: "Unspecified",
  1: "Confirmed",
  2: "False alarm",
  3: "Test",
  4: "Duplicate",
} satisfies Record<Resolution, string>;
const Resolutionru = {
  0: "Не указано",
  1: "Подтверждено",
  2: "Ложная тревога",
  3: "Тест",
  4: "Дубликат",
} satisfies Record<Resolution, string>;
const TimelineEntryKinden = {
  0: "Unspecified",
  1: "State changed",
  2: "Signal attached",
  3: "Priority changed",
  4: "Assigned",
  5: "Escalated",
  6: "Comment",
  7: "Link",
  8: "Step",
  9: "Command",
  10: "Notification",
} satisfies Record<TimelineEntryKind, string>;
const TimelineEntryKindru = {
  0: "Не указано",
  1: "Состояние изменено",
  2: "Сигнал прикреплён",
  3: "Приоритет изменён",
  4: "Назначено",
  5: "Эскалировано",
  6: "Комментарий",
  7: "Связь",
  8: "Шаг",
  9: "Команда",
  10: "Уведомление",
} satisfies Record<TimelineEntryKind, string>;
const Streamen = {
  0: "Unspecified",
  1: "Incidents",
  2: "Incident",
  3: "Location status",
} satisfies Record<Stream, string>;
const Streamru = {
  0: "Не указано",
  1: "Инциденты",
  2: "Инцидент",
  3: "Состояние местоположения",
} satisfies Record<Stream, string>;
const CommandStateen = {
  0: "Unspecified",
  1: "Requested",
  2: "Awaiting approval",
  3: "Sent",
  4: "Acknowledged",
  5: "Executed",
  6: "Failed",
  7: "Rejected",
  8: "Timed out",
  9: "Cancelled",
  10: "Unknown",
} satisfies Record<CommandState, string>;
const CommandStateru = {
  0: "Не указано",
  1: "Запрошено",
  2: "Ожидает подтверждения",
  3: "Отправлено",
  4: "Принято",
  5: "Исполнено",
  6: "Ошибка",
  7: "Отклонено",
  8: "Время истекло",
  9: "Отменено",
  10: "Неизвестно",
} satisfies Record<CommandState, string>;
const RunChangeen = {
  0: "Unspecified",
  1: "Run started",
  2: "Step activated",
  3: "Step completed",
  4: "Step skipped",
  5: "Step failed",
  6: "Mandatory steps changed",
  7: "Run completed",
  8: "Run cancelled",
} satisfies Record<RunChange, string>;
const RunChangeru = {
  0: "Не указано",
  1: "Запуск начат",
  2: "Шаг активирован",
  3: "Шаг завершён",
  4: "Шаг пропущен",
  5: "Ошибка шага",
  6: "Обязательные шаги изменены",
  7: "Запуск завершён",
  8: "Запуск отменён",
} satisfies Record<RunChange, string>;
const NotificationStateen = {
  0: "Unspecified",
  1: "Pending",
  2: "Sent",
  3: "Delivered",
  4: "Failed",
  5: "Aggregated",
  6: "Unknown",
} satisfies Record<NotificationState, string>;
const NotificationStateru = {
  0: "Не указано",
  1: "Ожидает",
  2: "Отправлено",
  3: "Доставлено",
  4: "Ошибка",
  5: "Агрегировано",
  6: "Неизвестно",
} satisfies Record<NotificationState, string>;
const PlanStateen = {
  0: "Unspecified",
  1: "Draft",
  2: "Published",
  3: "Archived",
} satisfies Record<PlanState, string>;
const PlanStateru = {
  0: "Не указано",
  1: "Черновик",
  2: "Опубликовано",
  3: "Архив",
} satisfies Record<PlanState, string>;
const NotificationChannelen = {
  0: "Unspecified",
  1: "In app",
  2: "Email",
  3: "Webhook",
} satisfies Record<NotificationChannel, string>;
const NotificationChannelru = {
  0: "Не указано",
  1: "В приложении",
  2: "Электронная почта",
  3: "Вебхук",
} satisfies Record<NotificationChannel, string>;
const RunStateen = {
  0: "Unspecified",
  1: "Running",
  2: "Completed",
  3: "Cancelled",
} satisfies Record<RunState, string>;
const RunStateru = {
  0: "Не указано",
  1: "Выполняется",
  2: "Завершено",
  3: "Отменено",
} satisfies Record<RunState, string>;
const StepStateen = {
  0: "Unspecified",
  1: "Pending",
  2: "Active",
  3: "Completed",
  4: "Skipped",
  5: "Failed",
  6: "Cancelled",
} satisfies Record<StepState, string>;
const StepStateru = {
  0: "Не указано",
  1: "Ожидает",
  2: "Активно",
  3: "Завершено",
  4: "Пропущено",
  5: "Ошибка",
  6: "Отменено",
} satisfies Record<StepState, string>;
export const contractLabels = {
  en: {
    DataQuality: DataQualityen,
    ResourceState: ResourceStateen,
    LocationKind: LocationKinden,
    ParameterType: ParameterTypeen,
    MaintenanceState: MaintenanceStateen,
    ActorKind: ActorKinden,
    Severity: Severityen,
    Priority: Priorityen,
    EventClass: EventClassen,
    IncidentState: IncidentStateen,
    PrioritySource: PrioritySourceen,
    Resolution: Resolutionen,
    TimelineEntryKind: TimelineEntryKinden,
    Stream: Streamen,
    CommandState: CommandStateen,
    RunChange: RunChangeen,
    NotificationState: NotificationStateen,
    PlanState: PlanStateen,
    NotificationChannel: NotificationChannelen,
    RunState: RunStateen,
    StepState: StepStateen,
  },
  ru: {
    DataQuality: DataQualityru,
    ResourceState: ResourceStateru,
    LocationKind: LocationKindru,
    ParameterType: ParameterTyperu,
    MaintenanceState: MaintenanceStateru,
    ActorKind: ActorKindru,
    Severity: Severityru,
    Priority: Priorityru,
    EventClass: EventClassru,
    IncidentState: IncidentStateru,
    PrioritySource: PrioritySourceru,
    Resolution: Resolutionru,
    TimelineEntryKind: TimelineEntryKindru,
    Stream: Streamru,
    CommandState: CommandStateru,
    RunChange: RunChangeru,
    NotificationState: NotificationStateru,
    PlanState: PlanStateru,
    NotificationChannel: NotificationChannelru,
    RunState: RunStateru,
    StepState: StepStateru,
  },
};
