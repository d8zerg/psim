# Taxonomy of Events v1 and Severity Model

<!-- language-links -->
[English](event-taxonomy.md) | [Русский](../ru/domain/event-taxonomy.md)
<!-- /language-links -->

Taxonomy is a common language for events from any source. The Normalizer rules convert vendor codes into taxonomy types; correlation and reporting rules only work with taxonomy types. The only source of truth is [contracts/taxonomy/v1/taxonomy.yaml](../../contracts/taxonomy/v1/taxonomy.yaml); this document describes the taxonomy. Tables in section 5 are automatically synchronized with YAML (`task contracts:verify`); new types are added to YAML first.

---

## 1. Code Structure

```
<domain>.<object>.<event>
```

- **Domain** - Security subsystem or infrastructure.
- **Object** - The affected entity: zone, stream, door, detector or channel.
- **Event** - What occurred.

Codes are lowercase letters, numbers, and `_`. Examples: `intrusion.zone.alarm`, `access.door.forced`, `video.analytics.person_detected`.

**Evolution:** Types are only added. Removed types are marked as `deprecated` and continue to be accepted until the next major version of the taxonomy. Renaming involves adding a new type and deprecating the old one. A taxonomy version is included in a normalized event.

## 2. Event Class

Each type belongs to one class. The class defines default behavior in correlation and reporting.

| Class | Meaning | Example |
|---|---|---|
| `alarm` | Alarm: potential threat | Intrusion, fire, door break-in |
| `warning` | Warning: deviation without direct threat | Door remains open, preliminary fire alert |
| `fault` | Equipment or communication failure | Stream interruption, video signal loss |
| `restore` | Recovery after `alarm`, `warning`, or `fault` | Stream restoration, connection restored |
| `state` | Mode change | Arming and disarming |
| `info` | Information event | Card access, operator entry into the device system |

Pairs `alarm`/`fault` -> `restore` are listed in the “Recovery” column; correlation rule `absence` and device state projections use them to determine the current status of the source.

## 3. Severity

| Level | Code | Number | Meaning | Significant Event (default) |
|---|---|---|---|---|
| Critical | `critical` | 4 | Life-threatening or major damage | Yes |
| High | `high` | 3 | Potential security threat | Yes |
| Medium | `medium` | 2 | Requires attention | Yes |
| Low | `low` | 1 | Deviation without immediate consequences | No |
| Informational | `info` | 0 | Normal, for history | No |

- The default severity is set for the type (in the “Critical” column). The normalization mapping rule can override it for a specific source.
- The significance threshold is configured per tenant (`severity ≥ medium` by default). The event referenced by the signal is always significant.

## 4. Incident Priority

| Priority | Meaning | Default Severity | SLA Acceptance (default) |
|---|---|---|---|
| `P1` | Immediate response | `critical` | 30 seconds |
| `P2` | Urgent response | `high` | 60 seconds |
| `P3` | Response within shift | `medium` | 5 minutes |
| `P4` | Planned processing | `low` | 30 minutes |

The correlation rule calculates the signal priority using an expression; by default, it’s based on the highest severity of the signal events in the table. Events `info` do not generate signals by default. SLA is set by escalation policy; values in the table are default settings.

---

## 5. Event Types

### 5.1 `intrusion` - Intrusion

| Code | Name | Class | Severity | Recovery |
|---|---|---|---|---|
| `intrusion.zone.alarm` | Zone alarm | alarm | high | `intrusion.zone.restore` |
| `intrusion.zone.restore` | Zone restore | restore | info | - |
| `intrusion.zone.armed` | Zone armed | state | info | - |
| `intrusion.zone.disarmed` | Zone disarmed | state | info | - |
| `intrusion.zone.arm_failed` | Zone arm failed | warning | medium | - |
| `intrusion.zone.bypassed` | Zone bypassed | warning | low | `intrusion.zone.unbypassed` |
| `intrusion.zone.unbypassed` | Zone unbypassed | restore | info | - |
| `intrusion.panic.alarm` | Panic alarm | alarm | critical | - |
| `intrusion.duress.alarm` | Duress alarm | alarm | critical | - |
| `intrusion.device.tamper` | Device tamper | alarm | high | `intrusion.device.tamper_restore` |
| `intrusion.device.tamper_restore` | Device tamper restore | restore | info | - |

