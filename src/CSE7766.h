#pragma once
#ifdef HAS_POWER_MONITOR

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

#endif  // HAS_POWER_MONITOR
