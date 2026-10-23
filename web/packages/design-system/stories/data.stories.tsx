import { AdvancedData } from "./advanced";
import type { Story, StoryDefault } from "@ladle/react";
import { Card, DataRegion, EmptyState, TableContainer } from "../src";
import { DescriptionList, Kbd, Stat, Stepper, Timeline } from "../src/components/controls";
import { Example, useStoryText } from "./presentation";
export default {
  meta: {
    components: [
      "DataTable",
      "Tree",
      "FloorPlan",
      "TrendChart",
      "Card",
      "DataRegion",
      "EmptyState",
      "TableContainer",
      "DescriptionList",
      "Kbd",
      "Stat",
      "Stepper",
      "Timeline",
    ],
  },
} satisfies StoryDefault;
export const States: Story = () => {
  const t = useStoryText();
  const labels = {
    loading: t("Loading data", "Загрузка данных"),
    refreshing: t("Updating. Current rows remain available.", "Обновление. Текущие строки остаются доступны."),
    stale: t("Last known data. Check the connection.", "Последние известные данные. Проверьте связь."),
  };
  return (
    <>
      <Example name="Card">
        <Card
          title={t("Incident", "Инцидент")}
          headingLevel={3}
          description={t("Source and current state", "Источник и текущее состояние")}
        >
          <p>INC-1042</p>
        </Card>
      </Example>
      <Example name="DataRegion">
        <DataRegion loading hasData={false} labels={labels}>
          <p>INC-1042</p>
        </DataRegion>
        <DataRegion loading hasData labels={labels}>
          <p>INC-1042</p>
        </DataRegion>
        <DataRegion loading={false} hasData labels={labels} error={t("Connection lost", "Связь потеряна")}>
          <p>INC-1042</p>
        </DataRegion>
      </Example>
      <Example name="EmptyState">
        <EmptyState
          title={t("No incidents", "Нет инцидентов")}
          description={t("New alarms will appear here", "Новые тревоги появятся здесь")}
        />
      </Example>
      <Example name="TableContainer">
        <TableContainer label={t("Incident records", "Записи инцидентов")}>
          <table className="ds-table">
            <caption>{t("Recent incidents", "Последние инциденты")}</caption>
            <thead>
              <tr>
                <th scope="col">ID</th>
                <th scope="col">{t("Priority", "Приоритет")}</th>
              </tr>
            </thead>
            <tbody>
              <tr>
                <td>INC-1042</td>
                <td>P1</td>
              </tr>
            </tbody>
          </table>
        </TableContainer>
      </Example>
      <Example name="DescriptionList">
        <DescriptionList
          items={[
            { label: t("Source", "Источник"), value: "SENSOR-42" },
            { label: t("Owner", "Ответственный"), value: t("Duty operator", "Дежурный оператор") },
          ]}
        />
      </Example>
      <Example name="Kbd">
        <p>
          {t("Command palette", "Палитра команд")}: <Kbd>Ctrl</Kbd> + <Kbd>K</Kbd>
        </p>
      </Example>
      <Example name="Stat">
        <Stat
          label={t("Active incidents", "Активные инциденты")}
          value="42"
          description={t("Within the selected scope", "В выбранной области")}
        />
      </Example>
      <Example name="Stepper">
        <Stepper
          label={t("Response plan", "Сценарий реагирования")}
          steps={[
            t("Verify source", "Проверить источник"),
            t("Contact operator", "Связаться с оператором"),
            t("Review result", "Проверить результат"),
          ]}
          current={1}
        />
      </Example>
      <Example name="Timeline">
        <Timeline
          label={t("Incident timeline", "Хронология инцидента")}
          items={[
            { id: "created", time: "14:32:10 UTC", text: t("Incident created", "Инцидент создан") },
            { id: "ack", time: "14:32:42 UTC", text: t("Acknowledged by operator", "Принят оператором") },
          ]}
        />
      </Example>
      <AdvancedData />
    </>
  );
};
