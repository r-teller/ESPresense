# Phase 1 — Implementation

**Goal**: Write all firmware + UI code, commit to a working branch on the local repository. End state is a fully-coded branch ready for Phase 2 (build verification).

**Estimated effort**: 12–16 hours

**Prerequisites**:
- Clean checkout of `/opt/git/personal/ESPresense` on `main`
- Design canon (`PRD/00-context-and-design.md`) read and understood
- `pio` and `npm` available locally (test with `pio --version` and `npm --version`)

## Status check (run before starting)

```bash
cd /opt/git/personal/ESPresense
git status                   # should show clean working tree
git branch --show-current    # should be on main
git log -1 --format="%H %s"  # capture commit hash; record this in handoff
ls PRD/                      # confirm PRD docs exist
```

If anything is dirty or unexpected, do not proceed. Commit/stash any work first.

## Step 0 — Repo setup (5 min)

The `feature/athom-plug-v3` branch already exists on the user's fork (r-teller) with the PRD docs as the first commit. Phase 1 layers code commits on top.

```bash
cd /opt/git/personal/ESPresense

# Ensure r-teller fork is configured as a remote
git remote add r-teller https://github.com/r-teller/ESPresense.git 2>/dev/null || true
git fetch r-teller

# Check out the existing branch (or create local tracking if missing)
git checkout feature/athom-plug-v3 2>/dev/null || git checkout -b feature/athom-plug-v3 r-teller/feature/athom-plug-v3
```

Verify:
```bash
git remote -v                       # should show origin (ESPresense/ESPresense) and r-teller (r-teller/ESPresense)
git branch --show-current           # should be feature/athom-plug-v3
git log --oneline -1                # should show the PRD-docs commit at the tip
ls PRD/                             # PRD directory should be in working tree
```

If `git log` shows only the upstream main commit (no PRD), the branch wasn't fetched correctly — re-run `git fetch r-teller feature/athom-plug-v3` and try again.

## Step 1 — I²C-on-CDC fix in `defaults.h` (10 min, separate commit)

This is a generic bug fix that ships separately from the Athom feature work.

### Edit `include/defaults.h`

Find the `#elif defined ESP32C3` arm at lines 112-117. Replace this block:

```c
#elif defined ESP32C3
#define DEFAULT_I2C_BUS_1_SDA 19
#define DEFAULT_I2C_BUS_1_SCL 18
#define DEFAULT_I2C_BUS_2_SDA -1
#define DEFAULT_I2C_BUS_2_SCL -1
#define DEFAULT_I2C_BUS 1
```

With:

```c
#elif defined ESP32C3
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT == 1
// GPIO18/19 are USB D-/D+ on CDC builds — defaulting to I²C would brick USB.
#define DEFAULT_I2C_BUS_1_SDA -1
#define DEFAULT_I2C_BUS_1_SCL -1
#else
#define DEFAULT_I2C_BUS_1_SDA 19
#define DEFAULT_I2C_BUS_1_SCL 18
#endif
#define DEFAULT_I2C_BUS_2_SDA -1
#define DEFAULT_I2C_BUS_2_SCL -1
#define DEFAULT_I2C_BUS 1
```

### Verify

```bash
pio run -e esp32c3-cdc      # should still build clean
pio run -e esp32c3          # should still build clean
```

### Commit

```bash
git add include/defaults.h
git commit -m "fix(esp32c3): disable I2C defaults that conflict with USB-CDC

GPIO18 and GPIO19 are the C3's native USB D-/D+ pins. When
ARDUINO_USB_CDC_ON_BOOT=1 (used by the esp32c3-cdc env), the USB
peripheral claims those pads. The previous default would silently
brick USB-CDC the moment a user enabled an I²C sensor.

Gate the GPIO18/19 defaults behind the inverse of CDC-on-boot so
non-CDC builds keep their original behavior on the bare DevKitM-1
while CDC builds default to disabled and let users pick free pins."
```

## Step 2 — Bump JSON buffer (5 min, fold into next commit)

### Edit `src/globals.h`

Find the `DynamicJsonDocument doc(768)` line (around line 34) and change `768` to `1024`:

```c
DynamicJsonDocument doc(1024);
```

(Don't commit yet — we'll fold this into the discovery-helper commit in Step 3.)

## Step 3 — Discovery helper extensions (30 min)

### Edit `src/mqtt.h`

Add `<math.h>` include near the top:

```c
#pragma once
#include <Arduino.h>
#include <math.h>      // for NAN sentinel
```

Replace these declarations (around lines 19-24):

```c
bool sendBinarySensorDiscovery(const String &name, const String &entityCategory, const String &devClass = DEVICE_CLASS_NONE);
bool sendSensorDiscovery(const String &name, const String &entityCategory, const String &devClass = DEVICE_CLASS_NONE, const String &units = "", bool frcUpdate = false);

bool sendButtonDiscovery(const String &name, const String &entityCategory);
bool sendSwitchDiscovery(const String &name, const String &entityCategory);
bool sendNumberDiscovery(const String &name, const String &entityCategory);
bool sendLightDiscovery(const String &name, const String &entityCategory, bool rgb, bool rgbw);
```

With:

```c
bool sendBinarySensorDiscovery(const String &name, const String &entityCategory, const String &devClass = DEVICE_CLASS_NONE);
bool sendSensorDiscovery(const String &name, const String &entityCategory, const String &devClass = DEVICE_CLASS_NONE, const String &units = "", bool frcUpdate = false, const String &stateClass = "");

bool sendButtonDiscovery(const String &name, const String &entityCategory);
bool sendSwitchDiscovery(const String &name, const String &entityCategory);
bool sendNumberDiscovery(const String &name, const String &entityCategory, float min = NAN, float max = NAN, float step = 0.1f, const String &units = "", const String &mode = "");
bool sendLightDiscovery(const String &name, const String &entityCategory, bool rgb, bool rgbw);
```

Also extend the `sendTeleSensorDiscovery` declaration (around line 17):

```c
bool sendTeleSensorDiscovery(const String &name, const String &entityCategory, const String &temp, const String &devClass = DEVICE_CLASS_NONE, const String &units = "", const String &stateClass = "");
```

### Edit `src/mqtt.cpp`

In `sendSensorDiscovery` body (around line 102-119), after the `if (!devClass.isEmpty()) doc["dev_cla"] = devClass;` line, add:

```c
if (!stateClass.isEmpty()) doc["stat_cla"] = stateClass;
```

The function signature must also be updated to match the header:

