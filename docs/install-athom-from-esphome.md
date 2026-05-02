# Flashing ESPresense onto an Athom Smart Plug V3 (from factory ESPHome)

This guide covers converting an Athom Smart Plug V3 from its factory ESPHome firmware to ESPresense **without opening the case**. The Athom V3 has no exposed USB port; the ESP32-C3's USB-Serial-JTAG pins are only reachable via internal test pads, so a no-USB upgrade path matters.

> **TL;DR:** The safest no-USB path is **Tasmota intermediate** — flash Tasmota first via ESPHome's web UI, then ESPresense via Tasmota's web upload. This produces a clean partition layout and gives you a known-recoverable fallback at every step.

---

## Hardware

| Field | Value |
|-------|-------|
| Model | Athom Smart Plug V3 |
| SoC | ESP32-C3 (RISC-V, single-core, USB-Serial-JTAG) |
| Flash | 4 MB |
| Stock firmware | ESPHome (Athom factory image) |
| Relay GPIO | GPIO5 |
| CSE7766 power monitor RX | GPIO20 (UART RX-only, 4800 8E1) |
| Status LED | GPIO6 (PWM-Inverted) |
| Button | GPIO3 (active LOW, internal pullup) |

---

## Pre-flash checklist

Before starting, verify each of these. **Do not proceed until all are ticked.**

- [ ] **Plug is on your Wi-Fi and you can reach its web UI.** Open `http://<plug-ip>/` in a browser. ESPHome's dashboard or upload form should load.
- [ ] **You have the Wi-Fi SSID/password handy** — both the new firmware first-boot and any intermediate Tasmota will need it.
- [ ] **You have an MQTT broker accessible from the plug's network** — host:port, optional username/password. ESPresense requires MQTT for its primary BLE-presence output.
- [ ] **You have downloaded the ESPresense firmware artifacts** (see "Getting the binaries" below).
- [ ] **You understand the rollback story** — at the time of writing, the ESPresense build for `athom-smart-plug-v3` does **not** have bootloader-level OTA rollback enabled. A failed first boot does not auto-revert; recovery requires opening the case to reach the UART pads. Plan accordingly.

---

## Getting the binaries

After a successful build (`pio run -e athom-smart-plug-v3`), three files are produced under `.pio/build/athom-smart-plug-v3/`:

| File | Offset | Size | Purpose |
|------|--------|------|---------|
| `bootloader.bin` | `0x0` | ~11 KB | ESP-IDF 2nd-stage bootloader |
| `partitions.bin` | `0x8000` | ~3 KB | Compiled partition table (4MB layout, dual OTA) |
| `firmware.bin` | `0x10000` | ~1.2 MB | App image |

For CI builds, the artifact uploaded by the matrix job (`athom-smart-plug-v3.bin`) is the merged single-image binary suitable for direct upload via Tasmota or ESPHome web UIs.

> **Important:** the merged single-file image and the three separate files are not interchangeable. The single-file `.bin` includes bootloader + partitions + firmware combined and is what most web UIs (Tasmota, ESPHome) expect for full-image upload.

---

## Path A — Tasmota intermediate (recommended)

This is the most reliable no-USB path. It costs ~10 extra minutes but gives you a known-recoverable fallback at each step.

### Step 1 — Web-OTA from ESPHome to Tasmota

1. Download `tasmota32c3-factory.bin` from <https://github.com/arendst/Tasmota/releases> (latest stable). The `-factory` suffix indicates a full-image binary including bootloader and partitions.
2. Open the plug's ESPHome web UI in a browser.
3. Locate the OTA Update form (typically a "Choose File" upload field on the dashboard).
4. Upload `tasmota32c3-factory.bin`. ESPHome will write to the inactive OTA slot and reboot.
5. The plug's Wi-Fi SSID changes — Tasmota broadcasts an open AP named `tasmota-XXXX`. Connect to it.
6. Browse to `http://192.168.4.1/`. Configure your home Wi-Fi credentials. The plug reboots onto your home network.
7. Find the plug's new IP via your router or `ping tasmota-XXXX.local`. Open its web UI.

