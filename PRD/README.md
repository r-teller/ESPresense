# Athom Smart Plug V3 — ESPresense Port PRD

This directory contains the Product Requirements Document for adding Athom Smart Plug V3 (ESP32-C3) support to ESPresense, including BLE detection, relay control, and CSE7766 power monitoring.

## Why this PRD exists

The full implementation spans ~600 lines of new C++ code, edits across 8 files, UI Svelte changes, CI matrix updates, and hardware verification. It is too large for a single Claude Code session to execute cleanly within token limits. This PRD is split into self-contained phase documents so each phase can be executed in its own session.

## How to use this PRD

Read documents in order. Each phase document is self-contained:
- States the prerequisites (what should be true before starting)
- Lists the steps with exact code/commands
- Defines the verification (how to know the phase is complete)
- Hands off to the next phase (what state should be true when handing off)

If you are starting a new Claude Code session for a phase:
1. Open the relevant `phase-N-*.md` document.
2. Run the "Status check" commands at the top to verify the previous phase is complete.
3. Execute the steps in order.
4. Run the verification at the end.
5. If anything is unclear, the canonical design lives in `00-context-and-design.md`.

## Document index

| Document | Purpose | Estimated effort |
|----------|---------|-----------------|
| [00-context-and-design.md](./00-context-and-design.md) | Project goal, all approved design decisions, deferred items, file map | Read-once reference |
| [phase-1-implementation.md](./phase-1-implementation.md) | Write all firmware + UI code, commit to branch | 12–16 hours |
| [phase-2-build-verification.md](./phase-2-build-verification.md) | Build all platformio envs, verify size, static analysis | 1–2 hours |
| [phase-3-hardware-testing.md](./phase-3-hardware-testing.md) | Flash to Athom plug, verify each feature on real hardware | 2–4 hours |
| [phase-4-pr-submission.md](./phase-4-pr-submission.md) | Push branch to fork, draft PR description, submit | 1–2 hours |

## Project goal

Add a new ESPresense build target `[env:athom-smart-plug-v3]` that:
- Runs on Athom Smart Plug V3 (ESP32-C3, 4MB flash) — preserves all existing ESPresense BLE detection capabilities
- Adds two new generic ESPresense modules — `Relay` (GPIO output with restore mode) and `CSE7766` (UART power monitor) — both gated behind `HAS_RELAY` and `HAS_POWER_MONITOR` build flags so they're reusable for future plug hardware
- Exposes the relay, power readings, energy total, current-limit configuration, and trip indicator as Home Assistant entities via MQTT discovery
- Allows physical button on GPIO3 to toggle the relay (short press) or factory-reset the device (4-second hold)

## Target repository

Branch will be pushed to **https://github.com/r-teller/ESPresense** (the user's fork), not directly upstream. PR is opened against the fork; future upstream PR is a separate decision after personal testing.

## Status

- ✅ Design phase complete (this PRD captures all decisions)
- ✅ Phase 1 — Implementation (complete 2026-05-02; 14/14 beads closed; epic ESPresense-nf5)
- ✅ Phase 2 — Build verification (complete 2026-05-02; 10/10 beads closed; epic ESPresense-k56). Local matrix 10/16 PASS — 6 esp32c6/s3 envs hit a pre-existing PlatformIO toolchain Python error unrelated to Phase 1; tracked in ESPresense-gww. CI on push is the authoritative gate.
- ⬜ Phase 3 — Hardware testing (epic ESPresense-8s8, 1/10 closed)
- ⬜ Phase 4 — PR submission (epic ESPresense-cly, 1/7 closed)
