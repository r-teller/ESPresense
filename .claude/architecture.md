# Architecture Overview

Purpose: High-level system context for quick orientation. Answers: "What is this? How does it fit together? How do I run it?"

> First file a new session should read. Detailed domain docs (`backend.md`, `frontend.md`, `data-model.md`, `security.md`, `tests.md`) go deeper.

---

## What We're Building

- **Project Name:** ESPresense — Athom Smart Plug V3 port
- **One-Sentence Summary:** Add a new ESPresense build target for the Athom Smart Plug V3 (ESP32-C3) that preserves BLE proximity detection while adding MQTT-controllable relay, CSE7766 power monitoring, overcurrent trip protection, and physical-button factory reset.
- **Programming Languages:**
  - **Firmware:** C++17 (Arduino / ESP-IDF via PlatformIO)
  - **UI:** TypeScript + Svelte 4
- **Main Frameworks/Tools:**
  - **Build:** PlatformIO (`pio`) — Arduino framework on ESP32 / ESP32-C3
  - **UI:** SvelteKit (static export) → embedded as C++ headers via `npm run build`
  - **Messaging:** MQTT (AsyncMqttClient) with Home Assistant discovery
  - **Persistence:** SPIFFS (filesystem-backed key/value files for state)
  - **Config UI:** HeadlessWiFiSettings captive portal (built into firmware)

---

## Product Guidance

### Vision

ESPresense is an ESP32-based BLE proximity-detection node for Home Assistant indoor positioning. This project ports it to the Athom Smart Plug V3 — a commodity HA-compatible smart plug — turning the plug into a dual-purpose device: BLE detection node AND smart switch with energy monitoring. Goal is upstream-quality, generic modules (`Relay`, `CSE7766`) gated behind feature flags so they're reusable for future plug-class hardware, not Athom-specific.

### Product Principles

1. **Generic-first, board-specific second.** New modules are gated behind `HAS_RELAY` / `HAS_POWER_MONITOR` build flags, not `ATHOM_PLUG_V3` board macros. Defaults flow through `defaults.h` so future plug boards inherit the work.
2. **Preserve all existing ESPresense behavior.** BLE detection, OTA, MQTT discovery, captive portal — none of it regresses on existing builds. Athom is an additive build target.
3. **Safety + recoverability over feature density.** Hard-coded 16A regulatory cap on overcurrent trip. Power-cycle-loop guard on stuck-on faults. 4-second button hold = factory reset (SPIFFS wipe → captive portal).
4. **Home Assistant native.** Every entity uses HA discovery; energy uses `state_class: total_increasing` for HA Energy dashboard; current limit is exposed as a `number` entity (not just a portal field) so it's tunable from HA.

### Explicit Exclusions

| Excluded Feature | Rationale | Status |
|-----------------|-----------|--------|
| `GUI::Relay()` on-device LED feedback | Ambiguous semantics — overlays BLE/WiFi/MQTT status on the single LED | Deferred (post-v1) |
| Pulse / momentary relay mode (timed-on) | Out of scope for v1 | Deferred |
| MQTT-triggered factory reset | Physical hold works; remote wipe needs threat-model review | Deferred |
| CSE7766 calibration multiplier overrides | Trust factory cal; only useful for non-Athom shunt resistors | Deferred |
| Per-build conditional UI rendering | UI shows all sections regardless of compile flags (existing pattern) | Permanent |
| Daily/weekly/monthly energy reset in firmware | HA `utility_meter` integration handles windowing (timezone + DST aware) | Permanent |

### Target Personas

| Persona | Description | Priority |
|---------|-------------|----------|
| End User (HA owner) | Owns an Athom Smart Plug V3, wants ESPresense BLE detection AND relay/energy in HA via MQTT | Primary |
| Developer (firmware contributor) | Maintains ESPresense; needs the new modules to be generic and upstream-quality | Primary |
| Developer (future plug porter) | Wants to port ESPresense to another plug-class device; reuses `Relay` + `CSE7766` modules | Secondary |
| System (HA / Energy dashboard) | Consumes MQTT discovery + telemetry; requires `state_class` for energy windowing | Secondary |

---

## Technology Decisions

