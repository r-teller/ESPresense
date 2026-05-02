# Phase 4 — PR Submission

**Goal**: Push the working branch to the user's fork (`github.com/r-teller/ESPresense`), open a pull request with a thorough description, attach hardware test evidence.

**Estimated effort**: 1–2 hours

**Prerequisites**:
- Phase 3 complete (`PRD/phase-3-handoff.md` exists with test evidence)
- All Phase 3 verification gates ✅
- GitHub CLI (`gh`) authenticated for the user (run `gh auth status` to verify) OR ability to create PR via web UI
- Branch `feat/athom-smart-plug-v3` is the active branch

## Status check (run before starting)

```bash
cd /opt/git/personal/ESPresense
git branch --show-current             # should be feat/athom-smart-plug-v3
git status                            # should be clean
git log --oneline main..HEAD          # should show ~11 commits
ls PRD/phase-3-handoff.md             # should exist
gh auth status 2>&1 | head -5         # confirm GitHub auth (optional but useful)
git remote -v                         # should show r-teller fork remote
```

If `r-teller` remote is missing:
```bash
git remote add r-teller https://github.com/r-teller/ESPresense.git
```

## Step 1 — Final pre-push verification (10 min)

Run one last build to confirm nothing's drifted:

```bash
cd /opt/git/personal/ESPresense
pio run -e athom-smart-plug-v3
ls -la .pio/build/athom-smart-plug-v3/firmware.bin
```

Capture commit list for the PR:

```bash
git log --oneline main..HEAD > /tmp/pr-commits.txt
cat /tmp/pr-commits.txt
```

Expected: ~11 commits, each with a clear conventional-commit-style message.

## Step 2 — Push to fork (5 min)

```bash
git push -u r-teller feat/athom-smart-plug-v3
```

Expected output: GitHub URL like `https://github.com/r-teller/ESPresense/pull/new/feat/athom-smart-plug-v3`.

If push fails:
- `unable to access`: check authentication (`gh auth status`, or set up SSH key)
- `non-fast-forward`: someone else pushed to the same branch — fetch and reconcile
- `fork does not exist`: confirm the fork exists at `github.com/r-teller/ESPresense`. If not, the user needs to create it via GitHub UI first.

## Step 3 — Open the PR (15 min)

The user wants the PR opened against their fork's `main` branch (their personal copy). Future upstream submission to `ESPresense/ESPresense` is a separate decision.

### PR title

`Add Athom Smart Plug V3 support: relay, power monitor, BLE`

(Under 70 chars per CLAUDE.md guidance.)

### PR description template

Copy this into the PR body, filling in the `[FILL IN]` sections from `PRD/phase-3-handoff.md`:

```markdown
## Summary

Adds support for the Athom Smart Plug V3 (ESP32-C3) — preserves BLE proximity
detection while exposing the device's relay and CSE7766 power monitor as
ESPresense-managed peripherals with full Home Assistant integration.

## What's added

- **New build target**: `[env:athom-smart-plug-v3]` (extends `esp32c3-cdc`)
- **New module `Relay`** — GPIO output with restore mode (Off / On / Restore Last)
  - Two-stage init: `EarlyInit()` drives GPIO LOW first thing in `setup()` to
    avoid boot-glitch on undriven pins; `Setup()` later applies portal-configured
    restore mode
  - State persisted to SPIFFS with throttled writes
- **New module `CSE7766`** — UART power monitor at 4800 8E1 RX-only
  - Validates every 24-byte packet against sync bytes + checksum
  - Honors per-channel validity bits in the ADJ byte (silent-hold pattern matches ESPHome)
  - JSON-coalesced telemetry every 10s (configurable 1-600s) to one MQTT topic
    instead of 6 — prevents backpressure on slow brokers
  - Energy accumulator with `total_increasing` state class for HA Energy dashboard
  - Persisted to SPIFFS every 5 min when delta ≥ 10 Wh (flash-wear conscious)
- **Overcurrent auto-trip**: 5s boot grace + 4 valid packets armed + 2-packet
  over-limit debounce (~100ms response). Latching — only manual relay re-engage
  clears it. Trip state persisted to SPIFFS to prevent power-cycle loops.
- **Button long-press → factory reset** (4-second hold). Generic feature on any
  ESPresense board with a configured button — provides physical recovery for
  users locked out by changed WiFi/MQTT config.
- **Discovery helper extensions** in `mqtt.cpp`:
  - `sendSensorDiscovery` + `sendTeleSensorDiscovery` — added `stateClass` arg
  - `sendNumberDiscovery` — added `min`/`max`/`step`/`units`/`mode` args with
    NaN sentinels so existing callers preserve behavior
- **Bug fix bundled**: I²C defaults on `esp32c3-cdc` no longer collide with
  USB-CDC. Previously `DEFAULT_I2C_BUS_1_SDA=19, SCL=18` (which are the C3's
  USB D-/D+ pins) would silently brick USB-CDC the moment a user enabled
  any I²C sensor.

## What's NOT in scope (deferred to follow-up issues)

- On-device LED feedback for relay state — overlays BLE/WiFi/MQTT status on
  the single LED, ambiguous semantics. Skip v1.
- Pulse / momentary mode for the relay (timed-on for X minutes).
- Daily / weekly / monthly energy windows in firmware. Recommend HA's
  `utility_meter` integration which handles timezone + DST correctly.
- CSE7766 calibration multiplier overrides — for boards with non-Athom shunt
  resistors. Trusting factory cal in v1.
- MQTT-triggered factory reset command — physical button hold works.

## How to use

1. Flash from factory ESPHome via OTA (most Athom V3 units lack a USB-C port):
   - Browse to `http://<plug-ip>/` and upload `firmware.bin` via the web UI, OR
   - `curl -X POST -F "file=@firmware.bin" http://<plug-ip>/update`
   - For units with USB-C: standard `pio run -e athom-smart-plug-v3 -t upload` also works