```c
bool sendSensorDiscovery(const String &name, const String &entityCategory, const String &devClass, const String &units, bool frcUpdate, const String &stateClass)
```

In `sendTeleSensorDiscovery` body (around line 80-99 — find it by searching for `"~/telemetry"`), add the same line after the `dev_cla` emission:

```c
if (!stateClass.isEmpty()) doc["stat_cla"] = stateClass;
```

Update its signature to match the header.

In `sendNumberDiscovery` body (around lines 172-188), replace the body with:

```c
bool sendNumberDiscovery(const String &name, const String &entityCategory, float min, float max, float step, const String &units, const String &mode)
{
    auto slug = slugify(name);

    commonDiscovery();
    doc["~"] = roomsTopic;
    doc["name"] = name;
    doc["uniq_id"] = Sprintf("espresense_%06x_%s", CHIPID, slug.c_str());
    doc["avty_t"] = "~/status";
    doc["stat_t"] = "~/" + slug;
    doc["cmd_t"] = "~/" + slug + "/set";
    if (!isnan(min)) doc["min"] = min;
    if (!isnan(max)) doc["max"] = max;
    doc["step"] = step;
    if (!units.isEmpty()) doc["unit_of_meas"] = units;
    if (!mode.isEmpty()) doc["mode"] = mode;
    if (!entityCategory.isEmpty()) doc["entity_category"] = entityCategory;

    const String discoveryTopic = Sprintf("%s/number/espresense_%06x/%s/config", homeAssistantDiscoveryPrefix.c_str(), CHIPID, slug.c_str());
    return pub(discoveryTopic.c_str(), 0, true, doc);
}
```

### Verify

```bash
pio run -e esp32                    # should still build (existing callers untouched)
pio run -e esp32c3-cdc              # should still build
```

### Commit (folds in globals.h bump)

```bash
git add src/mqtt.h src/mqtt.cpp src/globals.h
git commit -m "feat(mqtt): extend discovery helpers for HA Energy + bounded numbers

- sendSensorDiscovery + sendTeleSensorDiscovery: add defaulted stateClass
  arg to emit 'stat_cla' (measurement / total_increasing) for HA Energy
  dashboard support.
- sendNumberDiscovery: replace fixed step with min/max/step/units/mode args.
  Use NAN sentinels for min/max so existing callers preserve current
  behavior (HA's internal default bounds), while new callers can specify
  explicit bounds for entities that need them.
- Bump globals.h JsonDocument 768 -> 1024 to absorb the new state_class
  field in discovery payloads (energy entity was the closest to overflow)."
```

## Step 4 — Athom defaults block in `defaults.h` (15 min)

### Edit `include/defaults.h`

Find the LED chain ending around line 175 (`#else  // DevKit / generic`). Insert this `#elif` arm BEFORE that else:

```c
#elif defined ATHOM_PLUG_V3

#define DEFAULT_LED1_TYPE   1     // PWM Inverted — Athom blue LED is active LOW (GPIO6)
#define DEFAULT_LED1_PIN    6
#define DEFAULT_LED1_CNTRL  Control_Type_Status
#define DEFAULT_LED1_CNT    1

#define MAX_BRIGHTNESS      100

```

After the entire LED chain ends (after the closing `#endif` for the LED block), append three new defaults blocks:

```c

// Default Button 1 wiring per board (-1 = disabled, user configures via portal)
#if defined ATHOM_PLUG_V3
#define DEFAULT_BUTTON1_PIN   3
#define DEFAULT_BUTTON1_TYPE  0    // Pullup — Athom power button is active LOW
#else
#define DEFAULT_BUTTON1_PIN   -1
#define DEFAULT_BUTTON1_TYPE  0
#endif

// Relay defaults (only when HAS_RELAY is enabled by the build env)
#ifdef HAS_RELAY
#if defined ATHOM_PLUG_V3
#define DEFAULT_RELAY_PIN          5
#define DEFAULT_RELAY_RESTORE_MODE 1   // Always On (matches ESPHome RESTORE_DEFAULT_ON)
#else
#define DEFAULT_RELAY_PIN          -1
#define DEFAULT_RELAY_RESTORE_MODE 0
#endif
#endif

// CSE7766 power monitor defaults (only when HAS_POWER_MONITOR is enabled)
#ifdef HAS_POWER_MONITOR
#if defined ATHOM_PLUG_V3
#define DEFAULT_CSE7766_RX_PIN        20
#define DEFAULT_CURRENT_LIMIT_AMPS    16.0f   // Regulatory cap for US/EU/UK/IL/BR Athom plugs
#define DEFAULT_POWER_UPDATE_INTERVAL 10      // seconds
#else
#define DEFAULT_CSE7766_RX_PIN        -1
#define DEFAULT_CURRENT_LIMIT_AMPS    0.0f
#define DEFAULT_POWER_UPDATE_INTERVAL 10
#endif
#endif
```

Also override I²C defaults for ATHOM_PLUG_V3. Insert at the START of the I²C bus defaults block (around line 97-134):

```c
// I2C Defaults
#ifdef ATHOM_PLUG_V3
#define DEFAULT_I2C_BUS_1_SDA -1
#define DEFAULT_I2C_BUS_1_SCL -1
#define DEFAULT_I2C_BUS_2_SDA -1
#define DEFAULT_I2C_BUS_2_SCL -1
#define DEFAULT_I2C_BUS 1
#elif defined M5STICK
... existing block continues ...
```

### Verify

```bash
pio run -e esp32                    # should still build (no Athom flag set)
```

(Don't commit yet — we'll fold this with Button.cpp consumption next.)

## Step 5 — Update `Button.cpp` to consume new defaults (5 min)

### Edit `src/Button.cpp`

Find lines 49-51:

```c
button_1Type = HeadlessWiFiSettings.dropdown("button_1_type", pinTypes, 0, "Button One pin type");
button_1Pin = HeadlessWiFiSettings.integer("button_1_pin", -1, "Button One pin (-1 for disable)");
```

Replace with:

```c
button_1Type = HeadlessWiFiSettings.dropdown("button_1_type", pinTypes, DEFAULT_BUTTON1_TYPE, "Button One pin type");
button_1Pin = HeadlessWiFiSettings.integer("button_1_pin", DEFAULT_BUTTON1_PIN, "Button One pin (-1 for disable)");
```

Lines 54-55 (button 2) stay unchanged.

### Verify

```bash
pio run -e esp32                    # should still build, button 2 still defaults to -1
```

### Commit

