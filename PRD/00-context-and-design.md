# Context & Design Canon

This document is the single source of truth for design decisions made during the planning phase. Every decision below is approved and locked unless explicitly marked deferred or open.

## Project context

### Hardware target

- **Device**: Athom Smart Plug V3 (https://www.athom.tech/blank-1/esp32-c3-us-plug-for-esphome)
- **Chip**: ESP32-C3, 4MB flash, single core
- **Flash mode**: DIO
- **Initial flash path**: OTA from factory ESPHome firmware via the device's existing web_server (`http://<plug-ip>/update`). Some Athom plug units have a USB-C port for direct flashing; many do not — OTA is the universally-supported path. After first flash, ESPresense has its own OTA mechanism (web UI, MQTT URL push, or auto-update from GitHub releases).
- **Build env still uses `esp32c3-cdc`**: although USB-C programming may not be available on every unit, the chip's USB-CDC peripheral is what handles serial console output once the firmware is running. The build flag `ARDUINO_USB_CDC_ON_BOOT=1` doesn't require a physical USB-C port — the chip-level USB peripheral works for console even on plug-style enclosures (visible only if the user opens the case and connects to the USB pads).
- **Reference firmware**: ESPHome yaml at `/opt/git/personal/esp32-configs/athom-smart-plug.yaml` (ships pre-flashed)

### Pin assignments (verified against Athom's published yaml)

| GPIO | Function | Mode |
|------|----------|------|
| 3 | Power button | INPUT_PULLUP, active LOW |
| 5 | Relay output | OUTPUT, level-driven (non-latching SPST, confirmed by user) |
| 6 | Status LED | PWM **inverted** (active LOW) |
| 20 | CSE7766 UART RX | Serial1 RX-only at 4800 8E1 |

### Project goals

1. Run ESPresense BLE proximity detection on the Athom plug — preserve all existing capabilities
2. Add MQTT-controllable relay so the plug doubles as a smart switch
3. Add power monitoring (V/I/W/PF/energy) with HA Energy dashboard support
4. Overcurrent auto-trip for safety (16A regulatory cap)
5. Long-press factory reset for physical recovery
6. All code generic + reusable (gated behind feature flags) — upstream-quality

## Approved design decisions

### Build configuration

- **Env name**: `athom-smart-plug-v3` (kebab-case, matches existing `m5stickc-plus`/`macchina-a0` style)
- **FIRMWARE define**: `"athom-smart-plug-v3"` (drives OTA artifact name)
- **Feature flags**: `HAS_RELAY` and `HAS_POWER_MONITOR` (with `HAS_` prefix to avoid collision with library macros)
- **Board macro**: `ATHOM_PLUG_V3` (gates board-specific pin defaults)
- **Sensors**: drop `${sensors.lib_deps}` and `-D SENSORS` (Athom has no I²C/OneWire sensors)
- **Extends**: `esp32c3-cdc` (uses USB-CDC, matches Athom programming hardware)

### Default values (Athom-specific overrides in `defaults.h`)

| Macro | Athom value | Generic default |
|-------|-------------|-----------------|
| `DEFAULT_LED1_TYPE` | 1 (PWM Inverted) | varies per board |
| `DEFAULT_LED1_PIN` | 6 | varies |
| `DEFAULT_LED1_CNTRL` | `Control_Type_Status` | varies |
| `DEFAULT_LED1_CNT` | 1 | 1 |
| `DEFAULT_BUTTON1_PIN` | 3 | -1 (disabled) |
| `DEFAULT_BUTTON1_TYPE` | 0 (Pullup) | 0 |
| `DEFAULT_RELAY_PIN` | 5 | -1 (disabled) |
| `DEFAULT_RELAY_RESTORE_MODE` | 1 (Always On) | 0 (Always Off) |
| `DEFAULT_CSE7766_RX_PIN` | 20 | -1 (disabled) |
| `DEFAULT_CURRENT_LIMIT_AMPS` | 16.0f (regulatory cap) | 0.0f (disabled) |
| `DEFAULT_POWER_UPDATE_INTERVAL` | 10s | 10s |
| I²C bus | -1/-1 (no I²C on Athom) | varies |

### Wide bug fix bundled with this PR

The existing `ESP32C3` block in `defaults.h:112-117` sets `DEFAULT_I2C_BUS_1_SDA=19, SCL=18`. On C3+CDC builds, GPIO 18/19 are USB D-/D+ pins — defaulting to I²C kills USB-CDC. **Fix**: gate the GPIO 18/19 defaults behind `#if !defined(ARDUINO_USB_CDC_ON_BOOT) || ARDUINO_USB_CDC_ON_BOOT == 0`. Affects all C3-CDC users, not just Athom. Ships in a separate commit.

### Discovery helper extensions (`mqtt.cpp` + `mqtt.h`)

Three helper extensions needed:

1. **`sendSensorDiscovery`** — add defaulted `stateClass` arg (last position) to emit `stat_cla` for HA Energy dashboard (`measurement` / `total_increasing`)
2. **`sendTeleSensorDiscovery`** — same `stateClass` addition (used by JSON-coalesced telemetry pattern)
3. **`sendNumberDiscovery`** — replace fixed `step="0.1"` body with `min` (NaN sentinel), `max` (NaN sentinel), `step` (float), `units`, `mode` — preserves backwards compat by only emitting min/max when caller explicitly sets them

`sendBinarySensorDiscovery` already supports `devClass` — no change needed.

JSON document buffer at `globals.h:34` bumped from 768 → 1024 bytes (defensive against new field overflow with `state_class` added).

### Module C — `Relay.cpp/.h`

Output module mirroring `Switch.cpp` shape, gated by `#ifdef HAS_RELAY`.

**Lifecycle**: `EarlyInit`, `Setup`, `ConnectToWifi`, `SerialReport`, `Loop`, `SendDiscovery`, `SendOnline`, `Command`.

**Public cross-module API**: `set(bool)`, `toggle()`, `getState()` — used by GUI dispatcher (button) and CSE7766 (current trip).

**Two-stage init**:
- `EarlyInit()` runs as the **first action** of `setup()` in `main.cpp` (before `Serial.begin()`). Drives `DEFAULT_RELAY_PIN` (compile-time constant) LOW immediately to prevent boot-glitch on undriven GPIO. Compile-time pin only — runtime config not loaded yet.
- `Setup()` runs in the normal module-init phase after `setupNetwork()`. Loads SPIFFS state files and applies `restore_mode`.

**Restore modes**:
- 0 = Always Off
- 1 = Always On (Athom default — matches ESPHome's `RESTORE_DEFAULT_ON`)
- 2 = Restore Last (reads `/relay_state` from SPIFFS)

**Trip-persistence override**: If `/relay_tripped` exists at boot AND restore mode is Always On, force OFF instead. Prevents power-cycle loops when a stuck-on overcurrent condition triggers reboots.

**State persistence**:
- `/relay_state` (1 byte: `"0"`/`"1"`) — written every 15s if dirty AND restore_mode == 2 (Restore Last)
- Throttled per `LEDs::Save()` pattern at `LEDs.cpp:121`

**MQTT topology**:
- Publish: `<roomsTopic>/relay` retained, payload `ON`/`OFF`
- Subscribe (auto-routed via existing `Command` chain): `<roomsTopic>/relay/set`

**Discovery**: `sendSwitchDiscovery("relay", EC_NONE)`.

### Module D — `CSE7766.cpp/.h`

Power monitor module, gated by `#ifdef HAS_POWER_MONITOR`. Largest single module addition.

**Lifecycle**: standard 7 hooks. No `EarlyInit` (UART can be opened anytime).

**Public cross-module API**:
- `getCurrentPower()` — for energy integration / future modules
- `getCfPulses()` — for energy integration
- `clearTrip()` — called from `Relay::set(true)` to clear trip state on manual re-engage

**UART setup**:
```cpp
Serial1.setRxBufferSize(256);   // defensive against BLE-task stalls on single-core C3
Serial1.begin(4800, SERIAL_8E1, rxPin, -1);   // RX-only
while (Serial1.available()) Serial1.read();    // flush ROM-bootloader garbage on GPIO20
```

**Packet validation** (CSE7766 datasheet §5):
- Sync: `byte[1] == 0x5A`
- Header: `byte[0]` is `0x55` (normal), `0xAA` (chip not calibrated — log + skip), or `(byte[0] & 0xF0) == 0xF0` (per-channel fault flags)
- Checksum: `sum(bytes[2..22]) mod 256 == byte[23]`
- On invalid: slide buffer left by 1, retry sync on next packet

**Packet layout**:
```
[0]    Header
[1]    0x5A
[2-4]  Voltage cal (24-bit BE)
[5-7]  Voltage cycle (24-bit BE)
[8-10] Current cal
[11-13] Current cycle
[14-16] Power cal
[17-19] Power cycle
[20]   Adjustments byte (bit6=V valid, bit5=I valid, bit4=W valid)
[21-22] CF pulses (16-bit BE)
[23]   Checksum
```

**Calibration math**:
- `V = voltage_cal / voltage_cycle` (only when `adj & 0x40`, else silent-hold)
- `I = current_cal / current_cycle` (only when `adj & 0x20`, else silent-hold)
- `P = power_cal / power_cycle` (only when `adj & 0x10`, else silent-hold; force `P=0` if header is `0xF0..0xFF` AND bit 1 of header is set)

**Cold-start**: `voltage`/`current`/`power` initialize to `NaN`. Skip averaging+publishing until each has been seen at least once.

**Status code handling**: `0xAA` (not calibrated) → log message + skip packet. `0xF0..0xFF` → respect per-channel fault bits in low 4 bits, otherwise silent-hold via validity bits in byte 20.

**Averaging**: accumulate per-packet measurements into `double` sums (precision over ~200 samples). Publish averaged values every `updateIntervalSec` (default 10s, configurable 1–600s via portal).

**JSON-coalesced telemetry** (single MQTT message, prevents 6× publish backpressure):
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

Published to `<roomsTopic>/telemetry`. HA discovery uses `value_template: "{{ value_json.<key> }}"` per entity. Power factor multiplied by 100 in firmware so `unit_of_meas: "%"` reads naturally.

**Energy accumulation** (Item E, lives inside this module):
- `whPerPulse = (double)power_cal / 1e6 / 3600.0` (cached on first valid packet)
- Per packet: `uint16_t diff = cf_pulses - lastCfPulses; totalEnergyWh += diff * whPerPulse;` (unsigned subtraction handles 16-bit wrap natively)
- Persist to `/cse7766_total_wh` every 5 minutes when delta ≥ 10 Wh (flash-wear safety)
- Restore from SPIFFS on `Setup()`
- **No daily/weekly/monthly reset in firmware** — users add HA's `utility_meter` integration for windowing (handles timezone + DST correctly)

**Trip behavior** (Item G):
- 5-second boot grace AND require 4 consecutive valid-current packets before arming
- Per-packet evaluation (~50ms response time)
- Require 2 consecutive over-limit packets before tripping (~100ms total) — debounces boundary noise
- Latching: `tripped = true` blocks further evaluation until `clearTrip()` called
- `clearTrip()` invoked from `Relay::set(true)` — i.e., user manual re-engage clears trip → re-arms after 4 valid packets
- Persist `/relay_tripped` (1 byte) on trip; remove on clear — prevents power-cycle loops
- Trip publication: `<roomsTopic>/relay_trip` retained ON/OFF — exposed as binary_sensor with `device_class: problem`
- Rate-limit trip log to 1 Hz to prevent spam during repeated re-engages with persistent fault

**Discovery payload** (8 calls):
```cpp
sendTeleSensorDiscovery("voltage",        EC_NONE, "{{ value_json.voltage }}",        "voltage",        "V",   "measurement");
sendTeleSensorDiscovery("current",        EC_NONE, "{{ value_json.current }}",        "current",        "A",   "measurement");
sendTeleSensorDiscovery("power",          EC_NONE, "{{ value_json.power }}",          "power",          "W",   "measurement");
sendTeleSensorDiscovery("apparent_power", EC_NONE, "{{ value_json.apparent_power }}", "apparent_power", "VA",  "measurement");
sendTeleSensorDiscovery("reactive_power", EC_NONE, "{{ value_json.reactive_power }}", "reactive_power", "var", "measurement");
sendTeleSensorDiscovery("power_factor",   EC_NONE, "{{ value_json.power_factor }}",   "power_factor",   "%",   "measurement");
sendTeleSensorDiscovery("total_energy",   EC_NONE, "{{ value_json.total_energy }}",   "energy",         "kWh", "total_increasing");
sendNumberDiscovery("CSE7766 Current Limit", EC_CONFIG, 0.0f, 16.0f, 0.5f, "A", "box");
sendBinarySensorDiscovery("Relay Trip", EC_DIAGNOSTIC, "problem");
```

### Item F — Button → Relay wiring + factory reset

Edge-detection state machine in `Button.cpp::button_1Loop()`. Behavior dispatch via `GUI::ButtonPressed(int)` and `GUI::ButtonLongPressed(int)` — uses existing `LEDs::Motion()` ↔ `GUI::Motion()` pattern.

**Constants**:
- `BUTTON_LONG_PRESS_MS = 4000` (4s = factory reset)
- `BUTTON_SHORT_PRESS_MAX_MS = 1000` (release < 1s = tap)

**Behaviors**:
- Tap (release before 1s, no long-press fired) → `GUI::ButtonPressed(1)` → `Relay::toggle()` (if `HAS_RELAY`)
- Long-hold (4s+) → `GUI::ButtonLongPressed(1)` → `SPIFFS.format()` + `ESP.restart()`. Fires once even if held longer.
- Existing `<roomsTopic>/button_1` ON/OFF state publishing **preserved** — no breaking change for users with existing automations.
- Button 2 long-press: log only, no factory reset (button 2 is "secondary input" by convention — wiring it to factory reset would surprise generic-board users)

**Factory reset** (new behavior, not present in ESPresense before): `SPIFFS.format()` wipes WiFi, MQTT, room, relay state, energy total, all portal settings → reboot → captive portal returns. Recovery path for "stuck on wrong WiFi" or "wrong MQTT broker" scenarios.

### Item H — `main.cpp` lifecycle wiring

Nine insertion points + 2 header includes. All gated by `#ifdef HAS_RELAY` / `#ifdef HAS_POWER_MONITOR`.

| # | Hook | File:Line | Insert |
|---|------|-----------|--------|
| 1 | main.h includes | After AXP192.h block (line 33-35) | `Relay.h` and `CSE7766.h`, each #ifdef-guarded |
| 2 | `setup()` first action | `main.cpp:611` | `Relay::EarlyInit();` BEFORE Serial.begin() |
| 3 | Portal config | `main.cpp:209` (after `Button::ConnectToWifi`) | `Relay::ConnectToWifi(updating);` then `CSE7766::ConnectToWifi(updating);` |
| 4 | Module init | `main.cpp:638` (after `Button::Setup`) | `Relay::Setup();` then `CSE7766::Setup();` |
| 5 | SerialReport | `main.cpp:273` (after `Button::SerialReport`) | `Relay::SerialReport();` then `CSE7766::SerialReport();` |
| 6 | SendDiscovery chain | `main.cpp:70` (after `Button::SendDiscovery`) | `&& Relay::SendDiscovery() && CSE7766::SendDiscovery()` |
| 7 | SendOnline chain | `main.cpp:47` (after `Button::SendOnline`) | `&& Relay::SendOnline() && CSE7766::SendOnline()` |
| 8 | Command chain | `main.cpp:390` (after `Button::Command`) | `else if (Relay::Command(command, pay))` then `else if (CSE7766::Command(command, pay))` |
| 9 | loop() body | `main.cpp:686` (after `Button::Loop`) | `Relay::Loop();` then `CSE7766::Loop();` |

### Item L — CI matrix entry

Add `athom-smart-plug-v3` to the env list at `.github/workflows/build.yml:16`. Position: at end of list (matches family-grouping convention).

### Item M — UI hardware page (Svelte)

Add two sections to `ui/src/routes/hardware/+page.svelte` between Buttons (~line 477) and I²C (line 479):
- **Relay** section: relay_pin, relay_restore_mode (3-option dropdown)
- **Power Monitor** section: cse7766_rx_pin, cse7766_current_limit, cse7766_update_interval

Field `name` attributes must match firmware's `HeadlessWiFiSettings.*()` keys exactly. Defaults auto-populate from firmware's JSON config endpoint.

After Svelte changes: `cd ui && npm run build` regenerates these embedded headers in `src/`:
- `ui_app_immutable_assets_css.h`
- `ui_app_immutable_chunks_js.h`
- `ui_app_immutable_entry_js.h`
- `ui_app_immutable_nodes_js.h`
- `ui_html.h`
- `ui_routes.h`
- `ui_svg.h`

Both Svelte source AND regenerated headers must be committed.

## Deferred items (NOT in this PR — file as follow-up issues)

- **`GUI::Relay()` for on-device LED feedback** — overlays BLE/WiFi/MQTT status on the single status LED, ambiguous semantics. Skip v1.
- **Pulse/momentary mode** for the relay (timed-on for X minutes). Skip v1.
- **MQTT-triggered factory reset command** — useful for remote, but physical button hold works. Skip v1.
- **CSE7766 calibration multiplier overrides** — for boards with non-Athom shunt resistors. Trust factory cal in v1.
- **Per-build conditional UI rendering** — UI shows all sections regardless of compile flags. Existing pattern. Don't change.
- **`std::atomic<bool>` on `tripped` flag** — only relevant if CSE7766 parsing moves to its own FreeRTOS task. Add comment for future maintainers.

## Pre-existing bugs flagged (note in PR description, file separately)

- `sendSwitchDiscovery` (mqtt.cpp:166) writes `entity_category` even when empty — unlike all other helpers which guard with `isEmpty()`. Pre-existing.
- `*_Timeout` number entities (Switch/Button/Motion) have hardcoded `step: "0.1"` which is wrong for second-granularity timeouts. Pre-existing — `sendNumberDiscovery` extension in this PR enables better defaults but doesn't update existing call sites (out of scope).
- Duplicate `Motion::Command` line at `main.cpp:386` — pre-existing.

## Known limitations (document in PR description)

- 16-bit CF pulse counter wraps every ~30-60s at full load (3840W). Loop runs every 50ms in normal operation, so no missed wraps. If `loop()` stalls >30s during OTA: 1-10 Wh energy loss possible — acceptable.
- Reboot during energy accumulation can lose 0-30 Wh between last save and reboot. Mitigated by 5-min throttle + 10 Wh delta-gate.
- HA Energy dashboard interprets a `total_increasing` decrease as a meter reset — if `totalEnergyWh` reloads from SPIFFS at a value lower than HA last saw before reboot, HA discards the gap. Documented behavior.

## Cross-module dependency map

```
                                  +-------+
                                  |  GUI  |  ButtonPressed/ButtonLongPressed
                                  +-------+
                                      |
                  +-------------------+-------------------+
                  |                                       |
                  v                                       v
              +-------+                            (factory reset:
              | Relay |                             SPIFFS.format
              +-------+                             + ESP.restart)
                  ^
                  | clearTrip
                  | + Relay::set(false) on trip
                  |
            +-----------+
            |  CSE7766  |
            +-----------+
                  ^
                  | reads UART1 RX
                  | (4800 8E1, GPIO20)
                  |
            [CSE7766 chip on Athom board]
```

`Button.cpp` calls only `GUI::ButtonPressed()` / `GUI::ButtonLongPressed()` — never directly into `Relay`. This preserves ESPresense's existing layering (no direct cross-module mutator calls).

## Key file paths reference

### Files we will create
- `src/Relay.cpp` — new
- `src/Relay.h` — new
- `src/CSE7766.cpp` — new
- `src/CSE7766.h` — new

### Files we will modify
- `platformio.ini` — add env
- `include/defaults.h` — add ATHOM_PLUG_V3 arm + new defaults blocks + I²C-CDC fix
- `src/main.h` — add includes
- `src/main.cpp` — wire 9 lifecycle hooks
- `src/Button.cpp` — consume DEFAULT_BUTTON1_*, add edge-detection
- `src/GUI.h` — add ButtonPressed/ButtonLongPressed declarations
- `src/GUI.cpp` — add dispatcher implementations
- `src/mqtt.h` — extend helper signatures
- `src/mqtt.cpp` — extend helper bodies
- `src/globals.h` — bump JSON buffer
- `.github/workflows/build.yml` — CI matrix entry
- `ui/src/routes/hardware/+page.svelte` — UI fields
- `src/ui_*.h` — regenerated by `npm run build` (7 files)

### Files NOT modified
- BLE-related code (`BleFingerprint*`, `Enrollment`, `MiFloraHandler`, `NameModelHandler`, `rssi.h`)
- Sensor modules (BME280, AHTX0, etc.)
- Updater, HttpWebServer, NTP, MultiNetwork, SerialImprov
- Battery, CAN, Motion, Switch (preserved as-is — Switch's pattern was the template for Relay)

## Quick reference — naming summary

| Concept | Name |
|---------|------|
| Build env | `athom-smart-plug-v3` |
| FIRMWARE define | `"athom-smart-plug-v3"` |
| Board macro | `ATHOM_PLUG_V3` |
| Feature flags | `HAS_RELAY`, `HAS_POWER_MONITOR` |
| MQTT relay state topic | `<roomsTopic>/relay` |
| MQTT relay set topic | `<roomsTopic>/relay/set` |
| MQTT trip topic | `<roomsTopic>/relay_trip` |
| MQTT telemetry topic | `<roomsTopic>/telemetry` |
| MQTT current limit number | `<roomsTopic>/cse7766_current_limit` |
| SPIFFS files | `/relay_state`, `/relay_tripped`, `/cse7766_total_wh`, `/cse7766_current_limit` |
| HA discovery prefixes | sensor, switch, number, binary_sensor |
| HA stat_cla values | `measurement`, `total_increasing` |
| HA dev_cla values | `voltage`, `current`, `power`, `apparent_power`, `reactive_power`, `power_factor`, `energy`, `problem` |
