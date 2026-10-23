import { useState } from "react";
import {
  PriorityBadge,
  AlarmStateBadge,
  ConnectionIndicator,
  SlaTimer,
  AckButton,
  CommandLifecycle,
  AuditReasonField,
  DeviceMarker,
  AlarmBanner,
  CameraTile,
  Provenance,
  Timestamp,
} from "../src/components/domain";
import { normalAlarm } from "../src/alarm-model";
import { Example, useStoryText } from "./presentation";

export function DomainStatus() {
  const t = useStoryText();
  const [acknowledged, setAcknowledged] = useState(false);
  const [selected, setSelected] = useState(false);
  const labels = {
    normal: t("Normal", "Норма"),
    "active-unacknowledged": t("Active, unacknowledged", "Активна, не принята"),
    "active-acknowledged": t("Active, acknowledged", "Активна, принята"),
    "returned-unacknowledged": t("Returned, unacknowledged", "Возврат в норму, не принята"),
    visible: t("Visible", "Видима"),
    shelved: t("Shelved", "Отложена"),
    suppressed: t("Suppressed", "Подавлена"),
  };
  return (
    <>
      <Example name="PriorityBadge">
        <div className="flex flex-wrap gap-2">
          {(["P1", "P2", "P3", "P4"] as const).map((priority) => (
            <PriorityBadge
              key={priority}
              priority={priority}
              labels={{
                P1: t("Critical", "Критичный"),
                P2: t("High", "Высокий"),
                P3: t("Routine", "Обычный"),
                P4: t("Low", "Низкий"),
              }}
            />
          ))}
        </div>
      </Example>
      <Example name="AlarmStateBadge">
        <div className="grid gap-2">
          {(["normal", "active"] as const).flatMap((condition) =>
            [true, false].map((ack) => (
              <AlarmStateBadge
                key={`${condition}-${String(ack)}`}
                alarm={{ condition, acknowledged: ack, visibility: { kind: "visible" } }}
                labels={labels}
              />
            )),
          )}
          {(["shelved", "suppressed"] as const).map((kind) => (
            <AlarmStateBadge
              key={kind}
              alarm={{
                ...normalAlarm,
                visibility: { kind, actor: "operator-a", reason: t("Maintenance", "Обслуживание"), until: 60000 },
              }}
              labels={labels}
            />
          ))}
        </div>
      </Example>
      <Example name="ConnectionIndicator">
        {(["connected", "delayed", "disconnected"] as const).map((state) => (
          <ConnectionIndicator
            key={state}
            state={state}
            labels={{
              connected: t("Connected", "Связь установлена"),
              delayed: t("Delayed", "Задержка"),
              disconnected: t("Disconnected", "Нет связи"),
            }}
          />
        ))}
      </Example>
      <Example name="SlaTimer">
        <SlaTimer
          deadline={120000}
          now={60000}
          label={t("Remaining", "Осталось")}
          expiredLabel={t("Expired", "Срок нарушен")}
        />
        <SlaTimer
          deadline={30000}
          now={60000}
          label={t("Remaining", "Осталось")}
          expiredLabel={t("Expired", "Срок нарушен")}
        />
      </Example>
      <Example name="AckButton">
        <AckButton
          label={t("Acknowledge alarm", "Принять тревогу")}
          acknowledgedLabel={t("Alarm acknowledged", "Тревога принята")}
          acknowledged={acknowledged}
          onClick={() => {
            setAcknowledged(true);
          }}
        />
      </Example>
      <Example name="CommandLifecycle">
        <CommandLifecycle
          label={t("Command lifecycle", "Жизненный цикл команды")}
          command={{
            phase: "awaiting-approval",
            actor: "operator-a",
            target: "DOOR-42",
            reason: t("Verify entrance", "Проверить вход"),
          }}
          labels={{
            draft: "draft",
            "awaiting-approval": "awaiting-approval",
            approved: "approved",
            sent: "sent",
            acknowledged: "acknowledged",
            executed: "executed",
            failed: "failed",
            rejected: "rejected",
            "timed-out": "timed-out",
            cancelled: "cancelled",
            unknown: "unknown",
          }}
        />
      </Example>
      <Example name="AuditReasonField">
        <AuditReasonField
          label={t("Command reason", "Основание команды")}
          hint={t("Required before command approval", "Обязательно перед подтверждением команды")}
        />
      </Example>
      <Example name="DeviceMarker">
        <DeviceMarker
          label="SENSOR-42"
          state="alarm"
          stateLabel={t("Alarm", "Тревога")}
          selected={selected}
          onSelect={() => {
            setSelected((value) => !value);
          }}
        />
      </Example>
      <Example name="AlarmBanner">
        <AlarmBanner
          label={t("Critical alarms", "Критичные тревоги")}
          count={42}
          countLabel={t("Unacknowledged", "Не приняты")}
          priority="P1"
        />
      </Example>
      <Example name="CameraTile">
        <CameraTile
          label="CAMERA-42"
          state="offline"
          stateLabel={t("No connection", "Нет связи")}
          openLabel={t("Open video", "Открыть видео")}
          onOpen={() => {
            setSelected(true);
          }}
        />
      </Example>
      <Example name="Provenance">
        <Provenance
          label={t("Device state", "Состояние устройства")}
          value={t("Open", "Открыто")}
          stale
          staleLabel={t("Last known value", "Последнее известное значение")}
          whyLabel={t("Why this value", "Почему это значение")}
          sources={[
            { id: "SENSOR-42", label: t("Adapter event", "Событие адаптера"), value: "OPEN", sequence: "event:42" },
          ]}
        />
      </Example>
      <Example name="Timestamp">
        <Timestamp timestamp={1704110400000} now={1704110460000} locale="en" timeZone="UTC" />
      </Example>
    </>
  );
}