### 5.2 `fire` - Fire

| Code | Name | Class | Severity | Recovery |
|---|---|---|---|---|
| `fire.detector.alarm` | Detector alarm | alarm | critical | `fire.detector.restore` |
| `fire.detector.prealarm` | Detector prealarm | warning | high | `fire.detector.restore` |
| `fire.detector.restore` | Detector restore | restore | info | - |
| `fire.manual_call_point.alarm` | Manual call point alarm | alarm | critical | `fire.manual_call_point.restore` |
| `fire.manual_call_point.restore` | Manual call point restore | restore | info | - |
| `fire.loop.fault` | Loop fault | fault | medium | `fire.loop.restore` |
| `fire.loop.restore` | Loop restore | restore | info | - |
| `fire.extinguishing.started` | Extinguishing started | alarm | critical | - |
| `fire.extinguishing.blocked` | Extinguishing blocked | warning | high | `fire.extinguishing.unblocked` |
| `fire.extinguishing.unblocked` | Extinguishing unblocked | restore | info | - |
| `fire.panel.reset` | Panel reset | state | info | - |

### 5.3 `access` - Access

| Code | Name | Class | Severity | Recovery |
|---|---|---|---|---|
| `access.door.granted` | Door granted | info | info | - |
| `access.door.denied` | Door denied | info | low | - |
| `access.door.denied_repeated` | Door denied repeated | warning | medium | - |
| `access.door.forced` | Door forced | alarm | high | `access.door.closed` |
| `access.door.held_open` | Door held open | warning | medium | `access.door.closed` |
| `access.door.opened` | Door opened | info | info | - |
| `access.door.closed` | Door closed | restore | info | - |
| `access.door.locked` | Door locked | state | info | - |
| `access.door.unlocked` | Door unlocked | state | info | - |
| `access.credential.unknown` | Credential unknown | warning | low | - |
| `access.credential.blocked` | Credential blocked | warning | medium | - |
| `access.antipassback.violation` | Antipassback violation | warning | low | - |
| `access.reader.tamper` | Reader tamper | alarm | high | `access.reader.tamper_restore` |
| `access.reader.tamper_restore` | Reader tamper restore | restore | info | - |

### 5.4 `video` - Video

| Code | Name | Class | Crit. | Restoration |
|---|---|---|---|---|
| `video.analytics.motion` | Analytics motion | info | low | - |
| `video.analytics.person_detected` | Analytics person detected | warning | medium | - |
| `video.analytics.vehicle_detected` | Analytics vehicle detected | warning | low | - |
| `video.analytics.line_crossed` | Analytics line crossed | alarm | high | - |
| `video.analytics.zone_intrusion` | Analytics zone intrusion | alarm | high | - |
| `video.analytics.loitering` | Analytics loitering | warning | medium | - |
| `video.analytics.object_left` | Analytics object left | alarm | high | - |
| `video.analytics.object_removed` | Analytics object removed | alarm | medium | - |
| `video.analytics.crowd` | Analytics crowd | warning | medium | - |
| `video.analytics.smoke_fire` | Analytics smoke fire | alarm | critical | - |
| `video.camera.tamper` | Camera tamper | alarm | high | `video.camera.tamper_restore` |
| `video.camera.tamper_restore` | Camera tamper restore | restore | info | - |
| `video.camera.signal_lost` | Camera signal lost | fault | medium | `video.camera.signal_restored` |
| `video.camera.signal_restored` | Camera signal restored | restore | info | - |

Analytics attributes: `object_class`, `confidence` (0..1), `track_id`, `bbox`, `snapshot_url`.

### 5.5 `perimeter` - Perimeter

