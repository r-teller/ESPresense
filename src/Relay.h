#pragma once
#ifdef HAS_RELAY

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

void set(bool on);
void toggle();
bool getState();
}  // namespace Relay

#endif  // HAS_RELAY