| Category | Component | Version / Pin | Rationale |
|----------|-----------|---------------|-----------|
| **Chip** | Espressif ESP32-C3 | 4MB flash, single core, USB-CDC capable | Hardware target — Athom Smart Plug V3 |
| **Build env extends** | `esp32c3-cdc` | (existing in `platformio.ini`) | USB-CDC serial console works even without exposed USB-C port |
| **Board macro** | `ATHOM_PLUG_V3` | New | Gates board-specific defaults in `defaults.h` |
| **Feature flags** | `HAS_RELAY`, `HAS_POWER_MONITOR` | New | `HAS_` prefix avoids collision with library macros; flags drive module compilation |
| **Framework** | Arduino-ESP32 | (project default) | Existing ESPresense convention |
| **MQTT client** | AsyncMqttClient | (project default) | Existing convention |
| **Discovery** | Home Assistant MQTT discovery | (project default) | Native HA integration |
| **JSON buffer** | `DynamicJsonDocument` | 1024 bytes (was 768) | Defensive bump in `globals.h:34` to absorb new `state_class` field |
| **UI** | SvelteKit static export | (existing) | Compiled to C++ headers via `npm run build` |
| **Pin layout (Athom)** | GPIO3 button / GPIO5 relay / GPIO6 LED PWM-inverted / GPIO20 CSE7766 RX | Verified against vendor ESPHome yaml | See `00-context-and-design.md` |

> **Update strategy:** Firmware deps pinned via `platformio.ini`. Test build all envs (`pio run`) when bumping anything. UI deps via `npm` — run `npm run build` after any Svelte change to regenerate embedded headers.

---

## How to Run

### Prerequisites

- PlatformIO Core (`pio`) — `pip install platformio` or VS Code extension
- Node.js 18+ and npm (for UI builds)
- Athom Smart Plug V3 reachable on the local network (for OTA flash)

### Build firmware (Athom env)

```bash
# From repo root
pio-on && pio run -e athom-smart-plug-v3
```

Build artifact: `.pio/build/athom-smart-plug-v3/firmware.bin`

### Build all envs (CI parity)

```bash
pio-on && pio run                      # all envs
pio-on && pio run -e esp32c3-cdc       # quick sanity build
pio-on && pio run -e esp32             # ensure no regression on classic ESP32
```

### Rebuild UI (after Svelte edits)

```bash
cd ui && npm install && npm run build  # regenerates src/ui_*.h headers
cd .. && pio run -e athom-smart-plug-v3
```

> Both Svelte source AND the regenerated `src/ui_*.h` headers must be committed (see `AGENTS.md`).

### Flash to hardware

- **First flash:** OTA from factory ESPHome firmware via `http://<plug-ip>/update` upload form
- **Subsequent flashes:** ESPresense's own OTA — web UI, MQTT URL push, or auto-update from GitHub releases

### Environment Health Checks

Used by `/leroy` and `/gogogo` to verify the dev environment is ready.

| Service | Check Command | Expected | If missing |
|---------|--------------|----------|------------|
| PlatformIO | `.venv-pio/bin/pio --version 2>/dev/null \|\| pio --version` | `PlatformIO Core, version X.Y.Z` | Bootstrap project-local venv: `python3 -m venv .venv-pio && .venv-pio/bin/pip install platformio`. Either invoke `.venv-pio/bin/pio` directly or alias `pio-on='source .venv-pio/bin/activate'`. |
| Node / npm | `node --version && npm --version` | both print versions | Install via system package manager or nvm. |
| UI build outputs | `test -f src/ui_html.h && echo ok` | `ok` | Run `cd ui && npm install && npm run build` to regenerate headers. |
| Athom env builds | `.venv-pio/bin/pio run -e athom-smart-plug-v3 -t checkprogsize` | succeeds, prints flash usage | If linker errors: verify `src/Relay.cpp/.h` and `src/CSE7766.cpp/.h` exist (nf5.6/nf5.7) and `[env:athom-smart-plug-v3]` is in `platformio.ini` (nf5.5). |
| Branch | `git branch --show-current` | `feature/athom-plug-v3` | If on `main`: `git checkout feature/athom-plug-v3`. |
| r-teller remote | `git remote -v \| grep r-teller` | shows fork URL | `git remote add r-teller https://github.com/r-teller/ESPresense.git && git fetch r-teller`. |

---

## System Architecture

