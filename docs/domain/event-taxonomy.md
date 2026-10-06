# Таксономия событий v1 и модель критичности

Версия 0.1 · Шаг плана 0.2 · Статус: черновик

Таксономия - общий язык событий от любых источников. Правила отображения (Normalizer) переводят коды производителей в типы таксономии; правила корреляции и отчёты работают только с типами таксономии. В шаге 0.5 таксономия переносится в машиночитаемый файл в `contracts/` и становится единственным источником истины; этот документ остаётся описанием.

---

## 1. Структура кода

```
<домен>.<объект>.<событие>
```

- **Домен** - подсистема безопасности или инфраструктуры.
- **Объект** - что именно сработало: зона, шлейф, дверь, детектор, канал.
- **Событие** - что произошло.

Коды - строчные латинские буквы, цифры и `_`. Примеры: `intrusion.zone.alarm`, `access.door.forced`, `video.analytics.person_detected`.

**Эволюция:** типы только добавляются. Удалённый тип помечается `deprecated` и продолжает приниматься до следующей мажорной версии таксономии. Переименование - это добавление нового типа и вывод старого. Версия таксономии входит в нормализованное событие.

## 2. Класс события

Каждый тип относится к одному классу. Класс определяет поведение по умолчанию в корреляции и отображении.

| Класс | Смысл | Пример |
|---|---|---|
| `alarm` | Тревога: возможная угроза | Проникновение, пожар, взлом двери |
| `warning` | Предупреждение: отклонение без прямой угрозы | Дверь удерживается открытой, предварительный пожарный сигнал |
| `fault` | Неисправность оборудования или связи | Обрыв шлейфа, потеря видеосигнала |
| `restore` | Возврат в норму после `alarm`, `warning` или `fault` | Восстановление шлейфа, связь восстановлена |
| `state` | Смена режима | Постановка на охрану, снятие с охраны |
| `info` | Информационное событие | Проход по карте, вход оператора в систему устройства |

Пары `alarm`/`fault` ↔ `restore` указаны в столбце «Восстановление»; правило корреляции `absence` и проекции состояния устройств используют их, чтобы определить текущее состояние источника.

## 3. Критичность

| Уровень | Код | Число | Смысл | Значимое событие (по умолчанию) |
|---|---|---|---|---|
| Критичная | `critical` | 4 | Угроза жизни или крупный ущерб | да |
| Высокая | `high` | 3 | Вероятная угроза безопасности | да |
| Средняя | `medium` | 2 | Требует внимания | да |
| Низкая | `low` | 1 | Отклонение без немедленных последствий | нет |
| Информационная | `info` | 0 | Норма, для истории | нет |

- Критичность по умолчанию задана для типа (столбец «Крит.»). Правило отображения может переопределить её для конкретного источника.
- Порог значимости настраивается на тенанте (`severity ≥ medium` по умолчанию). Событие, на которое ссылается сигнал, значимо всегда.

## 4. Приоритет инцидента

| Приоритет | Смысл | Критичность по умолчанию | SLA принятия (по умолчанию) |
|---|---|---|---|
| `P1` | Немедленная реакция | `critical` | 30 с |
| `P2` | Срочная реакция | `high` | 60 с |
| `P3` | Реакция в пределах смены | `medium` | 5 мин |
| `P4` | Плановая обработка | `low` | 30 мин |

Правило корреляции вычисляет приоритет сигнала выражением; по умолчанию - из максимальной критичности событий сигнала по таблице. События `info` по умолчанию сигналов не порождают. SLA задаются политикой эскалации; значения в таблице - стартовые настройки.

---

## 5. Типы событий

### 5.1 `intrusion` - охранная сигнализация

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `intrusion.zone.alarm` | Тревога в охранной зоне | alarm | high | `intrusion.zone.restore` |
| `intrusion.zone.restore` | Зона в норме | restore | info | - |
| `intrusion.zone.armed` | Постановка на охрану | state | info | - |
| `intrusion.zone.disarmed` | Снятие с охраны | state | info | - |
| `intrusion.zone.arm_failed` | Отказ постановки на охрану | warning | medium | - |
| `intrusion.zone.bypassed` | Зона исключена из охраны | warning | low | `intrusion.zone.unbypassed` |
| `intrusion.zone.unbypassed` | Зона возвращена в охрану | restore | info | - |
| `intrusion.panic.alarm` | Тревожная кнопка | alarm | critical | - |
| `intrusion.duress.alarm` | Код принуждения | alarm | critical | - |
| `intrusion.device.tamper` | Вскрытие корпуса | alarm | high | `intrusion.device.tamper_restore` |
| `intrusion.device.tamper_restore` | Корпус закрыт | restore | info | - |

