#include "CSE7766.h"

#ifdef HAS_POWER_MONITOR

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

int8_t rxPin = DEFAULT_CSE7766_RX_PIN;
float currentLimitAmps = DEFAULT_CURRENT_LIMIT_AMPS;
int updateIntervalSec = DEFAULT_POWER_UPDATE_INTERVAL;

uint8_t buffer[24];
uint8_t bufferPos = 0;

float voltage = NAN;
float current = NAN;
float power = NAN;

double whPerPulse = 0.0;
double totalEnergyWh = 0.0;
double lastSavedWh = 0.0;
uint16_t lastCfPulses = 0;
bool energyInitialized = false;

double voltageSum = 0.0, currentSum = 0.0, powerSum = 0.0;
unsigned int sampleCount = 0;

bool tripped = false;
unsigned long setupMillis = 0;
unsigned int validCurrentPackets = 0;
unsigned int overLimitCount = 0;
unsigned long lastTripLogMillis = 0;

unsigned long lastEnergySave = 0;
unsigned long lastPublishMillis = 0;
bool online = false;

void Setup() {
    if (rxPin < 0) return;

    Serial1.setRxBufferSize(256);
    Serial1.begin(4800, SERIAL_8E1, rxPin, -1);
    while (Serial1.available()) Serial1.read();
    setupMillis = millis();

    if (SPIFFS.exists("/cse7766_total_wh")) {
        File f = SPIFFS.open("/cse7766_total_wh", "r");
        if (f) {
            totalEnergyWh = f.readString().toDouble();
            f.close();
            lastSavedWh = totalEnergyWh;
            Log.printf("CSE7766: restored total_energy = %.3f kWh\r\n", totalEnergyWh / 1000.0);
        }
    }

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

float getCurrentPower() { return isnan(power) ? 0.0f : power; }
uint32_t getCfPulses() { return lastCfPulses; }

void clearTrip() {
    if (!tripped) return;
    tripped = false;
    SPIFFS.remove("/relay_tripped");
    pub((roomsTopic + "/relay_trip").c_str(), 0, true, "OFF");
    Log.println("CSE7766: trip cleared by manual relay re-engage");
}

bool validatePacket() {
    if (buffer[1] != 0x5A) return false;

    uint8_t hdr = buffer[0];
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

void fireTrip(float i) {
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

void parsePacket() {
    if (!validatePacket()) {
        memmove(buffer, buffer + 1, 23);
        bufferPos = 23;
        return;
    }

    bufferPos = 0;

    uint8_t hdr = buffer[0];

    if (hdr == 0xAA) {
        static unsigned long lastNotCalLog = 0;
        if (millis() - lastNotCalLog > 5000) {
            Log.println("CSE7766: chip reports not calibrated");
            lastNotCalLog = millis();
        }
        return;
    }

    uint8_t adj = buffer[20];

    bool powerForcedZero = ((hdr & 0xF0) == 0xF0) && (hdr & 0x02);

    uint32_t voltage_cal   = read24BE(2);
    uint32_t voltage_cycle = read24BE(5);
    uint32_t current_cal   = read24BE(8);
    uint32_t current_cycle = read24BE(11);
    uint32_t power_cal     = read24BE(14);
    uint32_t power_cycle   = read24BE(17);
    uint16_t cf_pulses     = ((uint16_t)buffer[21] << 8) | buffer[22];

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

    if (!energyInitialized) {
        whPerPulse = (double)power_cal / 1e6 / 3600.0;
        lastCfPulses = cf_pulses;
        energyInitialized = true;
    } else {
        uint16_t diff = cf_pulses - lastCfPulses;
        lastCfPulses = cf_pulses;
        totalEnergyWh += (double)diff * whPerPulse;
    }

    if (!isnan(voltage) && !isnan(current) && !isnan(power)) {
        voltageSum += voltage;
        currentSum += current;
        powerSum += power;
        sampleCount++;
    }

    if (tripped) return;
    if (millis() - setupMillis < 5000) return;
    if (!(adj & 0x20)) return;

    validCurrentPackets++;
    if (validCurrentPackets < 4) return;

    if (currentLimitAmps > 0 && current > currentLimitAmps) {
        overLimitCount++;
        if (overLimitCount >= 2) {
            fireTrip(current);
            overLimitCount = 0;
        }
    } else {
        overLimitCount = 0;
    }
}

void Loop() {
    if (rxPin < 0) return;

    while (Serial1.available()) {
        if (bufferPos >= 24) bufferPos = 0;
        buffer[bufferPos++] = Serial1.read();
        if (bufferPos == 24) parsePacket();
    }

    unsigned long now = millis();

    if (sampleCount > 0 && (now - lastPublishMillis) >= (unsigned long)updateIntervalSec * 1000UL) {
        lastPublishMillis = now;

        double v_avg = voltageSum / sampleCount;
        double i_avg = currentSum / sampleCount;
        double p_avg = powerSum / sampleCount;
        double s     = v_avg * i_avg;
        double q     = sqrt(fmax(0.0, s * s - p_avg * p_avg));
        double pf    = (s > 0.001) ? (p_avg / s) * 100.0 : 0.0;

        StaticJsonDocument<256> j;
        j["voltage"]        = roundf(v_avg * 10.0f) / 10.0f;
        j["current"]        = roundf(i_avg * 1000.0f) / 1000.0f;
        j["power"]          = roundf(p_avg * 10.0f) / 10.0f;
        j["apparent_power"] = roundf(s * 10.0f) / 10.0f;
        j["reactive_power"] = roundf(q * 10.0f) / 10.0f;
        j["power_factor"]   = roundf(pf * 10.0f) / 10.0f;
        j["total_energy"]   = roundf((float)totalEnergyWh) / 1000.0f;

        String out;
        serializeJson(j, out);
        pub((roomsTopic + "/telemetry").c_str(), 0, true, out.c_str());

        voltageSum = currentSum = powerSum = 0.0;
        sampleCount = 0;
    }

    if (now - lastEnergySave > 5UL * 60UL * 1000UL) {
        lastEnergySave = now;
        if (fabs(totalEnergyWh - lastSavedWh) >= 10.0) {
            spurt("/cse7766_total_wh", String(totalEnergyWh, 3));
            lastSavedWh = totalEnergyWh;
        }
    }
}

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