| Code | Name | Class | Crit. | Restoration |
|---|---|---|---|---|
| `perimeter.sensor.alarm` | Sensor alarm | alarm | high | `perimeter.sensor.restore` |
| `perimeter.sensor.restore` | Sensor restore | restore | info | - |
| `perimeter.fence.cut` | Fence cut | alarm | high | `perimeter.sensor.restore` |
| `perimeter.fence.climb` | Fence climb | alarm | high | `perimeter.sensor.restore` |
| `perimeter.gate.opened` | Gate opened | info | info | `perimeter.gate.closed` |
| `perimeter.gate.closed` | Gate closed | restore | info | - |
| `perimeter.gate.forced` | Gate forced | alarm | high | `perimeter.gate.closed` |
| `perimeter.sensor.tamper` | Sensor tamper | alarm | high | `perimeter.sensor.restore` |

### 5.6 `technical` - Technical

| Code | Name | Class | Crit. | Restoration |
|---|---|---|---|---|
| `technical.device.offline` | Device offline | fault | medium | `technical.device.online` |
| `technical.device.online` | Device online | restore | info | - |
| `technical.device.fault` | Device fault | fault | medium | `technical.device.restore` |
| `technical.device.restore` | Device restore | restore | info | - |
| `technical.power.mains_lost` | Power mains lost | fault | medium | `technical.power.mains_restored` |
| `technical.power.mains_restored` | Power mains restored | restore | info | - |
| `technical.power.battery_low` | Power battery low | fault | medium | `technical.power.battery_ok` |
| `technical.power.battery_ok` | Power battery ok | restore | info | - |
| `technical.network.link_down` | Network link down | fault | medium | `technical.network.link_up` |
| `technical.network.link_up` | Network link up | restore | info | - |
| `technical.environment.temperature_high` | Environment temperature high | warning | medium | `technical.environment.normal` |
| `technical.environment.water_leak` | Environment water leak | alarm | high | `technical.environment.normal` |
| `technical.environment.normal` | Environment normal | restore | info | - |

### 5.7 `system` - System

These events are generated by the platform, not by devices, and follow the same pipeline. This allows for the creation of rules based on integration failures.

| Code | Name | Class | Crit. | Restoration |
|---|---|---|---|---|
| `system.connector.offline` | Connector offline | fault | high | `system.connector.online` |
| `system.connector.online` | Connector online | restore | info | - |
| `system.connector.buffer_overflow` | Connector buffer overflow | fault | high | - |
| `system.source.sequence_gap` | Source sequence gap | warning | medium | - |
| `system.source.clock_skew` | Source clock skew | warning | low | - |
| `system.heartbeat.missed` | Heartbeat missed | fault | medium | - |

### 5.8 `generic` - Generic

| Code | Name | Class | Crit. |
|---|---|---|---|
| `generic.alarm` | Alarm | alarm | high |
| `generic.warning` | Warning | warning | medium |
| `generic.fault` | Fault | fault | medium |
| `generic.restore` | Restore | restore | info |
| `generic.info` | Info | info | info |

Used when the integrator has not yet described the exact mapping. The proportion of events `generic.*` is a metric for integration quality.

---

## 6. General attributes of normalized events

In addition to fields, each event can carry attributes from the standard set. Any connector attribute is transmitted with the prefix `x.`.

| Attribute | Type | Application |
|---|---|---|
| `credential_id` | String | Access control: card identifier (stored as a hash if configured) |
| `person_ref` | String | Access control: external owner identifier |
| `direction` | `in`, `out` | Access control, perimeter, video |
| `object_class` | String | Video analytics |
| `confidence` | Number 0..1 | Video analytics |
| `track_id` | String | Video analytics |
| `snapshot_url`, `clip_url` | URL | Video: link to external system |
| `loop` | String | Fire and security alarm: circuit identifier |
| `value`, `unit` | Number, string | Environment measurements |
| `raw_code` | String | Original code of the event producer (always) |

## 7. Subsystem Coverage

| Supported subsystem | Domain taxonomy |
|---|---|
| Intrusion alarm | `intrusion` |
| Fire alarm | `fire` |
| Access control | `access` |
| Video analytics | `video` |
| Perimeter | `perimeter` |
| Technical failures | `technical`, `system` |
