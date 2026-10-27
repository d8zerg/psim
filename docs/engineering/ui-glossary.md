# Interface Terms ru/en

<!-- language-links -->
[English](ui-glossary.md) | [Русский](../ru/engineering/ui-glossary.md)
<!-- /language-links -->

Similar concepts use the same labels in the product, the showcase, and reports. User-defined names, identifiers, and messages from external devices are not translated as dictionary keys.

| English | Russian | Meaning |
|---|---|---|
| `Event` | `Событие` | A message received about a state change or action |
| `Signal` | `Сигнал` | The result of event processing according to a rule |
| `Alarm` | `Тревога` | A condition that deviates from its normal state, along with acknowledgement and visibility |
| `Incident` | `Инцидент` | A record of related signal processing and actions |
| `Priority` | `Приоритет` | Criticality P1-P4; not the current state |
| `Active` | `Активна` | The alarm condition continues to act |
| `Returned to normal` | `Возврат в норму` | The condition has stopped acting; confirmation is considered separately |
| `Acknowledge alarm` | `Подтвердить тревогу` | Confirm that the alarm was detected; do not remove its cause |
| `Acknowledge incident` | `Принять инцидент` | Start processing the incident according to its lifecycle |
| `Shelve` | `Отложить тревогу` | Temporarily hide an alarm with a reason and deadline |
| `Suppress` | `Подавить тревогу` | Exclude notifications based on explicit rules and reasons |
| `Owner` | `Ответственный` | The user responsible for processing |
| `Responsibility transfer` | `Передача ответственности` | The current owner remains until the recipient agrees |
| `Command` | `Команда` | A request to change the device’s state |
| `Requested` | `Запрошена` | The command has been created but not yet executed |
| `Awaiting approval` | `Ожидает подтверждения` | Requires approval from another authorized user |
| `Sent` | `Отправлена` | The command has been sent through the execution channel |
| `Acknowledged by device` | `Получена устройством` | Command receipt acknowledged; successful execution has not yet been confirmed |
| `Executed` | `Выполнена` | There is an acknowledgment of the execution result |
| `Outcome unknown` | `Исход неизвестен` | The result cannot be determined; automatic re-execution is prohibited |
| `Audit reason` | `Основание` | Explanation of the action, kept with its author |
| `Current data` | `Актуальные данные` | Connection and last successful load allow actions to be performed |
| `Refreshing` | `Обновление данных` | Current rows remain accessible during background loading |
| `Stale data` | `Устаревшие данные` | Last known result is saved; actions require restoring freshness |
| `Source` | `Источник` | The system or device from which the value came |
| `Provenance` | `Происхождение значения` | Origin, transformations, and time of obtaining the value |
| `Scope` | `Область` | Selected objects and access boundaries |
| `Shift supervisor` | `Старший смены` | Role of operational control and coordination |
| `Training mode` | `Режим учений` | Explicitly marked training work, separated from real command execution |
| `Video wall` | `Видеостена` | Large display for remote observation |

## Dictionaries

In `web/apps/console/src/ui/locales`, files `product-en.ts` and `product-ru.ts` contain product labels and ICU messages; `demo-en.ts` and `demo-ru.ts` contain demo object names, example values, and explanations of templates. Types and checks prevent missing keys and overlapping namespaces. The product console only loads the selected product dictionary; the showcase additionally loads its own dictionary in the same language.

The language changes after loading the required glossaries. Before that, previous rows and language are available; an error does not discard input or selection. If the initial loading fails, the page is offered to reload. If the language change fails, the current screen with a message about possible reloading is saved. This is a user-defined action: unfinished work does not automatically reload.

 Pseudolocalization only expands example literals in the component catalog. Identifiers and entered values do not change.
