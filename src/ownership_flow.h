#pragma once

#include <Arduino.h>

namespace OwnershipFlow {

// Call once from setup(), before AccountPairing::begin(). Loads any
// previously saved WiFi + ownership config from flash and registers
// this module's listener with AccountPairing.
void begin();

// True if a previous ownership flow completed and saved config is on
// flash. main.cpp uses this to decide whether to open the pairing
// portal at all on this boot ("Boot unconfigured -> start SoftAP"
// implies boot *configured* skips it).
bool hasSavedConfig();

// Connects WiFi using the saved credentials (skips the portal
// entirely) and, on success, runs the same post-connect flow as a
// fresh pairing (MQTT connect, birth message, registration publish).
// Call from setup() when hasSavedConfig() is true instead of
// AccountPairing::begin().
void autoConnectFromSavedConfig();

// Call from AccountPairing's result callback when Result::CONNECTED
// fires after a *fresh* pairing (portal was used this boot). Persists
// the WiFi + ownership fields captured via the pair-body listener,
// then proceeds to MQTT connect / registration.
void onFreshPairingConnected(const String &connectedSsid);

// Call every loop() iteration once WiFi is connected. Pumps the MQTT
// client and handles the periodic dummy sensor publish.
void loop();

}  // namespace OwnershipFlow