2. Connect to the device's WiFi AP `ESPresense-XXXXXX`, configure WiFi + MQTT
3. The captive portal hardware page now shows two new sections:
   - **Relay**: configure pin and restore mode
   - **Power Monitor**: configure UART RX pin, current limit (0-16A), and update interval
4. HA auto-discovers:
   - 1 switch (relay)
   - 7 sensors (V/I/W/VA/VAR/PF/kWh)
   - 1 number entity (current limit)
   - 1 binary_sensor (relay trip with `device_class: problem`)
5. For daily/weekly/monthly energy reports, add HA's `utility_meter`:
   ```yaml
   utility_meter:
     daily_energy:
       source: sensor.athom_plug_total_energy
       cycle: daily
   ```

## Hardware testing performed

[FILL IN from PRD/phase-3-handoff.md — specifically:]

- [ ] Captive portal showed new sections — screenshot attached
- [ ] All 10 HA entities discovered correctly
- [ ] Relay toggles via MQTT, web UI, physical button
- [ ] Restore mode works for all 3 settings
- [ ] Power readings sensible for known load (e.g., [FILL IN: e.g., "60W lamp shows 119.8V / 0.504A / 60.4W / PF 99%"])
- [ ] Total energy accumulates correctly (verified [FILL IN: e.g., "30 min @ 60W → 0.030 kWh"])
- [ ] Reboot restores total_energy from SPIFFS within 10 Wh delta
- [ ] Current-limit trip fires within ~100ms (verified with [FILL IN load])
- [ ] Trip persists across reboot — relay stays OFF on next boot
- [ ] Manual ON clears trip and re-arms detection
- [ ] 4-second button hold triggers factory reset → captive portal returns
- [ ] BLE detection still works alongside power monitoring
- [ ] [Optional] 24-hour soak: no crashes, free heap stable

## Compile-time verification

All 16 platformio environments still build cleanly (`pio run -e <env>`).
Firmware size for athom-smart-plug-v3: [FILL IN bytes from phase-2-handoff] / 1920 KB cap.

## Pre-existing bugs flagged (not fixed here)

These were observed during the review process but are unrelated to this PR.
Filing as separate issues:

- `sendSwitchDiscovery` (mqtt.cpp:166) writes `entity_category` even when empty
  (other helpers guard with `isEmpty()`).
- `*_Timeout` number entities (Switch/Button/Motion) hardcode `step: "0.1"`,
  which is wrong for second-granularity timeouts.
- Duplicate `Motion::Command` line at `main.cpp:386`.

## Documentation needs

The captive portal `<h2>` headers link to `https://espresense.com/configuration/settings#relay`
and `#power-monitor` — those anchors don't exist yet on the docs site. Filing
a docs PR after this lands.

## Compatibility

All existing envs continue to build. No breaking changes to MQTT topology
or JSON schemas. New modules gated by `HAS_RELAY` / `HAS_POWER_MONITOR`
build flags — generic and reusable for any future plug-style hardware.

## Commits

[FILL IN from /tmp/pr-commits.txt]

