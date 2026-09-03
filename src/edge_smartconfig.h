#pragma once

#include <Arduino.h>

namespace EdgeSmartConfig {

// Call once from setup() when this variant is active (see main.cpp
// wiring instructions). Blocks until SmartConfig completes or times
// out (SMARTCONFIG_TIMEOUT_MS), then pulls initial config from
// Firebase if WiFi connected.
void begin();

// Call every loop() iteration. Handles periodic Firebase config
// re-pull and periodic telemetry push.
void loop();

}  // namespace EdgeSmartConfig
