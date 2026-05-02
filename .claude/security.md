# Security & Safety Blueprint

Purpose: Document the threat model, safety guarantees, and recovery paths for the Athom Smart Plug V3 ESPresense build. The "security" surface here is unusual for embedded firmware — it covers electrical safety (overcurrent), recovery from misconfiguration (factory reset), and the OTA flash path.

---

## Threat model summary

| Concern | Severity | Mitigation in this PR |
|---------|----------|----------------------|
| Sustained overcurrent (load draws > 16A) | **High** — fire / regulatory | Latching trip in `CSE7766` at 16.0A default; cuts relay; persists to SPIFFS |
| Stuck-on overcurrent → power-cycle loop | High | `/relay_tripped` flag + boot-time override forces relay OFF before re-arming |
| Boot-glitch on relay GPIO before driver init | Medium | `Relay::EarlyInit()` drives pin LOW as the **first** action of `setup()` |
| User locked out of device (wrong WiFi/MQTT) | Medium | 4-second button hold = `SPIFFS.format()` + reboot → captive portal |
| Untrusted OTA firmware push | Medium | Existing ESPresense OTA mechanisms apply — not changed by this PR |
| Compromise of MQTT broker | Medium | Existing — relay is controllable from MQTT, same trust boundary as the rest of ESPresense |
| Accidental factory reset on secondary input | Low | Long-press only on Button 1; Button 2 long-press is log-only |

---

## Electrical safety: overcurrent trip

### Default

`DEFAULT_CURRENT_LIMIT_AMPS = 16.0f` on `ATHOM_PLUG_V3` builds. Matches the regulatory cap for US/EU/UK/IL/BR Athom plug models.

### Trip arming

To prevent false trips on inrush:
1. **5-second boot grace** — no evaluation for the first 5 seconds after `Setup()`.
2. **4 consecutive valid-current packets** required after grace before arming.
3. **2 consecutive over-limit packets** required to actually trip (~100ms total at 50ms cadence) — debounces boundary noise.

### Trip execution

- `tripped = true` (latching — blocks further evaluation)
- Write `/relay_tripped` to SPIFFS
- Call `Relay::set(false)`
- Publish `<roomsTopic>/relay_trip` retained `ON`
- Rate-limit log to 1 Hz (prevents spam during repeated re-engages)

### Trip clearing

- User manual re-engage (`Relay::set(true)` from button tap or MQTT) calls `CSE7766::clearTrip()`
- Clears `tripped`, removes `/relay_tripped`, publishes `<roomsTopic>/relay_trip` `OFF`
- Re-arms after another 4 valid-current packets

### Power-cycle-loop guard

If `/relay_tripped` exists at boot AND restore mode is `Always On`, `Relay::Setup()` forces OFF instead of restoring. Without this, a stuck-on overcurrent would: trip → reboot → restore-on → trip → reboot ... until flash dies.

### User configurability

Current limit is exposed to HA as a `number` entity (`<roomsTopic>/cse7766_current_limit/set`, range 0.0–16.0, step 0.5). Setting to `0` disables overcurrent trip. Persisted to `/cse7766_current_limit` SPIFFS file.

---

## Factory reset (recovery path)

**Trigger:** Button 1 held for ≥ 4 seconds.

**Action:** `SPIFFS.format()` + `ESP.restart()`.

**Wipes:**
- WiFi creds
- MQTT broker config
- Room topic
- Relay state (`/relay_state`, `/relay_tripped`)
- Energy total (`/cse7766_total_wh`)
- Current limit (`/cse7766_current_limit`)
- All other portal-managed settings

**Recovery:** Device reboots into captive portal — user re-enters WiFi/MQTT.

**Why physical-only:** MQTT-triggered factory reset is a deferred item — physical access requirement is a meaningful security boundary for the device-locked-out scenario.

**Constraint:** Fires once even if held longer (no double-format). Button 2 long-press is log-only because Button 2 is "secondary input" by ESPresense convention — wiring it to factory reset would surprise generic-board users.

---

## OTA flash path

### Initial flash (factory ESPHome → ESPresense)

OTA via the device's built-in ESPHome web server: `http://<plug-ip>/update` upload form. Universal across all Athom plug units (some have USB-C ports, many don't).

### Subsequent flashes

ESPresense's built-in OTA mechanisms (existing, not changed by this PR):
- Web UI upload
- MQTT URL push
- Auto-update from GitHub Releases

Same threat model as upstream ESPresense — this PR does not modify the OTA layer.

### Build artifact identity

`FIRMWARE` define = `"athom-smart-plug-v3"` — drives the OTA artifact name. Different from existing builds, so auto-update can't accidentally cross-flash.

---

## Secrets handling

### In repo

**No secrets in source.** Everything user-specific (WiFi PSK, MQTT credentials, room topic) is entered at the captive portal and stored in SPIFFS.

### On device

SPIFFS-stored config is **not encrypted**. Anyone with physical access to the device can dump SPIFFS and read credentials. This is the existing ESPresense threat model — not changed.

### CI

Build secrets (signing keys, etc.) are not part of this PR. Standard GitHub Actions secret handling.

---

## Discovered & deferred

### Pre-existing bugs flagged (out of scope, file separately — see PR description)

- `sendSwitchDiscovery` (`mqtt.cpp:166`) writes `entity_category` even when empty — unlike all other helpers which guard with `isEmpty()`.
- `*_Timeout` number entities (Switch/Button/Motion) have hardcoded `step: "0.1"`, wrong for second-granularity timeouts. The `sendNumberDiscovery` extension in this PR enables better defaults but doesn't update existing call sites.
- Duplicate `Motion::Command` line at `main.cpp:386`.

### Deferred (post-v1)

- MQTT-triggered factory reset
- `std::atomic<bool>` on `tripped` flag — only relevant if CSE7766 parsing moves to its own FreeRTOS task. Code comment noted for future maintainers.
- CSE7766 calibration multiplier overrides (for non-Athom shunt resistors)

---

## Verification before merging

- [ ] Overcurrent trip fires when test load > limit and clears on manual relay re-engage (Phase 3 hardware test)
- [ ] Power-cycle-loop guard: simulate stuck `/relay_tripped` + Always On restore mode → relay comes up OFF
- [ ] 4-second button hold formats SPIFFS and reboots into portal (Phase 3)
- [ ] Boot-glitch test: relay does not energize during the boot window before `Setup()` runs (Phase 3, scoped to "no observable click")
- [ ] No hardcoded credentials anywhere in the diff (`grep -rE 'password|token|secret' src/ include/`)
