# Firmware Module Map

> ESPresense has no traditional backend. This file documents the **firmware module layout** that plays the same role: lifecycle-orchestrated C++ modules running on the ESP32-C3, communicating with Home Assistant via MQTT.

Purpose: Map the modules being added/modified in this project, the lifecycle hooks they participate in, and the helpers they use. Use as a quick reference while editing `src/`.

---

## Module Lifecycle (existing pattern)

Every module exposes a subset of these static hooks. `main.cpp` orchestrates them. New modules added in this project (`Relay`, `CSE7766`) follow the same shape.

| Hook | Purpose | Called from |
|------|---------|-------------|
| `EarlyInit()` | First action of `setup()` — runs **before** `Serial.begin()`. Used to drive a known state on a GPIO before anything else can glitch it. Compile-time pin only — runtime config not loaded yet. | `main.cpp:611` |
| `Setup()` | Module init after network. SPIFFS is mounted; runtime config is loaded. | `main.cpp:638` (after `Button::Setup`) |
| `ConnectToWifi(updating)` | Register portal config fields with HeadlessWiFiSettings. | `main.cpp:209` |
| `SerialReport()` | Print module state to serial console (debug aid). | `main.cpp:273` |
| `SendDiscovery()` | Publish HA discovery payloads. Chained via `&&` so a single failure aborts the round. | `main.cpp:70` |
| `SendOnline()` | Publish initial state on MQTT-online transition. Chained via `&&`. | `main.cpp:47` |
| `Command(cmd, payload)` | Handle incoming MQTT command. `else if` chain — first module to claim the command wins. | `main.cpp:390` |
| `Loop()` | Main loop body. Called every iteration; modules must be non-blocking. | `main.cpp:686` |

Modules without a hook simply omit it from their header and `main.cpp` doesn't call it.

---

## New / Modified Modules

### `Relay` (NEW — `src/Relay.cpp/.h`)

Output module gated by `#ifdef HAS_RELAY`. Mirrors `Switch.cpp` shape.

**Hooks used:** `EarlyInit`, `Setup`, `ConnectToWifi`, `SerialReport`, `Loop`, `SendDiscovery`, `SendOnline`, `Command`.

**Cross-module API:**

| Function | Purpose | Caller |
|----------|---------|--------|
| `set(bool)` | Drive relay; clears trip on `true` (manual re-engage path) | `GUI::ButtonPressed()` (via toggle), MQTT command |
| `toggle()` | Flip current state | `GUI::ButtonPressed()` (button tap) |
| `getState()` | Read current state | telemetry / debug |

**Two-stage init rationale:**
- `EarlyInit()` drives `DEFAULT_RELAY_PIN` LOW immediately. Without this, the pin floats during boot — load can briefly energize before the relay driver is configured.
- `Setup()` runs after SPIFFS mount and applies the configured `restore_mode`.

**Restore modes:**
| Mode | Behavior |
|------|----------|
| `0` | Always Off |
| `1` | Always On (Athom default — matches ESPHome `RESTORE_DEFAULT_ON`) |
| `2` | Restore Last (reads `/relay_state` from SPIFFS) |

**Trip override:** if `/relay_tripped` exists at boot AND restore mode is Always On, force OFF instead. Prevents power-cycle loops from a stuck-on overcurrent fault.

**MQTT topology:**
- Publish: `<roomsTopic>/relay` (retained, `ON`/`OFF`)
- Subscribe (auto-routed via `Command` chain): `<roomsTopic>/relay/set`
- Discovery: `sendSwitchDiscovery("relay", EC_NONE)`

---

### `CSE7766` (NEW — `src/CSE7766.cpp/.h`)

Power monitor module gated by `#ifdef HAS_POWER_MONITOR`. Largest single module addition.

**Hooks used:** standard 7 (no `EarlyInit`).

**Cross-module API:**

| Function | Purpose | Caller |
|----------|---------|--------|
| `getCurrentPower()` | Latest averaged power reading | future modules / debug |
| `getCfPulses()` | Latest CF pulse count (energy integration) | future modules |
| `clearTrip()` | Clear trip-latched state and `/relay_tripped` flag | `Relay::set(true)` |

**UART setup (Serial1 RX-only):**
```cpp
Serial1.setRxBufferSize(256);   // defensive against BLE-task stalls on single-core C3
Serial1.begin(4800, SERIAL_8E1, rxPin, -1);
while (Serial1.available()) Serial1.read();   // flush ROM-bootloader garbage on GPIO20
```

**Packet validation (CSE7766 datasheet §5):**
- Sync: `byte[1] == 0x5A`
- Header: `0x55` (normal), `0xAA` (chip not calibrated → log + skip), or `(byte[0] & 0xF0) == 0xF0` (per-channel fault flags)
- Checksum: `sum(bytes[2..22]) mod 256 == byte[23]`
- On invalid: slide buffer left by 1, retry sync on next packet

**Calibration math:** `V = voltage_cal / voltage_cycle` (only when `adj & 0x40`, else silent-hold). Same pattern for I (`& 0x20`) and P (`& 0x10`).

**Cold-start:** V/I/P initialize to `NaN`; skip averaging+publishing until each has been seen at least once.

**Averaging:** accumulate per-packet measurements into `double` sums (precision over ~200 samples). Publish every `updateIntervalSec` (default 10s, configurable 1–600s via portal).

**JSON-coalesced telemetry** (single MQTT message; prevents 6× publish backpressure):
```json
{"voltage": 120.4, "current": 0.83, "power": 99.7, "apparent_power": 100.0,
 "reactive_power": 7.7, "power_factor": 95.0, "total_energy": 12.34}
```
Published to `<roomsTopic>/telemetry`. HA discovery uses `value_template: "{{ value_json.<key> }}"` per entity. Power factor is multiplied by 100 in firmware so `unit_of_meas: "%"` reads naturally.