### 5.2 `fire` - пожарная сигнализация

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `fire.detector.alarm` | Пожар | alarm | critical | `fire.detector.restore` |
| `fire.detector.prealarm` | Предварительный сигнал (внимание) | warning | high | `fire.detector.restore` |
| `fire.detector.restore` | Извещатель в норме | restore | info | - |
| `fire.manual_call_point.alarm` | Ручной пожарный извещатель | alarm | critical | `fire.manual_call_point.restore` |
| `fire.manual_call_point.restore` | Ручной извещатель в норме | restore | info | - |
| `fire.loop.fault` | Неисправность шлейфа | fault | medium | `fire.loop.restore` |
| `fire.loop.restore` | Шлейф в норме | restore | info | - |
| `fire.extinguishing.started` | Пуск автоматического пожаротушения | alarm | critical | - |
| `fire.extinguishing.blocked` | Автоматика пожаротушения отключена | warning | high | `fire.extinguishing.unblocked` |
| `fire.extinguishing.unblocked` | Автоматика пожаротушения включена | restore | info | - |
| `fire.panel.reset` | Сброс пожарной панели | state | info | - |

### 5.3 `access` - СКУД

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `access.door.granted` | Проход разрешён | info | info | - |
| `access.door.denied` | Проход запрещён | info | low | - |
| `access.door.denied_repeated` | Повторные отказы в проходе | warning | medium | - |
| `access.door.forced` | Взлом двери (открыта без разрешения) | alarm | high | `access.door.closed` |
| `access.door.held_open` | Дверь удерживается открытой | warning | medium | `access.door.closed` |
| `access.door.opened` | Дверь открыта | info | info | - |
| `access.door.closed` | Дверь закрыта | restore | info | - |
| `access.door.locked` | Дверь заблокирована | state | info | - |
| `access.door.unlocked` | Дверь разблокирована | state | info | - |
| `access.credential.unknown` | Неизвестный идентификатор | warning | low | - |
| `access.credential.blocked` | Заблокированный идентификатор | warning | medium | - |
| `access.antipassback.violation` | Нарушение antipassback | warning | low | - |
| `access.reader.tamper` | Вскрытие считывателя | alarm | high | `access.reader.tamper_restore` |
| `access.reader.tamper_restore` | Считыватель в норме | restore | info | - |

### 5.4 `video` - видео и видеоаналитика

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `video.analytics.motion` | Движение в кадре | info | low | - |
| `video.analytics.person_detected` | Обнаружен человек | warning | medium | - |
| `video.analytics.vehicle_detected` | Обнаружено транспортное средство | warning | low | - |
| `video.analytics.line_crossed` | Пересечение линии | alarm | high | - |
| `video.analytics.zone_intrusion` | Вход в запретную зону кадра | alarm | high | - |
| `video.analytics.loitering` | Длительное нахождение | warning | medium | - |
| `video.analytics.object_left` | Оставленный предмет | alarm | high | - |
| `video.analytics.object_removed` | Пропажа предмета | alarm | medium | - |
| `video.analytics.crowd` | Скопление людей | warning | medium | - |
| `video.analytics.smoke_fire` | Видеодетекция дыма или огня | alarm | critical | - |
| `video.camera.tamper` | Саботаж камеры (заслонение, расфокус, поворот) | alarm | high | `video.camera.tamper_restore` |
| `video.camera.tamper_restore` | Изображение камеры в норме | restore | info | - |
| `video.camera.signal_lost` | Потеря видеосигнала | fault | medium | `video.camera.signal_restored` |
| `video.camera.signal_restored` | Видеосигнал восстановлен | restore | info | - |

Атрибуты аналитики: `object_class`, `confidence` (0..1), `track_id`, `bbox`, `snapshot_url`.