```bash
git add include/defaults.h src/Button.cpp
git commit -m "feat(athom): board defaults and Button1 pin override

- Add ATHOM_PLUG_V3 board macro with LED1 and I²C pin defaults.
- Add DEFAULT_BUTTON1_PIN and DEFAULT_BUTTON1_TYPE generic macros so
  boards can pre-populate the Button 1 portal field.
- Update Button.cpp to consume the new defaults (other boards still
  default to -1 so behavior is unchanged).
- Add DEFAULT_RELAY_* and DEFAULT_CSE7766_* macros gated by HAS_RELAY
  and HAS_POWER_MONITOR build flags."
```

## Step 6 — Create `Relay.cpp/.h` (1.5–2 hours)

### Create `src/Relay.h`

```c
#pragma once
#include <Arduino.h>

namespace Relay {

void EarlyInit();
void Setup();
void ConnectToWifi(bool updating);
void SerialReport();
void Loop();
bool SendDiscovery();
bool SendOnline();
bool Command(String& command, String& pay);

bool getState();
void set(bool on);
void toggle();

}  // namespace Relay
```

### Create `src/Relay.cpp`

```c
#ifdef HAS_RELAY

#include "Relay.h"

#include <AsyncMqttClient.h>
#include <HeadlessWiFiSettings.h>
#include <SPIFFS.h>

#include "defaults.h"
#include "globals.h"
#include "mqtt.h"
#include "string_utils.h"

#ifdef HAS_POWER_MONITOR
#include "CSE7766.h"
#endif

namespace Relay {

int8_t pin = DEFAULT_RELAY_PIN;
int8_t restoreMode = DEFAULT_RELAY_RESTORE_MODE;
bool state = false;
bool dirty = false;
bool online = false;
unsigned long lastSave = 0;

void EarlyInit() {
    if (DEFAULT_RELAY_PIN < 0) return;
    pinMode(DEFAULT_RELAY_PIN, OUTPUT);
    digitalWrite(DEFAULT_RELAY_PIN, LOW);
}

void Setup() {
    if (pin < 0) return;
    pinMode(pin, OUTPUT);

    bool wantOn = false;
    switch (restoreMode) {
        case 0: wantOn = false; break;
        case 1: wantOn = true; break;
        case 2: {
            String s;
            if (SPIFFS.exists("/relay_state")) {
                File f = SPIFFS.open("/relay_state", "r");
                if (f) { s = f.readString(); f.close(); }
            }
            wantOn = (s == "1");
            break;
        }
    }

    // Trip-persistence safety: if last shutdown was tripped and restore
    // mode would turn relay on, keep it off until user manually re-engages.
    // Prevents power-cycle loops with stuck-on overcurrent loads.
    if (wantOn && SPIFFS.exists("/relay_tripped")) {
        Log.println("Skipping relay restore-on — last shutdown was tripped");
        wantOn = false;
    }

    state = wantOn;
    digitalWrite(pin, wantOn ? HIGH : LOW);
}

void ConnectToWifi(bool updating) {
    std::vector<String> restoreModes = {"Always Off", "Always On", "Restore Last"};
    pin = HeadlessWiFiSettings.integer("relay_pin", -1, 48, DEFAULT_RELAY_PIN, "Relay output pin (-1 to disable)");
    restoreMode = HeadlessWiFiSettings.dropdown("relay_restore_mode", restoreModes, DEFAULT_RELAY_RESTORE_MODE, "Relay state on boot");
}

void SerialReport() {
    Log.print("Relay:        ");
    Log.println(pin >= 0 ? (state ? "enabled (ON)" : "enabled (OFF)") : "disabled");
}

bool getState() { return state; }

void set(bool on) {
    if (pin < 0 || state == on) return;
    state = on;
    digitalWrite(pin, on ? HIGH : LOW);
    dirty = true;
    pub((roomsTopic + "/relay").c_str(), 0, true, on ? "ON" : "OFF");
#ifdef HAS_POWER_MONITOR
    if (on) CSE7766::clearTrip();
#endif
}

void toggle() { set(!state); }

void Save() {
    if (!dirty) return;
    if (restoreMode != 2) { dirty = false; return; }
    spurt("/relay_state", state ? "1" : "0");
    dirty = false;
}

void Loop() {
    if (millis() - lastSave > 15000) {
        lastSave = millis();
        Save();
    }
}

bool SendDiscovery() {
    if (pin < 0) return true;
    return sendSwitchDiscovery("relay", EC_NONE);
}

bool SendOnline() {
    if (online || pin < 0) { online = true; return true; }
    if (!pub((roomsTopic + "/relay").c_str(), 0, true, state ? "ON" : "OFF")) return false;
    online = true;
    return true;
}

bool Command(String& command, String& pay) {
    if (command != "relay") return false;
    set(pay == "ON" || pay == "1" || pay == "true");
    return true;
}

}  // namespace Relay

#endif  // HAS_RELAY
```

### Verify

```bash
pio run -e esp32                    # should still build (HAS_RELAY not set)
```

The .cpp content is fully gated by `#ifdef HAS_RELAY`, so on builds without the flag, the file compiles to nothing. Headers can be safely included unconditionally — but we'll guard them in main.h to keep the include chain clean.

