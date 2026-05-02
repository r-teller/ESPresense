#include "Relay.h"

#ifdef HAS_RELAY

#include <SPIFFS.h>
#include <HeadlessWiFiSettings.h>

#include "defaults.h"
#include "globals.h"
#include "mqtt.h"
#include "string_utils.h"

#ifdef HAS_POWER_MONITOR
#include "CSE7766.h"
#endif

namespace Relay {

static int relayPin = -1;
static int restoreMode = 0;
static bool state = false;
static bool dirty = false;
static unsigned long lastSave = 0;
static bool online = false;

void EarlyInit() {
#if DEFAULT_RELAY_PIN >= 0
    pinMode(DEFAULT_RELAY_PIN, OUTPUT);
    digitalWrite(DEFAULT_RELAY_PIN, LOW);
#endif
}

void Setup() {
    if (relayPin < 0) return;

    pinMode(relayPin, OUTPUT);

    bool tripPersisted = SPIFFS.exists("/relay_tripped");

    if (restoreMode == 0) {
        state = false;
    } else if (restoreMode == 1) {
        state = !tripPersisted;
    } else if (restoreMode == 2) {
        if (SPIFFS.exists("/relay_state")) {
            File f = SPIFFS.open("/relay_state", "r");
            if (f) {
                String content = f.readString();
                f.close();
                state = (content == "1");
            }
        }
    }

    digitalWrite(relayPin, state ? HIGH : LOW);
}

void ConnectToWifi(bool updating) {
    relayPin = HeadlessWiFiSettings.integer("relay_pin", DEFAULT_RELAY_PIN, "Relay pin (-1 to disable)");
    std::vector<String> modes = {"Always Off", "Always On", "Restore Last"};
    restoreMode = HeadlessWiFiSettings.dropdown("relay_restore_mode", modes, DEFAULT_RELAY_RESTORE_MODE, "Relay restore mode after boot");
}

void SerialReport() {
    Log.print("Relay:        ");
    Log.println(relayPin >= 0 ? "enabled" : "disabled");
}

static void Save() {
    if (restoreMode != 2 || !dirty) return;
    if (spurt("/relay_state", state ? "1" : "0")) dirty = false;
}

void Loop() {
    if (millis() - lastSave > 15000) {
        lastSave = millis();
        Save();
    }
}

bool SendDiscovery() {
    if (relayPin < 0) return true;
    return sendSwitchDiscovery("relay", EC_NONE);
}

bool SendOnline() {
    if (online || relayPin < 0) return true;
    if (!pub((roomsTopic + "/relay").c_str(), 0, true, state ? "ON" : "OFF")) return false;
    online = true;
    return true;
}

bool Command(String& command, String& pay) {
    if (command == "relay") {
        if (pay == "ON" || pay == "1" || pay == "on") {
            set(true);
        } else if (pay == "OFF" || pay == "0" || pay == "off") {
            set(false);
        }
        return true;
    } else if (command == "relay_pin") {
        spurt("/relay_pin", pay);
        return true;
    } else if (command == "relay_restore_mode") {
        spurt("/relay_restore_mode", pay);
        return true;
    }
    return false;
}

void set(bool on) {
    if (state == on) return;
    state = on;
    if (relayPin >= 0) digitalWrite(relayPin, state ? HIGH : LOW);
    dirty = true;
    pub((roomsTopic + "/relay").c_str(), 0, true, state ? "ON" : "OFF");

    if (on) {
        SPIFFS.remove("/relay_tripped");
#ifdef HAS_POWER_MONITOR
        CSE7766::clearTrip();
#endif
    }
}

void toggle() {
    set(!state);
}

bool getState() {
    return state;
}

}  // namespace Relay

#endif  // HAS_RELAY