**Energy accumulation:**
- `whPerPulse = (double)power_cal / 1e6 / 3600.0` (cached on first valid packet)
- Per packet: `uint16_t diff = cf_pulses - lastCfPulses; totalEnergyWh += diff * whPerPulse;` (unsigned subtraction handles 16-bit wrap)
- Persist to `/cse7766_total_wh` every 5 minutes when delta ≥ 10 Wh (flash-wear safety)
- Restore from SPIFFS on `Setup()`
- HA `utility_meter` integration handles daily/weekly/monthly windowing — not done in firmware

**Trip behavior:**
- 5-second boot grace AND require 4 consecutive valid-current packets before arming
- Per-packet evaluation (~50ms response time)
- Require 2 consecutive over-limit packets before tripping (~100ms total) — debounces boundary noise
- Latching: `tripped = true` blocks evaluation until `clearTrip()` called
- `clearTrip()` invoked from `Relay::set(true)` — i.e., user manual re-engage clears trip → re-arms after 4 valid packets
- Persist `/relay_tripped` (1 byte) on trip; remove on clear
- Trip publication: `<roomsTopic>/relay_trip` retained `ON`/`OFF` — exposed as `binary_sensor` with `device_class: problem`
- Rate-limit trip log to 1 Hz to prevent spam during repeated re-engages

---

### `Button` (modified — `src/Button.cpp`)

Add edge-detection + long-press state machine to `button_1Loop()`.

**Constants:**
- `BUTTON_LONG_PRESS_MS = 4000` (4s = factory reset)
- `BUTTON_SHORT_PRESS_MAX_MS = 1000` (release < 1s = tap)

**Behaviors:**
| Event | Action |
|-------|--------|
| Tap (release < 1s, no long-press fired) | `GUI::ButtonPressed(1)` → `Relay::toggle()` (if `HAS_RELAY`) |
| Long-hold (4s+) | `GUI::ButtonLongPressed(1)` → `SPIFFS.format()` + `ESP.restart()`. Fires once even if held longer. |
| Existing `<roomsTopic>/button_1` ON/OFF state | **Preserved** — no breaking change for existing automations |
| Button 2 long-press | Log only (button 2 is "secondary input" by convention) |

---

### `GUI` (modified — `src/GUI.cpp/.h`)

Adds `ButtonPressed(int)` / `ButtonLongPressed(int)` dispatchers. Mirrors the existing `LEDs::Motion()` ↔ `GUI::Motion()` pattern.

`Button.cpp` calls only `GUI::*` — never directly into `Relay`. Preserves layering: GUI is the cross-module dispatcher; modules don't reach across each other.

---

### `mqtt` (modified — `src/mqtt.cpp/.h`)

Three discovery helper extensions (backwards-compatible — defaults preserve existing behavior):

| Helper | Change |
|--------|--------|
| `sendSensorDiscovery` | Add defaulted `stateClass` arg (last position). Emits `stat_cla` for HA Energy dashboard. |
| `sendTeleSensorDiscovery` | Same `stateClass` addition (used by JSON-coalesced telemetry). |
| `sendNumberDiscovery` | Replace fixed `step="0.1"` body with `min` (NaN sentinel), `max` (NaN sentinel), `step`, `units`, `mode`. Backwards-compat: only emits min/max when caller explicitly sets them. |

`sendBinarySensorDiscovery` already supports `devClass` — no change.

`globals.h:34` JSON buffer bumped from 768 → 1024 bytes (defensive against `state_class` field overflow on energy entity).

---

## `defaults.h` Layout

Per-board pin and feature defaults. New `ATHOM_PLUG_V3` arm joins existing per-board chain. Key blocks:

| Section | What changes |
|---------|--------------|
| LED defaults | New `#elif defined ATHOM_PLUG_V3` arm: PWM Inverted on GPIO6 |
| Button defaults | New `#if defined ATHOM_PLUG_V3` block: pin=3, type=0 (Pullup) |
| Relay defaults (gated by `HAS_RELAY`) | Athom: pin=5, restore=1 (Always On). Generic: pin=-1, restore=0 |
| CSE7766 defaults (gated by `HAS_POWER_MONITOR`) | Athom: rx=20, current_limit=16.0A, interval=10s. Generic: rx=-1, limit=0, interval=10 |
| I²C defaults | Athom: all -1 (no I²C). Plus generic ESP32-C3+CDC fix: gate GPIO 18/19 defaults behind `!ARDUINO_USB_CDC_ON_BOOT` (USB D-/D+ pins). |

---

## Existing Conventions to Preserve

- **Switch module is the template for Relay.** Don't change `Switch.cpp` — it's preserved as-is.
- **No direct cross-module mutator calls from peripherals.** Use `GUI::*` dispatchers (Button → GUI → Relay). CSE7766 → Relay::set(false) on trip is acceptable because trip is a safety path.
- **MQTT command chain is `else if`-based.** Order matters — modules later in the chain only see commands the earlier modules didn't claim.
- **All module-state files in SPIFFS use 1-byte payloads** (`"0"` or `"1"`) for boolean state. CSE7766 totals are an exception (text-encoded float).

---

## Reference Files

- Existing module template: `src/Switch.cpp` (used as the shape for `Relay.cpp`)
- LEDs throttle pattern (used by Relay state save): `src/LEDs.cpp:121` — `LEDs::Save()`
- main.cpp lifecycle insertion points: documented in `PRD/00-context-and-design.md` § "Item H — main.cpp lifecycle wiring"
