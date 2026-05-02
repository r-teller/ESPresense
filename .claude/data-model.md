# Persistent State & MQTT Topology

> This project has no database. "Data model" here means: SPIFFS files (on-device persistence), MQTT topic layout, and Home Assistant discovery entity inventory.

Purpose: Single page covering every byte that survives a reboot, and every MQTT topic that crosses the network boundary.

---

## SPIFFS files

All state files live at the SPIFFS root. Wiped by **factory reset** (4-second button hold → `SPIFFS.format()`).

| File | Owner | Format | Written when | Purpose |
|------|-------|--------|--------------|---------|
| `/relay_state` | `Relay` | 1 byte: `"0"` or `"1"` | Every 15s if dirty AND `restore_mode == 2` (throttled per `LEDs::Save()` pattern) | Last relay state for `Restore Last` mode |
| `/relay_tripped` | `CSE7766` (writes), `Relay` (reads at boot) | 1 byte presence-flag | On overcurrent trip; deleted on `clearTrip()` | Power-cycle-loop guard. If present at boot AND restore mode is Always On, force OFF. |
| `/cse7766_total_wh` | `CSE7766` | text-encoded float (Wh) | Every 5 minutes when delta ≥ 10 Wh (flash-wear safety) | Cumulative energy across reboots |
| `/cse7766_current_limit` | `CSE7766` | text-encoded float (amps) | On portal save / MQTT number set | Configurable overcurrent trip threshold (default 16A on Athom; 0 disables) |
| portal-managed config | (existing) HeadlessWiFiSettings | key/value | On portal save | WiFi creds, MQTT broker, room topic, all module pins/intervals |

**Persistence cadence rationale:**
- Relay state: 15s throttle balances flash wear vs lost-state-on-crash window. Matches existing `LEDs::Save()` cadence.
- Energy total: 5 min + 10 Wh delta gate. At full load (3840W) the 10 Wh threshold trips every ~10s, but the 5-min throttle wins. At idle, neither fires — flash sees zero writes.
- Current limit: written only on user action (portal or MQTT number).

---

## MQTT topic layout

All topics use `<roomsTopic>` prefix from the existing ESPresense convention. New topics added by this project:

### Publishes (firmware → broker)

| Topic | Retained | Payload | Purpose |
|-------|----------|---------|---------|
| `<roomsTopic>/relay` | ✅ | `ON` / `OFF` | Current relay state |
| `<roomsTopic>/relay_trip` | ✅ | `ON` / `OFF` | Trip flag — exposed as `binary_sensor` `device_class: problem` |
| `<roomsTopic>/telemetry` | ❌ | JSON (see below) | Coalesced power-monitor telemetry, every `updateIntervalSec` (default 10s) |
| `<roomsTopic>/cse7766_current_limit` | ✅ | text-encoded float | Echoes current limit setting |
| `<roomsTopic>/button_1` | (existing) | `ON` / `OFF` | **Preserved** — no breaking change for users with existing automations |

**Telemetry payload shape:**
```json
{
  "voltage": 120.4,
  "current": 0.83,
  "power": 99.7,
  "apparent_power": 100.0,
  "reactive_power": 7.7,
  "power_factor": 95.0,
  "total_energy": 12.34
}
```
Single message instead of 6 separate publishes — prevents publish backpressure and keeps HA-side template parsing cheap. Power factor is `× 100` so `unit_of_meas: "%"` reads naturally. Energy is in kWh.

### Subscribes (broker → firmware)

| Topic | Routed to | Payload | Purpose |
|-------|-----------|---------|---------|
| `<roomsTopic>/relay/set` | `Relay::Command` | `ON` / `OFF` / `TOGGLE` | Drive relay |
| `<roomsTopic>/cse7766_current_limit/set` | `CSE7766::Command` | float | Adjust current limit at runtime (HA `number` entity) |

Auto-routed via the existing `Command` chain in `main.cpp:390`.

---

## Home Assistant discovery — entity inventory

8 new discovery payloads emitted by `CSE7766::SendDiscovery()` + 1 by `Relay::SendDiscovery()`:

| Entity name | HA platform | dev_cla | stat_cla | unit | Notes |
|-------------|-------------|---------|----------|------|-------|
| relay | `switch` | — | — | — | `sendSwitchDiscovery("relay", EC_NONE)` |
| voltage | `sensor` | `voltage` | `measurement` | V | from telemetry JSON |
| current | `sensor` | `current` | `measurement` | A | from telemetry JSON |
| power | `sensor` | `power` | `measurement` | W | from telemetry JSON |
| apparent_power | `sensor` | `apparent_power` | `measurement` | VA | from telemetry JSON |
| reactive_power | `sensor` | `reactive_power` | `measurement` | var | from telemetry JSON |
| power_factor | `sensor` | `power_factor` | `measurement` | % | × 100 in firmware |
| total_energy | `sensor` | `energy` | `total_increasing` | kWh | feeds HA Energy dashboard |
| CSE7766 Current Limit | `number` | — | — | A | bounded 0.0–16.0, step 0.5, mode `box`, `EC_CONFIG` |
| Relay Trip | `binary_sensor` | `problem` | — | — | `EC_DIAGNOSTIC` |

The `number` entity is what makes the current limit tunable from HA without a portal trip.

---

## Cross-module dependencies

```
                                  ┌───────┐
                                  │  GUI  │  ButtonPressed/ButtonLongPressed
                                  └───┬───┘
                                      │
                  ┌───────────────────┼───────────────────┐
                  │                                       │
                  ▼                                       ▼
              ┌───────┐                            (factory reset:
              │ Relay │                             SPIFFS.format
              └───┬───┘                             + ESP.restart)
                  ▲
                  │ clearTrip
                  │ + Relay::set(false) on trip
                  │
            ┌───────────┐
            │  CSE7766  │
            └───────────┘
                  ▲
                  │ reads UART1 RX
                  │ (4800 8E1, GPIO20)
                  │
            [CSE7766 chip on Athom board]
```

`Button.cpp` calls only `GUI::ButtonPressed()` / `GUI::ButtonLongPressed()` — never directly into `Relay`. CSE7766's coupling to Relay is one-way (`Relay::set(false)` on trip; `Relay::set(true)` triggers `CSE7766::clearTrip()` via the relay's set-path), and is the only sanctioned cross-module mutator call because the trip path is safety-critical.

---

## Known limitations (document in PR)

- **CF pulse counter wraps every ~30–60s at full load** (3840W). Loop runs every 50ms in normal operation, so wraps are caught natively (unsigned subtraction). If `loop()` stalls > 30s during OTA, 1–10 Wh of energy may be lost — acceptable.
- **Reboot during energy accumulation** can lose 0–30 Wh between last save and reboot. Mitigated by 5-min throttle + 10 Wh delta-gate.
- **HA `total_increasing` reset semantics**: if `totalEnergyWh` reloads from SPIFFS at a value lower than HA last saw before reboot, HA discards the gap. Documented behavior, not a bug.

---

## Reference

- Discovery helpers: `src/mqtt.cpp` / `src/mqtt.h`
- Topic prefix: `roomsTopic` (existing global)
- HA Energy dashboard requires `state_class: total_increasing` on the `energy` sensor — discovery helper extension exists for this purpose