(Don't commit yet — wait until after CSE7766 is built since we need both for the Relay tests to compile cleanly when HAS_RELAY is set.)

## Step 7 — Create `CSE7766.cpp/.h` (5–6 hours, the largest module)

### Create `src/CSE7766.h`

```c
#pragma once
#include <Arduino.h>

namespace CSE7766 {

void Setup();
void ConnectToWifi(bool updating);
void SerialReport();
void Loop();
bool SendDiscovery();
bool SendOnline();
bool Command(String& command, String& pay);

float getCurrentPower();
uint32_t getCfPulses();
void clearTrip();

}  // namespace CSE7766
```

### Create `src/CSE7766.cpp`

```c
#ifdef HAS_POWER_MONITOR

#include "CSE7766.h"

#include <ArduinoJson.h>
#include <AsyncMqttClient.h>
#include <HeadlessWiFiSettings.h>
#include <SPIFFS.h>
#include <math.h>

#include "defaults.h"
#include "globals.h"
#include "mqtt.h"
#include "string_utils.h"

#ifdef HAS_RELAY
#include "Relay.h"
#endif

namespace CSE7766 {

// ============================ Configuration ============================

int8_t rxPin = DEFAULT_CSE7766_RX_PIN;
float currentLimitAmps = DEFAULT_CURRENT_LIMIT_AMPS;
int updateIntervalSec = DEFAULT_POWER_UPDATE_INTERVAL;

// ============================ Packet buffer ============================

uint8_t buffer[24];
uint8_t bufferPos = 0;

// ============================ Latest measurements ============================

// NaN until first valid packet — skip publishing until each is real
float voltage = NAN;
float current = NAN;
float power = NAN;

// ============================ Energy state ============================

double whPerPulse = 0.0;          // cached on first valid packet
double totalEnergyWh = 0.0;        // monotonically increasing total
double lastSavedWh = 0.0;          // last value persisted to SPIFFS
uint16_t lastCfPulses = 0;         // CF pulse counter from previous packet
bool energyInitialized = false;    // false until first packet establishes baseline

// ============================ Averaging (use double for precision) ============================

double voltageSum = 0.0, currentSum = 0.0, powerSum = 0.0;
unsigned int sampleCount = 0;

// ============================ Trip state machine ============================

bool tripped = false;
unsigned long setupMillis = 0;
unsigned int validCurrentPackets = 0;
unsigned int overLimitCount = 0;
unsigned long lastTripLogMillis = 0;

// ============================ Misc ============================

unsigned long lastEnergySave = 0;
unsigned long lastPublishMillis = 0;
bool online = false;

// ============================ Lifecycle ============================

void Setup() {
    if (rxPin < 0) return;

    Serial1.setRxBufferSize(256);                      // defensive against BLE-task stalls
    Serial1.begin(4800, SERIAL_8E1, rxPin, -1);         // RX-only, TX disabled
    while (Serial1.available()) Serial1.read();          // flush ROM-bootloader garbage on GPIO20
    setupMillis = millis();

    // Restore total_energy from SPIFFS
    if (SPIFFS.exists("/cse7766_total_wh")) {
        File f = SPIFFS.open("/cse7766_total_wh", "r");
        if (f) {
            totalEnergyWh = f.readString().toDouble();
            f.close();
            lastSavedWh = totalEnergyWh;
            Log.printf("CSE7766: restored total_energy = %.3f kWh\r\n", totalEnergyWh / 1000.0);
        }
    }

    // Restore tripped state from SPIFFS (set by previous instance on trip)
    if (SPIFFS.exists("/relay_tripped")) {
        tripped = true;
        Log.println("CSE7766: restored tripped state from previous shutdown");
    }
}

void ConnectToWifi(bool updating) {
    rxPin = HeadlessWiFiSettings.integer("cse7766_rx_pin", -1, 48, DEFAULT_CSE7766_RX_PIN, "CSE7766 UART RX pin (-1 to disable)");
    currentLimitAmps = HeadlessWiFiSettings.floating("cse7766_current_limit", 0, 16, DEFAULT_CURRENT_LIMIT_AMPS, "Auto-trip current limit (Amps, 0 = disabled, max 16)");
    updateIntervalSec = HeadlessWiFiSettings.integer("cse7766_update_interval", 1, 600, DEFAULT_POWER_UPDATE_INTERVAL, "Power sensor update interval (seconds)");
}

void SerialReport() {
    Log.print("CSE7766:      ");
    Log.println(rxPin >= 0 ? "enabled" : "disabled");
}

// ============================ Cross-module API ============================

float getCurrentPower() { return isnan(power) ? 0.0f : power; }
uint32_t getCfPulses() { return lastCfPulses; }

void clearTrip() {
    if (!tripped) return;
    tripped = false;
    SPIFFS.remove("/relay_tripped");
    pub((roomsTopic + "/relay_trip").c_str(), 0, true, "OFF");
    Log.println("CSE7766: trip cleared by manual relay re-engage");
}

// ============================ Packet validation ============================

// CSE7766 datasheet §5: byte[0] = header, byte[1] = 0x5A, byte[23] = checksum
bool validatePacket() {
    if (buffer[1] != 0x5A) return false;

    uint8_t hdr = buffer[0];
    // Accept: 0x55 (normal), 0xAA (not calibrated, parsed but skipped),
    //         0xF0..0xFF (per-channel fault codes — low 4 bits flag specific channels)
    if (hdr != 0x55 && hdr != 0xAA && (hdr & 0xF0) != 0xF0) return false;

    uint16_t sum = 0;
    for (int i = 2; i <= 22; i++) sum += buffer[i];
    return (sum & 0xFF) == buffer[23];
}

uint32_t read24BE(int offset) {
    return ((uint32_t)buffer[offset]     << 16) |
           ((uint32_t)buffer[offset + 1] <<  8) |
            (uint32_t)buffer[offset + 2];
}

// ============================ Trip handling ============================

void fireTrip(float i) {
    // Set state BEFORE calling Relay::set to defend against future reentrancy
    tripped = true;
    spurt("/relay_tripped", "1");

    if (millis() - lastTripLogMillis > 1000) {
        Log.printf("CSE7766 overcurrent trip: %.2fA > %.2fA limit\r\n", i, currentLimitAmps);
        lastTripLogMillis = millis();
    }

    pub((roomsTopic + "/relay_trip").c_str(), 0, true, "ON");

#ifdef HAS_RELAY
    Relay::set(false);
#endif
}

// ============================ Packet parsing ============================

void parsePacket() {
    if (!validatePacket()) {
        // Slide buffer left by 1 byte; on next read we'll attempt re-sync
        memmove(buffer, buffer + 1, 23);
        bufferPos = 23;
        return;
    }

    bufferPos = 0;  // packet consumed

    uint8_t hdr = buffer[0];

    // 0xAA = chip not calibrated. ESPHome logs and skips; we do the same.
    if (hdr == 0xAA) {
        static unsigned long lastNotCalLog = 0;
        if (millis() - lastNotCalLog > 5000) {
            Log.println("CSE7766: chip reports not calibrated");
            lastNotCalLog = millis();
        }
        return;
    }

    // CSE7766 datasheet §5, byte 20 ADJ register:
    //   bit 6 (0x40) = voltage measurement valid this cycle
    //   bit 5 (0x20) = current measurement valid this cycle
    //   bit 4 (0x10) = power measurement valid this cycle
    uint8_t adj = buffer[20];

    // When header is 0xF0..0xFF, the low 4 bits flag per-channel faults:
    //   bit 1 = power cycle out-of-range -> force P=0
    bool powerForcedZero = ((hdr & 0xF0) == 0xF0) && (hdr & 0x02);

    uint32_t voltage_cal   = read24BE(2);
    uint32_t voltage_cycle = read24BE(5);
    uint32_t current_cal   = read24BE(8);
    uint32_t current_cycle = read24BE(11);
    uint32_t power_cal     = read24BE(14);
    uint32_t power_cycle   = read24BE(17);
    uint16_t cf_pulses     = ((uint16_t)buffer[21] << 8) | buffer[22];

    // Honour validity bits — silent-hold previous value when channel invalid this cycle
    if ((adj & 0x40) && voltage_cycle > 0) {
        voltage = (float)voltage_cal / voltage_cycle;
    }

    if ((adj & 0x20) && current_cycle > 0) {
        current = (float)current_cal / current_cycle;
    }

    if (powerForcedZero) {
        power = 0.0f;
    } else if ((adj & 0x10) && power_cycle > 0) {
        power = (float)power_cal / power_cycle;
    }

    // ---- Energy accumulation ----

    if (!energyInitialized) {
        whPerPulse = (double)power_cal / 1e6 / 3600.0;
        lastCfPulses = cf_pulses;
        energyInitialized = true;
    } else {
        // uint16_t subtraction handles 16-bit wrap natively
        uint16_t diff = cf_pulses - lastCfPulses;
        lastCfPulses = cf_pulses;
        totalEnergyWh += (double)diff * whPerPulse;
    }

    // ---- Accumulate for averaging window (only when all values are real) ----

    if (!isnan(voltage) && !isnan(current) && !isnan(power)) {
        voltageSum += voltage;
        currentSum += current;
        powerSum += power;
        sampleCount++;
    }

    // ---- Trip evaluation ----

    if (tripped) return;
    if (millis() - setupMillis < 5000) return;          // 5s boot grace
    if (!(adj & 0x20)) return;                           // current not validated

    validCurrentPackets++;
    if (validCurrentPackets < 4) return;                 // require 4 clean readings before arming

    if (currentLimitAmps > 0 && current > currentLimitAmps) {
        overLimitCount++;
        if (overLimitCount >= 2) {                       // 2-packet debounce (~100ms)
            fireTrip(current);
            overLimitCount = 0;
        }
    } else {
        overLimitCount = 0;
    }
}

// ============================ Loop ============================

void Loop() {
    if (rxPin < 0) return;

    // Drain RX buffer; each 24-byte chunk is a packet candidate
    while (Serial1.available()) {
        if (bufferPos >= 24) bufferPos = 0;
        buffer[bufferPos++] = Serial1.read();
        if (bufferPos == 24) parsePacket();
    }

    unsigned long now = millis();

    // ---- Throttle-publish averaged measurements ----

    if (sampleCount > 0 && (now - lastPublishMillis) >= (unsigned long)updateIntervalSec * 1000UL) {
        lastPublishMillis = now;

        double v_avg = voltageSum / sampleCount;
        double i_avg = currentSum / sampleCount;
        double p_avg = powerSum / sampleCount;
        double s     = v_avg * i_avg;                              // VA
        double q     = sqrt(fmax(0.0, s * s - p_avg * p_avg));     // VAR
        double pf    = (s > 0.001) ? (p_avg / s) * 100.0 : 0.0;    // %  (pre-multiplied for HA)

        // JSON-coalesced telemetry — single publish, prevents 6× backpressure
        StaticJsonDocument<256> j;
        j["voltage"]        = roundf(v_avg * 10.0f) / 10.0f;
        j["current"]        = roundf(i_avg * 1000.0f) / 1000.0f;
        j["power"]          = roundf(p_avg * 10.0f) / 10.0f;
        j["apparent_power"] = roundf(s * 10.0f) / 10.0f;
        j["reactive_power"] = roundf(q * 10.0f) / 10.0f;
        j["power_factor"]   = roundf(pf * 10.0f) / 10.0f;
        j["total_energy"]   = roundf((float)totalEnergyWh) / 1000.0f;   // kWh

        String out;
        serializeJson(j, out);
        pub((roomsTopic + "/telemetry").c_str(), 0, true, out.c_str());

        voltageSum = currentSum = powerSum = 0.0;
        sampleCount = 0;
    }

    // ---- Throttle-persist totalEnergyWh (5-min throttle, 10 Wh delta-gate) ----

    if (now - lastEnergySave > 5UL * 60UL * 1000UL) {
        lastEnergySave = now;
        if (fabs(totalEnergyWh - lastSavedWh) >= 10.0) {
            spurt("/cse7766_total_wh", String(totalEnergyWh, 3));
            lastSavedWh = totalEnergyWh;
        }
    }
}

// ============================ Discovery ============================

bool SendDiscovery() {
    if (rxPin < 0) return true;
    return
        sendTeleSensorDiscovery("voltage",        EC_NONE, "{{ value_json.voltage }}",        "voltage",        "V",   "measurement") &&
        sendTeleSensorDiscovery("current",        EC_NONE, "{{ value_json.current }}",        "current",        "A",   "measurement") &&
        sendTeleSensorDiscovery("power",          EC_NONE, "{{ value_json.power }}",          "power",          "W",   "measurement") &&
        sendTeleSensorDiscovery("apparent_power", EC_NONE, "{{ value_json.apparent_power }}", "apparent_power", "VA",  "measurement") &&
        sendTeleSensorDiscovery("reactive_power", EC_NONE, "{{ value_json.reactive_power }}", "reactive_power", "var", "measurement") &&
        sendTeleSensorDiscovery("power_factor",   EC_NONE, "{{ value_json.power_factor }}",   "power_factor",   "%",   "measurement") &&
        sendTeleSensorDiscovery("total_energy",   EC_NONE, "{{ value_json.total_energy }}",   "energy",         "kWh", "total_increasing") &&
        sendNumberDiscovery("CSE7766 Current Limit", EC_CONFIG, 0.0f, 16.0f, 0.5f, "A", "box") &&
        sendBinarySensorDiscovery("Relay Trip", EC_DIAGNOSTIC, "problem");
}

bool SendOnline() {
    if (online || rxPin < 0) { online = true; return true; }
    if (!pub((roomsTopic + "/relay_trip").c_str(), 0, true, tripped ? "ON" : "OFF")) return false;
    if (!pub((roomsTopic + "/cse7766_current_limit").c_str(), 0, true, String(currentLimitAmps, 1).c_str())) return false;
    online = true;
    return true;
}

bool Command(String& command, String& pay) {
    if (command == "cse7766_current_limit") {
        currentLimitAmps = pay.toFloat();
        spurt("/cse7766_current_limit", pay);
        pub((roomsTopic + "/cse7766_current_limit").c_str(), 0, true, pay.c_str());
        return true;
    }
    return false;
}

}  // namespace CSE7766

#endif  // HAS_POWER_MONITOR
```

### Verify

```bash
pio run -e esp32                    # should still build (HAS_POWER_MONITOR not set)
```

### Commit

```bash
git add src/Relay.cpp src/Relay.h src/CSE7766.cpp src/CSE7766.h
git commit -m "feat: add Relay and CSE7766 modules

- Relay: GPIO output with restore mode (Always Off/On/Restore Last).
  Two-stage init via EarlyInit() + Setup() — the early init drives
  GPIO LOW first thing in setup() to avoid boot-glitch on undriven
  pins (per ESP32-C3 datasheet caveat). State persisted to SPIFFS
  for Restore Last mode. Trip-persistence override prevents
  power-cycle loops with stuck-on overcurrent loads.

- CSE7766: UART power monitor at 4800 8E1 RX-only. Validates
  every 24-byte packet against sync bytes + checksum. Honors per-
  channel validity bits in the ADJ byte (silent-hold pattern
  matches ESPHome). JSON-coalesced telemetry every 10s prevents
  6× MQTT backpressure on slow brokers. Energy accumulated via
  CF pulse counter with unsigned-subtraction wrap handling;
  persisted to SPIFFS every 5 min when delta >= 10 Wh.

- Trip behavior: 5s boot grace + 4 valid packets armed + 2-packet
  over-limit debounce. Latching trip — only manual Relay::set(true)
  clears it (via cross-module clearTrip() callback). Trip state
  persisted to SPIFFS to survive crashes mid-trip.

- HA discovery: 7 sensors (V/I/W/VA/VAR/PF/kWh) + 1 number entity
  (current limit) + 1 binary_sensor (relay trip with device_class
  problem). Power factor multiplied by 100 in firmware so HA
  displays as percentage with unit '%'.

- Both modules gated by HAS_RELAY / HAS_POWER_MONITOR build flags
  so non-Athom builds compile unchanged."
```

## Step 8 — Add `GUI` dispatcher methods (30 min)

### Edit `src/GUI.h`

Add to the `namespace GUI` block (existing function declarations are around lines 22-27):

```c
void ButtonPressed(int btn);
void ButtonLongPressed(int btn);
```

### Edit `src/GUI.cpp`

Add the necessary include near the top (with the other includes):

```c
#include <SPIFFS.h>

#ifdef HAS_RELAY
#include "Relay.h"
#endif
```

Append these two functions to the `namespace GUI` block (before the closing brace `}  // namespace GUI` at end of file):

```c
void ButtonPressed(int btn) {
    Log.printf("Button %d short-pressed\r\n", btn);
#ifdef HAS_RELAY
    if (btn == 1) Relay::toggle();
#endif
}

void ButtonLongPressed(int btn) {
    Log.printf("Button %d long-pressed (4s) — factory reset\r\n", btn);
    if (btn != 1) return;
    SPIFFS.format();
    delay(100);
    ESP.restart();
}
```

## Step 9 — Add edge-detection in `Button.cpp` (30 min)

### Edit `src/Button.cpp`

Add the GUI include near the top of the file (after the existing includes):

```c
#include "GUI.h"
```

Just below the existing namespace state variables (after `unsigned long lastbutton_2Milli = 0;`), add:

```c
// Edge-detection state for short/long-press dispatching
bool button_1WasPressed = false;
unsigned long button_1PressStart = 0;
bool button_1LongPressFired = false;

#define BUTTON_LONG_PRESS_MS       4000   // 4-second hold = factory reset
#define BUTTON_SHORT_PRESS_MAX_MS  1000   // <1s release = "tap"
```

In `button_1Loop()` (around line 80-90), add an edge-detection block at the START of the function (right after the early-return for `button_1Pin < 0`):

```c
static void button_1Loop() {
    if (button_1Pin < 0) return;
    bool detected = digitalRead(button_1Pin) == button_1Detected;
    unsigned long now = millis();

    // ---- Edge-detect block — dispatch tap/long-press to GUI ----
    if (detected && !button_1WasPressed) {
        button_1PressStart = now;
        button_1LongPressFired = false;
    } else if (!detected && button_1WasPressed) {
        unsigned long held = now - button_1PressStart;
        if (!button_1LongPressFired && held < BUTTON_SHORT_PRESS_MAX_MS) {
            GUI::ButtonPressed(1);
        }
    } else if (detected && !button_1LongPressFired
               && (now - button_1PressStart) >= BUTTON_LONG_PRESS_MS) {
        button_1LongPressFired = true;
        GUI::ButtonLongPressed(1);
    }
    button_1WasPressed = detected;
    // ---- End edge-detect ----

    // (existing publish-on-state-change logic continues unchanged)
    if (detected) lastbutton_1Milli = millis();
    unsigned long since = millis() - lastbutton_1Milli;
    int button_1Value = (detected || since < (button_1Timeout * 1000)) ? HIGH : LOW;

    if (lastbutton_1Value == button_1Value) return;
    pub((roomsTopic + "/button_1").c_str(), 0, true, button_1Value == HIGH ? "ON" : "OFF");
    lastbutton_1Value = button_1Value;
}
```

(The lines after `// ---- End edge-detect ----` are existing code — don't duplicate, this is the merged final version.)

### Verify

```bash
pio run -e esp32                    # should still build, no behavior change for esp32 (button_1_pin defaults to -1)
```

### Commit

```bash
git add src/GUI.h src/GUI.cpp src/Button.cpp
git commit -m "feat(button,gui): tap toggles relay, long-hold factory resets

- Add edge-detection state machine in Button::button_1Loop() that
  dispatches taps (release < 1s) and long-holds (>= 4s) via the
  GUI namespace. Existing per-state ON/OFF publishing on /button_1
  is preserved unchanged so user automations are not broken.

- Add GUI::ButtonPressed(int) and GUI::ButtonLongPressed(int)
  dispatchers. Mirrors the existing GUI::Motion() <-> LEDs::Motion()
  pattern — keeps Button.cpp loosely coupled (no direct cross-module
  calls).

- ButtonPressed(1) toggles the relay when HAS_RELAY is set; otherwise
  no-op.

- ButtonLongPressed(1) wipes SPIFFS and restarts. Provides a physical
  recovery path for users locked out by changed WiFi/MQTT config.
  Generic feature — not Athom-specific. Behavior matches ESPHome's
  factory_reset platform on the Athom yaml."
```

## Step 10 — Wire modules into `main.cpp` (30 min)

### Edit `src/main.h`

Around line 33-35 (after the AXP192 include), add:

```c
#ifdef HAS_RELAY
#include "Relay.h"
#endif
#ifdef HAS_POWER_MONITOR
#include "CSE7766.h"
#endif
```

### Edit `src/main.cpp`

**At the top of `void setup()`** (line 611, before `Serial.begin`), add:

```c
void setup() {
#ifdef HAS_RELAY
    Relay::EarlyInit();   // drive relay GPIO LOW immediately to avoid boot-glitch
#endif
#ifdef FAST_MONITOR
    Serial.begin(1500000);
    ...
```

**In `setupNetwork()` `ConnectToWifi` cascade** (after `Button::ConnectToWifi(updating);` at line 209), add:

```c
    Button::ConnectToWifi(updating);
#ifdef HAS_RELAY
    Relay::ConnectToWifi(updating);
#endif
#ifdef HAS_POWER_MONITOR
    CSE7766::ConnectToWifi(updating);
#endif
```

**In `setup()` module-init block** (after `Button::Setup();` at line 638), add:

```c
    Button::Setup();
#ifdef HAS_RELAY
    Relay::Setup();
#endif
#ifdef HAS_POWER_MONITOR
    CSE7766::Setup();
#endif
    Battery::Setup();
```

**In `SerialReport()` block** (after `Button::SerialReport();` at line 273), add:

```c
    Button::SerialReport();
#ifdef HAS_RELAY
    Relay::SerialReport();
#endif
#ifdef HAS_POWER_MONITOR
    CSE7766::SerialReport();
#endif
```

**In `SendDiscovery` chain** (after `Button::SendDiscovery()` at line 70), add:

```c
            && Button::SendDiscovery()
#ifdef HAS_RELAY
            && Relay::SendDiscovery()
#endif
#ifdef HAS_POWER_MONITOR
            && CSE7766::SendDiscovery()
#endif
            && Enrollment::SendDiscovery()
```

**In `SendOnline` chain** (after `Button::SendOnline()` at line 47), add:

```c
            && Button::SendOnline()
#ifdef HAS_RELAY
            && Relay::SendOnline()
#endif
#ifdef HAS_POWER_MONITOR
            && CSE7766::SendOnline()
#endif
            && GUI::SendOnline()
```

**In `Command` chain** (after `Button::Command(command, pay)` at line 390), add:

```c
        else if (Button::Command(command, pay))
            ...
#ifdef HAS_RELAY
        else if (Relay::Command(command, pay))
            ...
#endif
#ifdef HAS_POWER_MONITOR
        else if (CSE7766::Command(command, pay))
            ...
#endif
```

(Match the existing pattern — each `else if` branch already has its own action `...`.)

**In `loop()` body** (after `Button::Loop();` at line 686), add:

```c
    Button::Loop();
#ifdef HAS_RELAY
    Relay::Loop();
#endif
#ifdef HAS_POWER_MONITOR
    CSE7766::Loop();
#endif
    HttpWebServer::Loop();
```

### Verify

```bash
pio run -e esp32                    # no HAS_RELAY/HAS_POWER_MONITOR — should compile unchanged
```

(Don't commit yet — wait until platformio env added so we can verify the new env builds too.)

## Step 11 — Add `[env:athom-smart-plug-v3]` to `platformio.ini` (5 min)

### Edit `platformio.ini`

Append at the end of the file:

```ini

[env:athom-smart-plug-v3]
extends = esp32c3-cdc
lib_deps = ${esp32c3.lib_deps}
build_flags =
  -D CORE_DEBUG_LEVEL=1
  -D FIRMWARE='"athom-smart-plug-v3"'
  -D ATHOM_PLUG_V3
  -D HAS_POWER_MONITOR
  -D HAS_RELAY
  ${esp32c3-cdc.build_flags}
```

### Verify

```bash
pio run -e athom-smart-plug-v3      # should build clean!
pio run -e esp32                    # should still build (no regression)
pio run -e esp32c3-cdc              # should still build (no regression)
```

If the athom-smart-plug-v3 build fails, debug by reading the compiler errors carefully. Common issues:
- Missing `#include "Relay.h"` somewhere — add it
- Symbol mismatch between header and .cpp — verify signatures match
- `#ifdef` guards missing on a cross-module call — every reference to `Relay::*` outside Relay.cpp/.h must be guarded

### Commit

```bash
git add platformio.ini src/main.h src/main.cpp
git commit -m "feat(athom-plug-v3): add platformio env and main.cpp wiring

- New [env:athom-smart-plug-v3] extends esp32c3-cdc with:
  - HAS_RELAY / HAS_POWER_MONITOR / ATHOM_PLUG_V3 build flags
  - FIRMWARE='\"athom-smart-plug-v3\"' (matches CI artifact name)
  - Drops sensors lib deps (Athom has no I²C/OneWire sensors)
  - Explicit lib_deps for consistency with sibling envs

- Wire Relay and CSE7766 into the main.cpp lifecycle at 9 hooks:
  setup() early init, ConnectToWifi cascade, module Setup, SerialReport,
  SendDiscovery, SendOnline, Command, Loop, plus main.h includes.
  All call sites gated by #ifdef HAS_RELAY / HAS_POWER_MONITOR so other
  envs compile unchanged."
```

## Step 12 — Add to CI matrix (2 min)

### Edit `.github/workflows/build.yml`

At line 16, append `, athom-smart-plug-v3` at the end of the env array:

```yaml
        env: [esp32, esp32c3, esp32c3-cdc, esp32c6, esp32c6-cdc, esp32s3, esp32s3-cdc, esp32-verbose, esp32c3-verbose, esp32c6-verbose, esp32s3-verbose, m5stickc, m5stickc-plus, m5atom, macchina-a0, athom-smart-plug-v3]
```

### Commit

```bash
git add .github/workflows/build.yml
git commit -m "ci: add athom-smart-plug-v3 to build matrix

CI now produces a release artifact (athom-smart-plug-v3.bin) on tag
pushes and PR alpha test builds, alongside all existing envs. Matches
the FIRMWARE define so OTA updates resolve correctly."
```

## Step 13 — UI hardware page edits (30 min)

### Edit `ui/src/routes/hardware/+page.svelte`

Find the existing structure: LEDs section, then a single `<h2>` covering Motion/Switches/Buttons/DHT, then the I²C section starting around line 479.

**Insert two new `<h2>` sections between the Buttons section and the I²C section** (right before the line `<h2>` that starts the I²C section).

Find the closing of the Button Two section (around line 428-429, after the closing `</p>` of `button_2_timeout`) and the closing of the DHT section. Insert AFTER the entire pin-based devices section ends but BEFORE `<h2>` starting I²C.

```svelte
        <h2>
            <a href="https://espresense.com/configuration/settings#relay" target="_blank">Relay</a>
        </h2>
        <h4>Relay:</h4>
        <p>
            <label>
                Pin (-1 to disable):<br />
                <input
                    type="number"
                    step="1"
                    min="-1"
                    max="48"
                    name="relay_pin"
                    placeholder={$hardwareSettings.defaults['relay_pin']}
                    bind:value={$hardwareSettings.values['relay_pin']}/>
            </label>
        </p>
        <p>
            <label>
                State on Boot:<br />
                <select name="relay_restore_mode" bind:value={$hardwareSettings.values['relay_restore_mode']}>
                    <option disabled selected hidden>Always Off</option>
                    <option value="0">Always Off</option>
                    <option value="1">Always On</option>
                    <option value="2">Restore Last</option>
                </select>
            </label>
        </p>

        <h2>
            <a href="https://espresense.com/configuration/settings#power-monitor" target="_blank">Power Monitor</a>
        </h2>
        <h4>CSE7766 (Athom plugs and similar):</h4>
        <p>
            <label>
                UART RX Pin (-1 to disable):<br />
                <input
                    type="number"
                    step="1"
                    min="-1"
                    max="48"
                    name="cse7766_rx_pin"
                    placeholder={$hardwareSettings.defaults['cse7766_rx_pin']}
                    bind:value={$hardwareSettings.values['cse7766_rx_pin']}/>
            </label>
        </p>
        <p>
            <label>
                Auto-Trip Current Limit (Amps, 0 = disabled, max 16):<br />
                <input
                    type="number"
                    step="0.5"
                    min="0"
                    max="16"
                    name="cse7766_current_limit"
                    placeholder={$hardwareSettings.defaults['cse7766_current_limit']}
                    bind:value={$hardwareSettings.values['cse7766_current_limit']}/>
            </label>
        </p>
        <p>
            <label>
                Update Interval (seconds, 1-600):<br />
                <input
                    type="number"
                    step="1"
                    min="1"
                    max="600"
                    name="cse7766_update_interval"
                    placeholder={$hardwareSettings.defaults['cse7766_update_interval']}
                    bind:value={$hardwareSettings.values['cse7766_update_interval']}/>
            </label>
        </p>
```

### Commit Svelte change (separately from generated headers)

```bash
git add ui/src/routes/hardware/+page.svelte
git commit -m "ui(hardware): add Relay and Power Monitor configuration sections

New sections appear in the captive portal hardware page between the
existing pin-based input devices (Buttons/DHT) and the I²C section.
Field names match the firmware HeadlessWiFiSettings keys exactly so
defaults auto-populate from the settings JSON endpoint.

UI shows on all builds; effective only when firmware has HAS_RELAY
or HAS_POWER_MONITOR set. This matches the existing pattern where
the UI shows every section regardless of compile-time flags."
```

## Step 14 — Run `npm run build` and commit headers (5 min)

```bash
cd /opt/git/personal/ESPresense/ui
npm install                  # if not done already
npm run build
cd ..
```

This regenerates the embedded UI headers in `src/`:
- `src/ui_app_immutable_assets_css.h`
- `src/ui_app_immutable_chunks_js.h`
- `src/ui_app_immutable_entry_js.h`
- `src/ui_app_immutable_nodes_js.h`
- `src/ui_html.h`
- `src/ui_routes.h`
- `src/ui_svg.h`

### Verify

```bash
git status                          # should show updated ui_*.h files
pio run -e athom-smart-plug-v3      # final build sanity check
```

### Commit

```bash
git add src/ui_*.h
git commit -m "ui(hardware): regenerate UI build outputs

Regenerated by 'npm run build' in ui/ after adding the Relay and
Power Monitor sections. Includes the SHA-hashed bundle file names
so the embedded SPA serves the latest UI."
```

## Final phase verification

Run all of these and confirm clean builds:

```bash
cd /opt/git/personal/ESPresense
pio run -e athom-smart-plug-v3
pio run -e esp32
pio run -e esp32c3
pio run -e esp32c3-cdc
pio run -e esp32s3
pio run -e esp32s3-cdc
pio run -e esp32c6
pio run -e esp32c6-cdc
pio run -e m5stickc
pio run -e m5atom
pio run -e macchina-a0
```

Capture the firmware binary size for our env:

```bash
ls -la .pio/build/athom-smart-plug-v3/firmware.bin
# Expected: well under 1920 KB (1966080 bytes)
```

Capture the commit list:

```bash
git log --oneline main..HEAD
```

Expected output (~10 code commits + 1 PRD commit = 11 total ahead of main):
```
<hash> ui(hardware): regenerate UI build outputs
<hash> ui(hardware): add Relay and Power Monitor configuration sections
<hash> ci: add athom-smart-plug-v3 to build matrix
<hash> feat(athom-plug-v3): add platformio env and main.cpp wiring
<hash> feat(button,gui): tap toggles relay, long-hold factory resets
<hash> feat: add Relay and CSE7766 modules
<hash> feat(athom): board defaults and Button1 pin override
<hash> feat(mqtt): extend discovery helpers for HA Energy + bounded numbers
<hash> fix(esp32c3): disable I2C defaults that conflict with USB-CDC
8c737d7 docs: add PRD for Athom Smart Plug V3 ESPresense port    (← already exists, the base of this branch)
```

## Handoff to Phase 2

When Phase 1 is complete, the following should be true:

1. **Branch `feature/athom-plug-v3` exists** with the PRD-docs commit + ~9-10 code commits on top.
2. **All commits build cleanly** when checked out individually (`git checkout <hash>` then `pio run -e <env>` succeeds).
3. **Firmware binary** for athom-smart-plug-v3 is under 1920 KB.
4. **No regressions**: every other env in the CI matrix still builds.
5. **Working tree is clean** (`git status` shows nothing).
6. **Branch is pushed to r-teller/feature/athom-plug-v3** so Phase 2 can `git pull` from any machine.

Update `PRD/README.md` status section:
```
- ✅ Phase 1 — Implementation
- ⬜ Phase 2 — Build verification
```

Record handoff metadata in `PRD/phase-1-handoff.md` (create this file in the new session):
- Branch name: `feature/athom-plug-v3`
- Commit count: 11 (or whatever final count)
- Final commit hash: (record `git log -1 --format=%H`)
- Firmware size: (record from `ls -la` above)
- Any deviations from this PRD (if you modified an approach mid-phase, document why)

Phase 2 picks up by reading the handoff file and running its own verification suite.
