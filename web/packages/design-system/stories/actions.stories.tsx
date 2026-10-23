import { AdvancedActions } from "./advanced";
import type { Story, StoryDefault } from "@ladle/react";
import { useState } from "react";
import { Button } from "../src";
import { ConfirmAction } from "../src/components/confirm-action";
import { ButtonGroup, IconButton } from "../src/components/controls";
import { Icon, domainIcons } from "../src/components/icons";
import { Example, useStoryText } from "./presentation";
export default {
  meta: {
    components: [
      "Dialog",
      "Sheet",
      "Popover",
      "Tooltip",
      "DropdownMenu",
      "ContextMenu",
      "Tabs",
      "SideNav",
      "Breadcrumbs",
      "Pagination",
      "CommandPalette",
      "NotificationCenter",
      "Toast",
      "Button",
      "ButtonGroup",
      "IconButton",
      "ConfirmAction",
      "Icon",
    ],
  },
} satisfies StoryDefault;
export const States: Story = () => {
  const t = useStoryText();
  const [sent, setSent] = useState(false);
  const [count, setCount] = useState(0);
  return (
    <>
      <Example name="Button">
        <div className="catalogue-examples">
          {(["primary", "secondary", "danger", "ghost"] as const).map((variant) => (
            <Button key={variant} variant={variant}>
              {t("Action", "Действие")} {variant}
            </Button>
          ))}
          <Button disabled>{t("Unavailable", "Недоступно")}</Button>
          <Button loading>{t("Sending", "Отправка")}</Button>
          <Button asChild variant="secondary">
            <a href="#catalogue-title">{t("Back to heading", "К заголовку")}</a>
          </Button>
        </div>
      </Example>
      <Example name="ButtonGroup">
        <ButtonGroup label={t("Incident actions", "Действия с инцидентом")}>
          <Button
            onClick={() => {
              setCount((value) => value + 1);
            }}
          >
            {t("Acknowledge", "Принять")}
          </Button>
          <Button variant="secondary">{t("Assign", "Назначить")}</Button>
        </ButtonGroup>
        <p role="status" data-testid="action-count">
          {count}
        </p>
      </Example>
      <Example name="IconButton">
        <div className="catalogue-examples">
          <IconButton label={t("Open camera", "Открыть камеру")} icon={domainIcons.camera} />
          <IconButton label={t("Unavailable camera", "Недоступная камера")} icon={domainIcons.camera} disabled />
        </div>
      </Example>
      <Example name="ConfirmAction">
        <ConfirmAction
          trigger={t("Send command", "Отправить команду")}
          title={t("Confirm command", "Подтвердите команду")}
          description={t(
            "Unlock the selected door after checking the reason.",
            "Разблокировать выбранную дверь после проверки основания.",
          )}
          details={<p>{t("Door", "Дверь")}: DOOR-42</p>}
          confirm={t("Confirm", "Подтвердить")}
          cancel={t("Cancel", "Отмена")}
          onConfirm={() => {
            setSent(true);
          }}
        />
        <p role="status" data-testid="command-result">
          {sent ? t("Sent", "Отправлено") : t("Not sent", "Не отправлено")}
        </p>
      </Example>
      <Example name="Icon">
        <div className="catalogue-examples">
          {Object.entries(domainIcons).map(([name, icon]) => (
            <span key={name}>
              <Icon icon={icon} label={name} />
              {name}
            </span>
          ))}
        </div>
      </Example>
      <AdvancedActions />
    </>
  );
};