🤖 Generated with [Claude Code](https://claude.com/claude-code)
```

### Open the PR via gh CLI

If the PR target is the user's fork main branch:

```bash
cd /opt/git/personal/ESPresense

# Save the PR body to a tempfile so HEREDOC works cleanly
cat > /tmp/pr-body.md <<'EOF'
[Paste the filled-in description from above here]
EOF

gh pr create \
    --repo r-teller/ESPresense \
    --base main \
    --head feat/athom-smart-plug-v3 \
    --title "Add Athom Smart Plug V3 support: relay, power monitor, BLE" \
    --body-file /tmp/pr-body.md \
    --draft
```

`--draft` opens the PR in draft mode, which signals "feedback wanted but not strictly merge-ready." This makes sense for a feature this size — gives the user a chance to review before marking ready.

If the user prefers a non-draft PR (since this is their own fork, they may just want to merge themselves), drop `--draft`.

If `gh` is not available, open the PR via the GitHub web UI:
1. Browse to `https://github.com/r-teller/ESPresense/compare/main...feat/athom-smart-plug-v3`
2. Click "Create pull request"
3. Paste title and body
4. Mark as draft (or not)
5. Submit

## Step 4 — Attach hardware test evidence (15 min)

The PR body has placeholders for test evidence. Attach as comments on the PR:

### Screenshots to attach

1. **Captive portal hardware page** — showing the new Relay and Power Monitor sections with correct defaults populated
2. **Home Assistant entity list** — showing the 10 new entities under the device
3. **HA dashboard with live values** — voltage/current/power readings on a known load
4. **HA history graph** — total_energy ramping over the test window
5. **Serial log excerpt** — showing a successful trip event with timestamp + values
6. **Serial log excerpt** — showing factory reset triggered by 4s hold

### How to attach

Either:
- Drag-and-drop into the PR description (GitHub uploads to its CDN automatically)
- OR add as a follow-up comment on the PR with `gh pr comment <PR#> --body-file <file>`

If screenshots aren't available (e.g., headless test environment), document equivalent text:
```
HA entity discovery confirmed via mosquitto_sub:
$ mosquitto_sub -h localhost -t 'homeassistant/+/espresense_+/+/config' -v | head -10
[paste output]
```

## Step 5 — Optional: upstream submission decision

The user's stated goal is upstream PR target, but for now they're pushing to their own fork. After Phase 4 completes:

- Test the firmware in production for 30+ days
- If stable: open a second PR against `ESPresense/ESPresense:main` (the upstream)
- Use the same PR description (slightly trimmed if needed)
- Tag DTTerastar (the maintainer) for review
- Be prepared for 2-3 rounds of feedback on naming/style/layering

To open the upstream PR later:
```bash
cd /opt/git/personal/ESPresense
git push origin feat/athom-smart-plug-v3      # push to upstream (if write access exists)
# OR
gh pr create --repo ESPresense/ESPresense --head r-teller:feat/athom-smart-plug-v3 ...
```

(If user doesn't have write access to upstream, they'd push from their fork and `gh` knows how to create a cross-repo PR with `--head r-teller:feat/athom-smart-plug-v3`.)

This is OPTIONAL and not required for Phase 4 to be considered complete.

## Step 6 — Update PRD status (5 min)

```bash
cd /opt/git/personal/ESPresense
```

Edit `PRD/README.md`, mark Phase 4 done:

```
- ✅ Phase 1 — Implementation
- ✅ Phase 2 — Build verification
- ✅ Phase 3 — Hardware testing
- ✅ Phase 4 — PR submission
```

Create `PRD/phase-4-handoff.md` with:
- PR URL
- PR number
- Status (draft / ready / merged)
- Any reviewer feedback received (if reviewed during this session)
- Follow-up issues to file (deferred features, pre-existing bugs)
- Decision on upstream submission (yes/no/wait)

## Step 7 — Optional follow-ups (file as separate issues)

Per the design canon (`PRD/00-context-and-design.md`), these were explicitly deferred:

1. **GUI::Relay() for v2** — on-device LED feedback when relay state changes
2. **Pulse/momentary mode** — `relay/set "ON_FOR_5MIN"` style timed activation
3. **MQTT factory reset command** — remote reset without physical button
4. **CSE7766 calibration overrides** — per-board V/I multipliers for non-Athom hardware
5. **Pre-existing bugs** flagged in PR description — `sendSwitchDiscovery` empty entity_category, timeout step values

Use `gh issue create` to file them on the user's fork:

```bash
gh issue create \
    --repo r-teller/ESPresense \
    --title "v2: GUI::Relay() for on-device LED feedback" \
    --body "Deferred from initial Athom plug PR. See PRD/00-context-and-design.md ..."
```

(Repeat for each.)

## Phase 4 verification gate

- [ ] Branch pushed to `r-teller/ESPresense` successfully
- [ ] PR created (draft or ready)
- [ ] PR description filled in with hardware test results from Phase 3
- [ ] Screenshots attached
- [ ] PR URL captured in handoff
- [ ] Follow-up issues filed (or noted in handoff for later)
- [ ] PRD status updated to all-green

## Project complete

When this phase is done:
- Working firmware exists for the Athom Smart Plug V3
- All design decisions captured in PRD canon
- All work organized in 11 reviewable commits
- Hardware verified end-to-end
- PR opened on user's fork, ready for personal merge or further iteration

The user can:
- Use the firmware on their personal Athom plugs
- Iterate based on real-world experience
- Eventually open an upstream PR after a soak period
- File follow-up issues to track v2 features