### 5.5 `perimeter` - периметр

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `perimeter.sensor.alarm` | Срабатывание периметрального средства обнаружения | alarm | high | `perimeter.sensor.restore` |
| `perimeter.sensor.restore` | Участок периметра в норме | restore | info | - |
| `perimeter.fence.cut` | Перекус или разрушение ограждения | alarm | high | `perimeter.sensor.restore` |
| `perimeter.fence.climb` | Преодоление ограждения | alarm | high | `perimeter.sensor.restore` |
| `perimeter.gate.opened` | Ворота открыты | info | info | `perimeter.gate.closed` |
| `perimeter.gate.closed` | Ворота закрыты | restore | info | - |
| `perimeter.gate.forced` | Несанкционированное открытие ворот | alarm | high | `perimeter.gate.closed` |
| `perimeter.sensor.tamper` | Вскрытие средства обнаружения | alarm | high | `perimeter.sensor.restore` |

### 5.6 `technical` - технические неисправности и инженерные системы

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `technical.device.offline` | Устройство не на связи | fault | medium | `technical.device.online` |
| `technical.device.online` | Связь с устройством восстановлена | restore | info | - |
| `technical.device.fault` | Неисправность устройства | fault | medium | `technical.device.restore` |
| `technical.device.restore` | Устройство в норме | restore | info | - |
| `technical.power.mains_lost` | Пропадание основного питания | fault | medium | `technical.power.mains_restored` |
| `technical.power.mains_restored` | Основное питание восстановлено | restore | info | - |
| `technical.power.battery_low` | Разряд резервного источника | fault | medium | `technical.power.battery_ok` |
| `technical.power.battery_ok` | Резервный источник в норме | restore | info | - |
| `technical.network.link_down` | Потеря канала связи | fault | medium | `technical.network.link_up` |
| `technical.network.link_up` | Канал связи восстановлен | restore | info | - |
| `technical.environment.temperature_high` | Превышение температуры | warning | medium | `technical.environment.normal` |
| `technical.environment.water_leak` | Протечка | alarm | high | `technical.environment.normal` |
| `technical.environment.normal` | Параметры среды в норме | restore | info | - |

### 5.7 `system` - события самой платформы и коннекторов

Порождаются платформой, а не устройствами, и проходят тот же конвейер - это позволяет строить правила на отказах интеграций.

| Код | Название | Класс | Крит. | Восстановление |
|---|---|---|---|---|
| `system.connector.offline` | Коннектор не на связи | fault | high | `system.connector.online` |
| `system.connector.online` | Коннектор на связи | restore | info | - |
| `system.connector.buffer_overflow` | Переполнение буфера коннектора, возможна потеря событий | fault | high | - |
| `system.source.sequence_gap` | Разрыв порядковых номеров источника | warning | medium | - |
| `system.source.clock_skew` | Рассогласование часов источника | warning | low | - |
| `system.heartbeat.missed` | Ожидаемое периодическое событие не получено (правило `absence`) | fault | medium | - |

### 5.8 `generic` - для источников без точного соответствия

| Код | Название | Класс | Крит. |
|---|---|---|---|
| `generic.alarm` | Тревога (без уточнения) | alarm | high |
| `generic.warning` | Предупреждение | warning | medium |
| `generic.fault` | Неисправность | fault | medium |
| `generic.restore` | Норма | restore | info |
| `generic.info` | Информация | info | info |

Используется, когда интегратор ещё не описал точное отображение. Доля событий `generic.*` - метрика качества интеграции.

---

## 6. Общие атрибуты нормализованного события

Помимо полей конверта, каждое событие может нести атрибуты из стандартного набора. Произвольные атрибуты коннектора передаются с префиксом `x.`.

| Атрибут | Тип | Применение |
|---|---|---|
| `credential_id` | строка | СКУД: идентификатор карты (хранится в виде хэша, если так настроено) |
| `person_ref` | строка | СКУД: внешний идентификатор владельца |
| `direction` | `in`, `out` | СКУД, периметр, видео |
| `object_class` | строка | Видеоаналитика |
| `confidence` | число 0..1 | Видеоаналитика |
| `track_id` | строка | Видеоаналитика |
| `snapshot_url`, `clip_url` | URL | Видео: ссылка во внешнюю систему |
| `loop` | строка | Пожарная и охранная сигнализация: номер шлейфа |
| `value`, `unit` | число, строка | Измерения среды |
| `raw_code` | строка | Исходный код события производителя (всегда) |

## 7. Покрытие требований DoD шага 0.2

| Подсистема из DoD | Домен таксономии |
|---|---|
| Охранная сигнализация | `intrusion` |
| Пожарная сигнализация | `fire` |
| СКУД | `access` |
| Видеоаналитика | `video` |
| Периметр | `perimeter` |
| Технические неисправности | `technical`, `system` |
