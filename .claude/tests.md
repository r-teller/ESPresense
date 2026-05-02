# Testing Strategy

Purpose: How this project verifies correctness across three layers — compile/build, on-bench hardware, and HA integration. Embedded firmware has limited unit-test surface; testing is dominated by build verification + scripted hardware checks.

---

## Test layers

| Layer | Tool | Where | Phase |
|-------|------|-------|-------|
| **Build verification** | `pio run` (PlatformIO) | local + CI (`.github/workflows/build.yml`) | Phase 2 |
| **Static analysis** | `pio check` (cppcheck/clang-tidy if configured) | local | Phase 2 |
| **Firmware-size check** | `pio run -t checkprogsize` | local + CI | Phase 2 |
| **Hardware on-the-bench** | Manual scripted checklist on real Athom plug | local | Phase 3 |
| **HA integration** | Home Assistant + MQTT broker on the bench | local | Phase 3 |

Unit tests for individual modules are **out of scope** for this PR — ESPresense convention is integration testing on hardware. Future work could mock `Serial1` for CSE7766 packet-parser unit tests; not done here.

---

## Build verification matrix (Phase 2)

All envs must build clean:

```bash
pio run -e athom-smart-plug-v3   # NEW env — primary target
pio run -e esp32                 # ensure no regression on classic ESP32
pio run -e esp32c3-cdc           # ensure CDC env still builds (touched by I²C fix)
pio run                          # full matrix as a final pass
```

Warnings introduced by the new modules must be reviewed and resolved before Phase 3.

### Firmware size

`pio run -e athom-smart-plug-v3 -t checkprogsize` — confirm flash usage fits the 4MB partition layout in `partitions_singleapp.csv`. Capture absolute and percentage values for the PR description.

### CI matrix

`.github/workflows/build.yml:16` env list updated to include `athom-smart-plug-v3` (end of list, family-grouping convention). CI must pass on the feature branch before PR is opened.

---

## Hardware on-the-bench checklist (Phase 3)

Run on a real Athom Smart Plug V3 after OTA from factory ESPHome firmware. Each item is its own pass/fail check.

### Boot + BLE regression

- [ ] Device boots, connects to WiFi via captive portal
- [ ] BLE detection still works — known iBeacon / Tile detected + reported via MQTT (no regression)
- [ ] Status LED behaves correctly (PWM Inverted on GPIO6 — active LOW)

### Relay control

- [ ] Relay defaults to **Always On** at first boot (matches ESPHome behavior)
- [ ] MQTT `<roomsTopic>/relay/set` `ON`/`OFF`/`TOGGLE` drives relay
- [ ] `<roomsTopic>/relay` retained state matches actual relay
- [ ] HA discovery: relay appears as a `switch` entity
- [ ] Button tap (release < 1s) toggles relay
- [ ] After power-cycle in Restore Last mode, relay returns to last state
- [ ] After power-cycle in Always On mode, relay turns on

### Power monitoring

- [ ] With load attached, V/I/W/PF/energy values appear in HA
- [ ] Telemetry JSON arrives every 10 seconds (default `updateIntervalSec`)
- [ ] Power factor reads as `XX.X %` (× 100 in firmware)
- [ ] Energy entity has `state_class: total_increasing` (verify via HA dev-tools)
- [ ] Energy total survives reboot (within 0–30 Wh of pre-reboot value)
- [ ] HA Energy dashboard accepts the device (visible in source dropdown)

### Overcurrent trip

- [ ] With limit set below test-load draw: relay trips within ~100ms (2 packets at 50ms)
- [ ] `<roomsTopic>/relay_trip` retained `ON` after trip; `binary_sensor` shows "Detected" with `device_class: problem`
- [ ] Manual re-engage (button or MQTT `relay/set ON`) clears trip
- [ ] Re-arm requires another 4 valid packets after re-engage
- [ ] Power-cycle-loop guard: simulate stuck `/relay_tripped` + Always On restore mode → relay comes up OFF
- [ ] Setting limit to `0` via HA `number` entity disables tripping

### Factory reset

- [ ] 4-second hold of Button 1 → SPIFFS wiped, device reboots into captive portal
- [ ] Holding > 4s does not double-format
- [ ] Button 2 long-press logs only (no factory reset)
- [ ] Existing `<roomsTopic>/button_1` ON/OFF state messages still publish (preserved behavior)

### Boot-glitch (qualitative)

- [ ] No audible relay click between power-on and `Setup()` complete (confirms `EarlyInit()` drives pin LOW first)

---

## Pre-existing bugs to be aware of (NOT fixed in this PR)

These will surface during testing — they're not regressions:

- `sendSwitchDiscovery` (`mqtt.cpp:166`) writes `entity_category` even when empty
- `*_Timeout` number entities have hardcoded `step: "0.1"`, wrong for second-granularity timeouts
- Duplicate `Motion::Command` line at `main.cpp:386`

If any of these change behavior in this PR, that's a new bug — file it.

---

## What we are NOT testing

- BLE-internal correctness — preserved-as-is, regression check is "still detects known beacon"
- Other sensor modules (BME280, AHTX0, etc.) — disabled on Athom build
- Cross-board behavior — generic defaults are validated by CI build matrix only
- Unit-level packet parser correctness — covered by hardware integration testing

---

## Reference

- Build matrix: `.github/workflows/build.yml`
- Partition layout: `partitions_singleapp.csv`
- Phase docs: `PRD/phase-2-build-verification.md`, `PRD/phase-3-hardware-testing.md`