You now have **a known-good fallback**: Tasmota's web UI is always reachable as long as the firmware boots and connects to Wi-Fi.

### Step 2 — Tasmota → ESPresense

1. In the Tasmota web UI, go to **Firmware Upgrade**.
2. Select **Upgrade by file upload** and choose the merged `athom-smart-plug-v3.bin`.
3. Click **Start upgrade**. Tasmota writes the full image (bootloader + partitions + firmware), reboots.
4. The plug's Wi-Fi SSID changes again — ESPresense broadcasts an open AP named `ESPresense-XXXX`.
5. Connect, browse to `http://192.168.4.1/`, configure Wi-Fi + MQTT.

### Why this path is safer

- **Partition table is rewritten.** Direct ESPHome→ESPresense web OTA only writes the app slot, leaving ESPHome's partition table in place. ESPresense's `SPIFFS.begin()` then fails because there is no `spiffs` partition in the running table. Tasmota's full-image upload rewrites the partition table, eliminating this issue.
- **Tasmota is widely battle-tested on the Athom V3.** Many community guides cover Athom→Tasmota conversion; if step 1 fails, you have a documented community recovery path.
- **Two-stage commits.** If anything fails between ESPHome→Tasmota or Tasmota→ESPresense, you stop on a working firmware. Direct one-shot is all-or-nothing.

---

## Path B — Direct ESPHome web OTA (faster, less safe)

Only attempt if you accept the risks below.

1. Open the plug's ESPHome web UI.
2. Upload the merged `athom-smart-plug-v3.bin`. Reboot.
3. Connect to the new `ESPresense-XXXX` AP.

### Risks of this path

- **Partition mismatch.** ESPHome's web OTA writes only the app image. ESPresense's runtime then uses ESPHome's partition table at offset `0x8000`. ESPresense's `spiffs` partition does not exist in ESPHome's typical layout (which uses `littlefs` at a different offset). Result: settings persistence (relay state file `/relay_state`, OTA state, MQTT broker URL) silently fails to write to flash. The captive portal still works (settings live in NVS, which both layouts share at the same offset), but reboots lose any state stored in SPIFFS.
- **No automatic rollback** if ESPresense fails to boot. Recovery requires opening the case.
- **Mixed bootloader.** You retain ESPHome's bootloader. For ESP32-C3 with stable IDF major versions this is generally fine, but it is not the validated configuration.

### How to detect partition mismatch after flash

Browse to `http://<esp-ip>/wifi/hardware` after the device boots. If the JSON includes the new `relay_pin`, `relay_restore_mode`, `cse7766_*` keys with sensible defaults (relay_pin=5, etc.), the firmware booted. If you toggle the relay and reboot — the relay's last state should restore via the `relay_restore_mode` setting. If state is lost across reboots, that's the partition-mismatch symptom.

---

## Path C — USB recovery (case-cracking required)

If both A and B leave you in a bad state, this is the recovery floor. Reference only — not part of the normal flow.

1. Open the plug's case (it's clip-fit with no screws on most batches; use a guitar pick around the seam).
2. Locate the UART test pads on the PCB. Athom provides a small header with TX, RX, GND, and 3V3.
3. Connect a USB-UART adapter (CP2102, CH340G, or FT232 — any 3.3V-logic USB serial works).
4. Hold GPIO9 (boot strap) LOW while powering up to enter the ESP32-C3 ROM bootloader.
5. Use esptool to flash all three files at their canonical offsets:

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 460800 \
  write_flash \
    0x0 bootloader.bin \
    0x8000 partitions.bin \
    0x10000 firmware.bin
