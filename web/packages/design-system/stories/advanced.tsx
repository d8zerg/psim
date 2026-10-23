import { CollectionBenchmark } from "./benchmark";
import { useState } from "react";
import { Button } from "../src";
import {
  Dialog,
  Sheet,
  Popover,
  Tooltip,
  DropdownMenu,
  ContextMenu,
  NotificationCenter,
  Toast,
} from "../src/components/overlays";
import { Tabs, SideNav, Breadcrumbs, Pagination, CommandPalette } from "../src/components/navigation";
import { Combobox, MultiSelect, DatePeriodPicker, FileUpload, ValidatedForm } from "../src/components/forms";
import { DataTable, Tree, FloorPlan, TrendChart } from "../src/components/collections";
import { Example, useStoryText } from "./presentation";

export function AdvancedActions() {
  const t = useStoryText();
  const [result, setResult] = useState("");
  const [notices, setNotices] = useState([{ id: "one", text: t("Source recovered", "Источник восстановлен") }]);
  const [page, setPage] = useState(1);
  const actions = [
    {
      id: "inspect",
      label: t("Inspect", "Просмотр"),
      onSelect: () => {
        setResult("inspect");
      },
    },
    {
      id: "forbidden",
      label: t("Unavailable", "Недоступно"),
      disabled: true,
      onSelect: () => {
        setResult("forbidden");
      },
    },
  ];
  const dialog = {
    title: t("Incident details", "Подробности инцидента"),
    description: t("Source SENSOR-42", "Источник SENSOR-42"),
    closeLabel: t("Close", "Закрыть"),
    children: <p>INC-1042</p>,
  };
  return (
    <>
      <Example name="Dialog">
        <Dialog {...dialog} trigger={<Button variant="secondary">{t("Open dialog", "Открыть диалог")}</Button>} />
      </Example>
      <Example name="Sheet">
        <Sheet {...dialog} trigger={<Button variant="secondary">{t("Open sheet", "Открыть панель")}</Button>} />
      </Example>
      <Example name="Popover">
        <Popover
          trigger={<Button variant="secondary">{t("Source details", "Сведения об источнике")}</Button>}
          label={t("Provenance", "Происхождение")}
        >
          <p>SENSOR-42 - Kafka - projection</p>
        </Popover>
      </Example>
      <Example name="Tooltip">
        <Tooltip text={t("Read source evidence", "Посмотреть сведения источника")}>
          <Button variant="secondary">{t("Evidence", "Основание")}</Button>
        </Tooltip>
      </Example>
      <Example name="DropdownMenu">
        <DropdownMenu
          label={t("Record actions", "Действия с записью")}
          trigger={<Button variant="secondary">{t("Open menu", "Открыть меню")}</Button>}
          actions={actions}
        />
      </Example>
      <Example name="ContextMenu">
        <ContextMenu label={t("Incident context", "Контекст инцидента")} actions={actions}>
          INC-1042
        </ContextMenu>
      </Example>
      <Example name="Tabs">
        <Tabs
          label={t("Incident sections", "Разделы инцидента")}
          items={[
            { id: "summary", label: t("Summary", "Сводка"), content: "INC-1042" },
            { id: "history", label: t("History", "История"), content: "SENSOR-42" },
          ]}
        />
      </Example>
      <Example name="SideNav">
        <SideNav
          label={t("Modules", "Модули")}
          userRole="operator"
          items={[
            {
              id: "alarms",
              label: t("Alarms", "Тревоги"),
              href: "#catalogue-title",
              roles: ["operator"],
              current: true,
            },
            { id: "admin", label: t("Administration", "Администрирование"), href: "#admin", roles: ["admin"] },
          ]}
        />
      </Example>
      <Example name="Breadcrumbs">
        <Breadcrumbs
          label={t("Location", "Расположение")}
          items={[
            { label: t("Site", "Площадка"), href: "#site" },
            { label: t("Floor", "Этаж"), href: "#floor" },
          ]}
        />
      </Example>
      <Example name="Pagination">
        <Pagination
          label={t("Pages", "Страницы")}
          previousLabel={t("Previous", "Назад")}
          nextLabel={t("Next", "Далее")}
          page={page}
          pages={3}
          onPageChange={setPage}
        />
      </Example>
      <Example name="CommandPalette">
        <CommandPalette
          label={t("Command palette", "Палитра команд")}
          searchLabel={t("Find action", "Найти действие")}
          closeLabel={t("Close", "Закрыть")}
          actions={actions}
        />
      </Example>
      <Example name="NotificationCenter">
        <NotificationCenter
          label={t("Notifications", "Уведомления")}
          dismissLabel={t("Dismiss", "Скрыть")}
          items={notices}
          onDismiss={(id) => {
            setNotices((items) => items.filter((item) => item.id !== id));
          }}
        />
      </Example>
      <Example name="Toast">
        <Toast
          message={t("Saved locally", "Сохранено локально")}
          dismissLabel={t("Dismiss message", "Скрыть сообщение")}
        />
      </Example>
      <output data-testid="menu-result">{result}</output>
    </>
  );
}
const choices = Array.from({ length: 1000 }, (_, index) => ({
  value: `SENSOR-${String(index)}`,
  label: `SENSOR-${String(index)}`,
}));
export function AdvancedForms() {
  const t = useStoryText();
  const [period, setPeriod] = useState({ start: "2024-01-01T12:00", end: "2024-01-01T14:00", timeZone: "UTC" });
  const [saved, setSaved] = useState("");
  return (
    <>
      <Example name="Combobox">
        <Combobox
          label={t("Search sensor", "Найти датчик")}
          options={choices}
          labels={{
            loading: t("Loading", "Загрузка"),
            empty: t("No matches", "Нет совпадений"),
            error: t("Search failed", "Ошибка поиска"),
          }}
        />
      </Example>
      <Example name="MultiSelect">
        <MultiSelect
          label={t("Selected priorities", "Выбранные приоритеты")}
          options={[
            { value: "P1", label: "P1" },
            { value: "P2", label: "P2" },
          ]}
          defaultValue={["P1"]}
        />
      </Example>
      <Example name="DatePeriodPicker">
        <DatePeriodPicker
          labels={{
            start: t("From", "Начало"),
            end: t("Until", "Конец"),
            timeZone: t("Time zone", "Часовой пояс"),
            invalid: t("End precedes start", "Конец раньше начала"),
          }}
          value={period}
          onValueChange={setPeriod}
          zones={["UTC", "Europe/Moscow"]}
        />
      </Example>
      <Example name="FileUpload">
        <FileUpload
          label={t("Import catalogue", "Импорт каталога")}
          cancelLabel={t("Cancel upload", "Отменить загрузку")}
          errorLabel={t("Upload failed", "Ошибка загрузки")}
          reportLabel={t("Row report", "Отчёт по строкам")}
          upload={(_file, progress) => {
            progress(100);
            return Promise.resolve([{ row: 2, message: t("Unknown zone", "Неизвестная зона") }]);
          }}
        />
      </Example>
      <Example name="ValidatedForm">
        <ValidatedForm
          fields={[
            {
              name: "reason",
              label: t("Audit reason", "Основание аудита"),
              requiredMessage: t("Reason required", "Укажите основание"),
            },
          ]}
          submitLabel={t("Save reason", "Сохранить основание")}
          errorLabel={t("Correct form errors", "Исправьте ошибки формы")}
          onSubmit={(value) => {
            setSaved(value.reason ?? "");
          }}
        />
        <output data-testid="form-result">{saved}</output>
      </Example>
    </>
  );
}
const records = Array.from({ length: 500 }, (_, index) => ({
  id: `INC-${String(index).padStart(4, "0")}`,
  source: `SENSOR-${String(index % 20)}`,
  priority: index % 5 === 0 ? "P1" : "P3",
}));
export function AdvancedData() {
  const t = useStoryText();
  const [selected, setSelected] = useState("");
  const [rows, setRows] = useState(records);
  const [bulk, setBulk] = useState<readonly string[]>([]);
  if (new URLSearchParams(location.search).get("benchmark") === "true") return <CollectionBenchmark />;
  return (
    <>
      <Example name="DataTable">
        <Button
          variant="secondary"
          onClick={() => {
            setRows((previous) => [{ id: "INC-new", source: "SENSOR-new", priority: "P2" }, ...previous]);
          }}
        >
          {t("Insert row", "Вставить строку")}
        </Button>
        <DataTable
          label={t("Virtual incidents", "Виртуальная таблица инцидентов")}
          data={rows}
          columns={[
            { accessorKey: "id", header: "ID" },
            { accessorKey: "source", header: t("Source", "Источник") },
            { accessorKey: "priority", header: t("Priority", "Приоритет") },
          ]}
          labels={{
            search: t("Filter records", "Фильтр записей"),
            select: t("Select record", "Выбрать запись"),
            columns: t("Columns", "Столбцы"),
            pin: t("Pin", "Закрепить"),
            bulk: t("Bulk action", "Групповое действие"),
            empty: t("No records", "Нет записей"),
          }}
          onBulkAction={setBulk}
        />
        <output data-testid="bulk-result">{bulk.join(", ")}</output>
      </Example>
      <Example name="Tree">
        <Tree
          label={t("Resource tree", "Дерево ресурсов")}
          nodes={[{ id: "site", label: t("Site", "Площадка"), hasChildren: true }]}
          loadChildren={() =>
            Promise.resolve([
              { id: "floor", label: t("Floor", "Этаж"), children: [{ id: "sensor", label: "SENSOR-42" }] },
            ])
          }
          loadingLabel={t("Loading children", "Загрузка дочерних элементов")}
          errorLabel={t("Loading failed; expand to retry", "Ошибка загрузки; раскройте повторно")}
          onSelect={setSelected}
        />
      </Example>
      <Example name="FloorPlan">
        <FloorPlan
          label={t("Floor plan", "План этажа")}
          markers={[
            { id: "sensor", label: "SENSOR-42", x: 25, y: 35, state: "alarm" },
            { id: "door", label: "DOOR-42", x: 65, y: 65, state: "normal" },
          ]}
          selected={selected}
          onSelect={setSelected}
          labels={{
            zoomIn: t("Zoom in", "Увеличить"),
            zoomOut: t("Zoom out", "Уменьшить"),
            reset: t("Reset zoom", "Сбросить масштаб"),
            legend: t("Legend and marker list", "Легенда и список маркеров"),
            normal: t("Normal", "Норма"),
            alarm: t("Alarm", "Тревога"),
            offline: t("Offline", "Нет связи"),
          }}
        />
      </Example>
      <Example name="TrendChart">
        <TrendChart
          label={t("Alarm trend", "Динамика тревог")}
          valueLabel={t("Count", "Количество")}
          values={[
            { label: "00-10", value: 2 },
            { label: "10-20", value: 9 },
            { label: "20-30", value: 4 },
          ]}
        />
      </Example>
    </>
  );
}