```
Athom Smart Plug V3 (ESP32-C3)
┌──────────────────────────────────────────────────────────────────┐
│                                                                  │
│   ┌──────────┐   ┌──────────┐   ┌──────────────┐                 │
│   │  Button  │──▶│   GUI    │──▶│  Relay (out) │── GPIO5 → load  │
│   │ (GPIO3)  │   │dispatcher│   └──────┬───────┘                 │
│   └──────────┘   └─────┬────┘          │                         │
│                         │      clearTrip│                         │
│                         │ longHold(4s)  │                         │
│                         ▼               │                         │
│                  SPIFFS.format()        │                         │
│                  + ESP.restart()        ▼                         │
│                                  ┌─────────────┐                  │
│                                  │   CSE7766   │◀─── GPIO20 UART  │
│                                  │ (V/I/W/PF/E)│   (4800 8E1)     │
│                                  └──────┬──────┘                  │
│                                         │                         │
│   ┌────────────┐    ┌─────────────┐     │                         │
│   │ BLE scan   │──▶│ MQTT client │◀────┘ telemetry / discovery   │
│   │(unchanged) │    │ AsyncMQTT   │                               │
│   └────────────┘    └──────┬──────┘                               │
│                            │                                      │
└────────────────────────────┼──────────────────────────────────────┘
                             ▼
                   Home Assistant broker
                   (relay switch + 7 sensors + current-limit number
                    + relay-trip binary_sensor + BLE proximity)
```

`Button.cpp` calls only `GUI::ButtonPressed()` / `GUI::ButtonLongPressed()` — never directly into `Relay`. Preserves ESPresense's existing layering.

---

## Directory Structure

```
ESPresense/
├── src/                  # Firmware C++ source (modules + main)
│   ├── main.cpp/.h       # Lifecycle orchestration
│   ├── Relay.cpp/.h      # NEW — generic relay module (HAS_RELAY)
│   ├── CSE7766.cpp/.h    # NEW — power monitor module (HAS_POWER_MONITOR)
│   ├── Button.cpp        # Edge-detection state machine + long-press
│   ├── GUI.cpp/.h        # ButtonPressed / ButtonLongPressed dispatch
│   ├── mqtt.cpp/.h       # Discovery helpers (extended for state_class + bounded numbers)
│   ├── globals.h         # Shared JsonDocument buffer
│   └── ui_*.h            # Generated from ui/ — DO NOT hand-edit
├── include/
│   └── defaults.h        # Per-board pin/feature defaults (ATHOM_PLUG_V3 arm)
├── ui/                   # SvelteKit source — compiles to src/ui_*.h
│   └── src/routes/hardware/+page.svelte   # Adds Relay + Power Monitor sections
├── lib/                  # Vendored libraries
├── test/                 # PlatformIO test scaffolding
├── platformio.ini        # Build envs (new: [env:athom-smart-plug-v3])
├── partitions_singleapp.csv
├── PRD/                  # Phase-split implementation plan (this project)
│   ├── 00-context-and-design.md   # Design canon — single source of truth
│   ├── phase-1-implementation.md
│   ├── phase-2-build-verification.md
│   ├── phase-3-hardware-testing.md
│   └── phase-4-pr-submission.md
├── .github/workflows/build.yml   # CI matrix (add athom-smart-plug-v3)
└── .claude/              # Claude Code context (this directory)
```

---

## Deployment

- **Hosting:** Self-hosted on user hardware (Athom Smart Plug V3)
- **Distribution:** GitHub Releases — auto-update via ESPresense's OTA mechanism (web UI / MQTT URL push / GitHub-release auto-update)
- **CI/CD:** GitHub Actions (`.github/workflows/build.yml`) — builds all platformio envs on every push
- **Branch / target repo:** Work happens on `feature/athom-plug-v3` against `https://github.com/r-teller/ESPresense` (user's fork). Upstream PR is a separate decision after personal hardware testing.
- **Secrets:** None in repo. WiFi/MQTT creds entered via captive portal, stored in SPIFFS on-device.

---

## Related Context Files

- **`backend.md`** — firmware module map, lifecycle hooks, MQTT discovery helpers, cross-module API
- **`frontend.md`** — Svelte hardware-config page, embedded UI build pipeline
- **`data-model.md`** — SPIFFS files, MQTT topic topology, HA discovery entity layout
- **`security.md`** — overcurrent trip safety, factory reset, OTA flash path, secret handling
- **`tests.md`** — build verification matrix, hardware-on-the-bench checklist, pre-existing bugs noted