```

6. Reboot. The plug now runs ESPresense with the correct partition table.

> **Safety:** the plug's relay carries mains. Do **not** attempt to flash with the plug connected to mains power. Use the USB-UART adapter as the sole power source (3.3V) during recovery.

---

## First-boot configuration

Regardless of path, ESPresense's first boot drops into a captive portal:

1. Connect to the open AP `ESPresense-XXXX`.
2. Browse to `http://192.168.4.1/`.
3. Configure:
   - **Wi-Fi:** your home network SSID + password.
   - **Hostname / Room:** a unique name per plug (used as MQTT topic prefix).
   - **MQTT broker:** host, port (1883 for plain, 8883 for TLS), username, password.
   - **Hardware (advanced):** the new sections include:
     - **Relay** — `relay_pin = 5`, `relay_restore_mode = 1` (Always On) on the Athom V3 by default.
     - **Power Monitor** — `cse7766_rx_pin = 20`, `cse7766_current_limit = 16.0` A, `cse7766_update_interval = 10` s.
4. Click **Save**. The plug reboots and connects to your network.
5. The relay should energize after `relay_restore_mode = 1` takes effect (Always On). The button on the plug (GPIO3) toggles the relay on tap; a 4-second hold triggers SPIFFS format + restart (factory reset back into captive portal).

---

## Verifying the install

After the plug joins your network:

| Check | Expected |
|-------|----------|
| Web UI reachable at `http://<plug-ip>/` | Yes, ESPresense dashboard renders |
| `/wifi/hardware` JSON includes new keys | `relay_pin`, `relay_restore_mode`, `cse7766_rx_pin`, `cse7766_current_limit`, `cse7766_update_interval` all present |
| MQTT discovery in Home Assistant | Switch entity (`switch.<room>_relay`), sensor entities for power/voltage/current/energy/trip status |
| Relay tap | Single press toggles relay; serial log prints `Button 1 short-pressed` |
| Relay long-press | 4-second hold formats SPIFFS and reboots into captive portal |
| Power monitor (load attached) | `<room>/power`, `<room>/voltage`, `<room>/current`, `<room>/energy` MQTT topics publish every `cse7766_update_interval` seconds |
| Boot persistence | After reboot, relay returns to its `relay_restore_mode` state (or last state if mode = `Restore Last`) |

---

## Rolling back to ESPHome / Tasmota

If you want to leave ESPresense and return to your previous firmware:

- **From ESPresense web UI:** the Update page accepts a URL or upload of an arbitrary ESP32-C3 firmware. Upload `tasmota32c3-factory.bin` (or the original Athom ESPHome image if you saved it) — ESPresense's `Updater.cpp` writes it to the inactive OTA slot and reboots.
- **From the physical button:** 4-second hold formats SPIFFS and reboots back into the captive portal — but the firmware is still ESPresense. The button is not a rollback to ESPHome.
- **From a brick:** Path C above (case + USB-UART).

---

## Known limitations

- **No bootloader-level OTA rollback** in the current build. `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` is unset because arduino-esp32's pre-built bootloader does not enable it. Adding this requires switching the build to ESP-IDF-as-component mode, which is tracked separately.
- **Single-radio chip.** ESP32-C3 has one BLE/Wi-Fi radio. ESPresense scans BLE while connected to Wi-Fi via the radio scheduler, which is reliable but introduces small scan gaps. This is identical to all single-radio ESPresense targets.
- **Power monitor is RX-only.** CSE7766 reports power data via UART continuously. The chip has no commands; calibration values are baked into the protocol stream.
- **First-boot stack unwind.** If your Wi-Fi password is wrong or the MQTT broker is unreachable, the plug stays in captive portal on the open AP. It does not crash. There is no SoftAP password by default — set one via the captive portal if your environment requires it.

---

## Reference

- Phase 1 implementation PRD: `PRD/phase-1-implementation.md`
- Phase 2 build verification PRD: `PRD/phase-2-build-verification.md`
- Project context and design decisions: `PRD/00-context-and-design.md`
- Build environment: `[env:athom-smart-plug-v3]` in `platformio.ini`
- Source modules: `src/Relay.cpp`/`.h`, `src/CSE7766.cpp`/`.h`, `src/Button.cpp`, `src/GUI.cpp`/`.h`
