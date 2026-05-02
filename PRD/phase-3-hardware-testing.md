# Phase 3 — Hardware Testing

**Goal**: Verify every feature of the new firmware works end-to-end against a real Athom Smart Plug V3. The user handles the actual flashing + initial captive-portal setup before this phase begins; the agent picks up at a known-IP, configured device and exercises every feature.

**Estimated effort**: 2–4 hours active testing + optional 24-hour soak

**Prerequisites** (user handles flashing + initial setup before Phase 3 begins):
- Phase 2 complete (`PRD/phase-2-handoff.md` exists)
- User has flashed the ESPresense firmware to the Athom plug via the browser (using the factory ESPHome firmware's `/update` web upload — see Phase 4 / hardware notes for the curl alternative if needed)
- User has connected the now-running ESPresense to their WiFi via the `ESPresense-XXXXXX` captive portal AP and configured MQTT broker + room name
- **User provides the device's IP address** at the start of this phase — record it in `PRD/phase-3-handoff.md` as the first action
- Access to a Home Assistant instance (or a stand-alone MQTT broker like mosquitto for raw MQTT testing)
- A small load to plug in (desk lamp, fan, anything safe to switch on/off)
- Optionally: a high-current load (hairdryer, kettle) for the trip test — only do this if comfortable safety-wise

## Status check (run before starting)

```bash
cd /opt/git/personal/ESPresense
git branch --show-current             # should be feat/athom-smart-plug-v3
git status                            # should be clean
ls PRD/phase-2-handoff.md             # confirm Phase 2 was completed
cat PRD/phase-2-handoff.md            # read for any context
```

Capture all the inputs from the user. Without these the agent cannot verify HA entities or MQTT topology:

```bash
PLUG_IP="192.168.x.x"     # <-- ESPresense device IP (post-config) provided by user
ROOM_NAME="kitchen"        # <-- room name as configured in captive portal (case-sensitive)
MQTT_HOST="192.168.x.x"    # <-- MQTT broker host the user pointed the device at
MQTT_USER=""               # <-- broker username if any (often empty)
MQTT_PASS=""               # <-- broker password if any
```

Smoke-test reachability:

```bash
curl -sf "http://${PLUG_IP}/" | head -20
```

You should see ESPresense's web UI HTML (look for tokens like `ESPresense`, `roomsTopic`, or `<title>`). If the device doesn't respond, check with the user:
- Is the plug powered?
- Did the WiFi/MQTT setup actually succeed? (the device may still be in captive portal mode)
- Is the IP correct? Re-confirm via `arp -a` or `ping <hostname>.local`

If any of `ROOM_NAME`, `MQTT_HOST`, etc. are unknown, ask the user before proceeding.

## Step 1 — Confirm pre-test state and inspect new settings (5 min)

User has already flashed and configured. Quick sanity checks:

### Confirm the new hardware sections are populated

Browse to `http://${PLUG_IP}/wifi/hardware` (or use curl + jq):

```bash
curl -sf "http://${PLUG_IP}/wifi/hardware" | python3 -m json.tool | grep -E "relay|cse7766" | head -20
```

Expected output should include keys like:
```
"relay_pin": ...
"relay_restore_mode": ...
"cse7766_rx_pin": ...
"cse7766_current_limit": ...
"cse7766_update_interval": ...
```

And under `defaults`:
```
"relay_pin": 5
"cse7766_rx_pin": 20
"cse7766_current_limit": 16.0
"cse7766_update_interval": 10
```

If those keys aren't present:
- The user may have flashed an older build — re-confirm `firmware.bin` was the one from `feat/athom-smart-plug-v3`
- Or the firmware version macros may not be wired correctly — check the boot log if accessible

### Confirm MQTT is connected

```bash
mosquitto_sub -h ${MQTT_HOST} \
    ${MQTT_USER:+-u $MQTT_USER} \
    ${MQTT_PASS:+-P $MQTT_PASS} \
    -t "espresense/rooms/${ROOM_NAME}/#" -v -C 5
```

Within ~5-10 seconds you should see at least:
- `espresense/rooms/${ROOM_NAME}/status online`
- `espresense/rooms/${ROOM_NAME}/relay OFF` (or `ON`)
- `espresense/rooms/${ROOM_NAME}/relay_trip OFF`
- `espresense/rooms/${ROOM_NAME}/telemetry {…JSON…}`

If no messages arrive in 30 seconds:
- Confirm `${ROOM_NAME}` matches what the user configured (case-sensitive)
- Confirm device shows `online` status (firmware might not have connected to MQTT yet)
- Verify MQTT credentials match what user entered in the captive portal

## Step 2 — HA entity discovery verification (10 min)

In Home Assistant, navigate to MQTT integration → look for the device named `ESPresense (athom-smart-plug-v3)` or similar.

You should see these entities:

| Entity | Type | Source |
|--------|------|--------|
| Relay | Switch | `sendSwitchDiscovery("relay", ...)` |
| Voltage | Sensor (V, voltage) | `sendTeleSensorDiscovery("voltage", ...)` |
| Current | Sensor (A, current) | … |
| Power | Sensor (W, power) | … |
| Apparent power | Sensor (VA, apparent_power) | … |
| Reactive power | Sensor (var, reactive_power) | … |
| Power factor | Sensor (%, power_factor) | … |
| Total energy | Sensor (kWh, energy, total_increasing) | … |
| CSE7766 Current Limit | Number (A, 0-16, step 0.5) | `sendNumberDiscovery(...)` |
| Relay Trip | Binary sensor (problem) | `sendBinarySensorDiscovery(...)` |

Plus existing ESPresense entities (BLE counts, RSSI, etc.).

**If any entity is missing**: check serial console for the discovery publish messages. If discovery sends but HA doesn't show it, check HA's MQTT integration logs for parse errors (often a typo in `value_template` or a missing `device_class`).

## Step 3 — Relay control test (15 min)

Plug a small load (desk lamp, anything safe) into the Athom plug.

### MQTT control

In HA: toggle the Relay switch entity ON.
- Listen for the audible click of the relay
- Lamp should turn on
- Serial console should log no errors
- HA switch should remain ON (state confirmed)

Toggle OFF:
- Click, lamp off, HA shows OFF.

### Web UI control

Browse to `http://<plug-ip>/`. Find the relay control on the web UI.
- Toggle on/off, confirm same behavior

### Physical button test (short tap)

Tap the power button on the plug (release within 1 second).
- Relay should toggle (clicked on → click off, or vice versa)
- HA should reflect the new state within 100ms
- Serial console should log `Button 1 short-pressed`

### Restore mode test

In HA's portal config (or via MQTT), set Relay to ON. Then unplug and re-plug the device.
- With `restore_mode = Always On` (Athom default): relay clicks on shortly after boot
- Verify the LED indicates restore — boot sequence completes, then GPIO5 drives HIGH

Change restore mode to "Always Off" via the captive portal (re-enter the portal), reboot:
- Relay should NOT activate

Change to "Restore Last":
- Set relay ON via MQTT
- Reboot
- Relay should come back ON
- Set relay OFF
- Reboot
- Relay should come back OFF

## Step 4 — Power monitoring sensor sanity (10 min)

With a load plugged in (e.g., a 60W lamp):

Wait 10 seconds for the first telemetry publish, then check HA:

- **Voltage**: should be ~115-125V (US) or ~220-240V (EU). Reasonable.
- **Current**: should be ~0.5A for a 60W lamp at 120V (60/120 = 0.5)
- **Power**: should be ~60W (matches the lamp's rating)
- **Apparent power**: should be very close to active power for a resistive load (lamp PF ~ 1)
- **Reactive power**: should be small (< 5VAR)
- **Power factor**: should be > 90%
- **Total energy**: should slowly increase

If voltage shows 0:
- The chip is reporting fault codes (no AC measurement) — check that the plug is actually plugged into mains
- Or the GPIO20 wiring isn't connecting (re-flash a known-good build to verify)

If current/power show 0 with a load running:
- The CSE7766 may need a few seconds to stabilize on a small load
- For very small loads (< 3W), the chip's filter zeros them out (this matches Athom's ESPHome behavior)

If values are wildly off (e.g., voltage = 60V with a real 120V mains):
- Likely a calibration issue — the factory cal should be correct, this would indicate parsing error
- Add debug logging temporarily to print raw `voltage_cal` and `voltage_cycle` values
- Compare against known-good ESPHome readings if available

## Step 5 — Energy accumulation test (run for 30 min)

With a known-power load (e.g., 60W lamp) running:

- Note the initial `total_energy` value in HA
- Wait 30 minutes
- Note the final value
- Expected accumulation: 60W × 0.5h = 30Wh = 0.030 kWh

Check that the value increased monotonically. Refresh HA history graph — should show a smooth ramp, no resets.

### Reboot persistence

While accumulation is in progress, unplug the device for 5 seconds and re-plug.

- Watch serial console for `CSE7766: restored total_energy = X.XXX kWh`
- The restored value should be within ~10 Wh of what HA last showed (matches our 5-min throttle + 10 Wh delta-gate)
- After reboot, accumulation continues from there

## Step 6 — Current-limit trip test (20 min)

⚠️ **Safety**: only attempt with a load you're confident is safe to cycle. A hairdryer or electric kettle works. Do NOT use anything that could be damaged by abrupt power cycles (computers, motors with delicate startup, etc.).

### Configure a low limit

Via HA's CSE7766 Current Limit number entity, set the limit to a value LOWER than your test load's current. For a 1500W (12.5A @ 120V) hairdryer, set the limit to 5A.

### Trigger the trip

Plug in the high-current load. Turn it on (some loads have their own switch).

Within ~100ms after the load starts drawing > 5A:
- Relay should click off audibly
- Load should lose power
- HA should show:
  - `Relay` switch entity = OFF
  - `Relay Trip` binary sensor = problem (red indicator)
- Serial console should log: `CSE7766 overcurrent trip: X.XXa > 5.00A limit`

### Try to re-engage

In HA, toggle the Relay switch back to ON.

If the load is still drawing > 5A:
- Within ~100ms, relay clicks back off
- Trip indicator stays ON

This is the expected latching behavior — relay won't stay on while the overcurrent persists.

### Restore safe operation

Raise the current limit back to 16A (or unplug the load). Toggle Relay ON.

- Relay stays on
- Trip binary sensor goes OFF (no longer "problem")

### Power-cycle resilience

While the relay is tripped (limit = 5A, hairdryer plugged in but off, scenario set up so a future ON would trip):
- Unplug the device for 5 seconds
- Re-plug
- Expected: relay stays OFF after boot (because `/relay_tripped` was persisted)
- Even with `restore_mode = Always On`, the trip-persistence override kicks in
- Serial log should mention "Skipping relay restore-on — last shutdown was tripped"

To recover: manually toggle Relay ON via HA. If load is no longer drawing too much, relay stays on.

## Step 7 — Factory reset (10 min)

Hold the power button continuously for ~5 seconds.

- After 4 seconds (no serial visible without case access — observe via behavior)
- SPIFFS formatting begins
- Device reboots
- On boot, no WiFi credentials → captive portal AP returns
- Status LED returns to red (portal mode)

Verify by trying to reach `http://${PLUG_IP}/` — connection should fail (device dropped off WiFi). The user can then re-enter the captive portal to verify the Relay + Power Monitor sections still appear, then reconfigure WiFi + MQTT to re-join the network.

After re-join, capture the new IP (DHCP may have assigned a different one) and update `PLUG_IP` in your shell session.

## Step 8 — BLE detection regression test (15 min)

This is the original ESPresense feature — verify it still works after all our additions.

With at least one known BLE device near the plug (your phone with Bluetooth enabled, an iBeacon, etc.):

- In HA, look for `<room>` entities under MQTT
- Listen for `espresense/devices/<mac>` topics in MQTT broker logs:
  ```bash
  mosquitto_sub -h <mqtt-broker> -t 'espresense/devices/#' -v | head -30
  ```
- Distance/RSSI values should be reasonable for proximity (< 5 m for adjacent devices)
- Walk away from the plug — RSSI should weaken
- Walk closer — RSSI should strengthen

Verify for at least 10 minutes. Without serial access, monitor for stability via MQTT — if BLE detection stops publishing for >2 minutes, something has gone wrong.

## Step 9 — 24-hour soak test (overnight, optional but recommended)

Leave the device running with realistic loads (relay ON with a small load drawing some power):

After ~24 hours, check via MQTT (no serial available):

```bash
# Confirm device has stayed online the whole time
mosquitto_sub -h <mqtt-broker> -t 'espresense/rooms/+/status' -C 1
# Expect: "online"

# Check uptime (how long since last reboot)
mosquitto_sub -h <mqtt-broker> -t 'espresense/rooms/+/telemetry' -C 1 | python3 -m json.tool
# Look for an "uptime" field; should be close to 24h * 3600 = 86400 seconds

# Verify total_energy has accumulated
mosquitto_sub -h <mqtt-broker> -t 'espresense/rooms/+/telemetry' -C 1 | python3 -c "import sys, json; print(json.loads(sys.stdin.read()).get('total_energy', 'missing'))"
# Should reflect ~24 hours of consumption

# Verify BLE is still active
mosquitto_sub -h <mqtt-broker> -t 'espresense/devices/#' -C 5
# Should see recent device fingerprints
```

Verification:
- WiFi: still connected (status = `online`)
- MQTT: still connected
- Memory pressure: ESPresense publishes free heap as part of telemetry — should not be < 30 KB
- Energy: total_energy reflects ~24 hours of accumulation
- BLE: still detecting devices
- No unexpected reboots (uptime should be close to test duration)

## Phase 3 verification gate

All of these must be ✅ to proceed to Phase 4:

- [ ] Device is reachable at the user-provided IP, `/wifi/hardware` shows our new keys with correct defaults (Step 1)
- [ ] User confirmed captive portal showed Relay + Power Monitor sections during setup (user-reported, recorded in handoff)
- [ ] MQTT topics appear under `espresense/rooms/<room>/...` (Step 1)
- [ ] All 10 HA entities discovered (1 switch + 7 sensors + 1 number + 1 binary_sensor)
- [ ] Relay toggles via MQTT, web UI, and physical button
- [ ] Restore mode works for all 3 settings (Off / On / Restore Last)
- [ ] Voltage / current / power readings are sensible for a known load
- [ ] Total energy accumulates correctly over 30 min
- [ ] Reboot restores total_energy from SPIFFS within 10 Wh
- [ ] Current-limit trip fires within ~100ms of overcurrent
- [ ] Trip persists across reboot (relay stays OFF on next boot if Always On)
- [ ] Manual ON clears trip and re-arms detection
- [ ] 4-second button hold triggers factory reset → captive portal returns
- [ ] BLE detection works alongside power monitoring (no regression)
- [ ] (Optional) 24-hour soak: no crashes, no memory leak

## Handoff to Phase 4

If Phase 3 passes:

1. Update `PRD/README.md` status:
   ```
   - ✅ Phase 1 — Implementation
   - ✅ Phase 2 — Build verification
   - ✅ Phase 3 — Hardware testing
   - ⬜ Phase 4 — PR submission
   ```

2. Create `PRD/phase-3-handoff.md` with:
   - The DEVICE_IP, ROOM_NAME, MQTT broker the user provided
   - Test results for each verification gate
   - Any sensor calibration deltas observed (e.g., voltage reading vs measured mains)
   - Any unexpected behaviors that didn't fail the test but are worth noting in the PR description
   - User-supplied screenshots (where available):
     - Captive portal showing new sections (user took during their setup)
     - HA dashboard showing the entities
     - HA history graph of `total_energy` ramp
     - Trip event in HA's logbook

3. Phase 4 reads the handoff file and uses the test evidence to draft the PR description.

If Phase 3 had failures:
- Fix the underlying code issue (back to Phase 1 if needed)
- Re-run the failed test
- Document any limitations that can't be fixed (e.g., "calibration is ±5% on this unit due to factory variation")

## Common issues and quick fixes

| Symptom | Likely cause | Fix |
|---------|-------------|-----|
| Device not reachable at provided IP | IP changed (DHCP lease) or user gave wrong IP | Ask user to verify IP via router DHCP / mDNS again |
| `/wifi/hardware` JSON missing relay/cse7766 keys | Firmware was an older build without HAS_RELAY/HAS_POWER_MONITOR | User re-flashes from latest `.pio/build/athom-smart-plug-v3/firmware.bin` |
| MQTT topics not appearing | Device couldn't reach broker; or wrong room name | Verify with user: room name (case-sensitive), broker reachability |
| Captive portal doesn't show new sections (user reported during setup) | Not running latest build | Have user re-flash from `.pio/build/athom-smart-plug-v3/firmware.bin`; confirm `npm run build` was run in Phase 1 Step 14 |
| HA doesn't discover new entities | MQTT discovery prefix mismatch | Check `homeassistant_discovery_prefix` setting in portal — should be `homeassistant` |
| Relay clicks but load doesn't power | External wiring issue (not firmware) | Check the relay output terminal of the plug |
| Power readings are 0 | Chip needs a few seconds | Wait 10s, retry |
| Voltage shows 240V on a 120V outlet | Calibration issue (very rare on Athom factory cal) | Compare with known-good ESPHome reading |
| Trip fires when it shouldn't | Boundary noise + need more debounce | Already 2-packet debounce; if still flaky, increase to 3 in CSE7766.cpp |
| Reboot loses energy | SPIFFS write didn't fire (device crashed before 5-min throttle) | Acceptable; document in PR |
| BLE detection regressed | NimBLE memory pressure from added modules | Check free heap; should be > 30KB; if not, reduce DEFAULT_MAX_FINGERPRINTS |
