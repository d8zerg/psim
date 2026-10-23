import { AdvancedForms } from "./advanced";
import type { Story, StoryDefault } from "@ladle/react";
import { useState } from "react";
import { Field, SelectField, Switch } from "../src";
import { Checkbox, NumberInput, RadioGroup, Slider, Textarea } from "../src/components/controls";
import { Example, useStoryText } from "./presentation";
export default {
  meta: {
    components: [
      "Combobox",
      "MultiSelect",
      "DatePeriodPicker",
      "FileUpload",
      "ValidatedForm",
      "Field",
      "SelectField",
      "Switch",
      "Checkbox",
      "NumberInput",
      "RadioGroup",
      "Slider",
      "Textarea",
    ],
  },
} satisfies StoryDefault;
export const States: Story = () => {
  const t = useStoryText();
  const [enabled, setEnabled] = useState(false);
  const [choice, setChoice] = useState("operator");
  return (
    <>
      <Example name="Field">
        <Field
          label={t("Device name", "Название устройства")}
          hint={t("Shown in the incident feed", "Показывается в ленте инцидентов")}
          defaultValue="SENSOR-42"
        />
        <Field label={t("Reason", "Основание")} required error={t("Enter a reason", "Укажите основание")} />
        <Field label={t("Device identifier", "Идентификатор устройства")} disabled defaultValue="DEVICE-42" />
      </Example>
      <Example name="SelectField">
        <SelectField label={t("Priority", "Приоритет")} defaultValue="P2">
          <option>P1</option>
          <option>P2</option>
          <option>P3</option>
          <option>P4</option>
        </SelectField>
        <SelectField label={t("Unavailable selection", "Недоступный выбор")} disabled>
          <option>{t("No permission", "Нет доступа")}</option>
        </SelectField>
      </Example>
      <Example name="Switch">
        <Switch label={t("Sound alerts", "Звуковые оповещения")} checked={enabled} onCheckedChange={setEnabled} />
        <Switch
          label={t("Fixed preference", "Фиксированная настройка")}
          checked
          disabled
          onCheckedChange={setEnabled}
        />
      </Example>
      <Example name="Checkbox">
        <Checkbox label={t("Include resolved incidents", "Включить решённые инциденты")} defaultChecked />
        <Checkbox label={t("Unavailable filter", "Недоступный фильтр")} disabled />
      </Example>
      <Example name="NumberInput">
        <NumberInput label={t("Volume", "Громкость")} min={0} max={100} defaultValue={50} />
      </Example>
      <Example name="RadioGroup">
        <RadioGroup
          label={t("Recipient", "Получатель")}
          value={choice}
          onValueChange={setChoice}
          options={[
            { value: "operator", label: t("Operator", "Оператор") },
            { value: "supervisor", label: t("Supervisor", "Старший смены") },
          ]}
        />
      </Example>
      <Example name="Slider">
        <Slider label={t("Plan scale", "Масштаб плана")} min={25} max={200} defaultValue={100} />
      </Example>
      <Example name="Textarea">
        <Textarea
          label={t("Audit note", "Запись аудита")}
          hint={t("Explain the operational context", "Опишите оперативную обстановку")}
          defaultValue={t("Verify the selected entrance", "Проверить выбранный вход")}
        />
        <Textarea
          label={t("Required note", "Обязательная запись")}
          error={t("Enter a note", "Введите запись")}
          required
        />
      </Example>
      <AdvancedForms />
    </>
  );
};
