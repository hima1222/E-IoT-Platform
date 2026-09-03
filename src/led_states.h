#pragma once

#include <Arduino.h>

namespace LedStates {

enum class State {
    NOT_CONNECTED,        // status LED off (red power LED still on)
    CONFIG_PORTAL_ACTIVE,  // pairing portal (AP/BLE) is open — distinct pattern
    CONNECTING_WIFI,        // WiFi handshake in progress — 1s blink
    CLOUD_PENDING,           // WiFi up, waiting on cloud/MQTT ack — 1s blink continues
    CLOUD_CONFIRMED,          // WiFi + cloud ack received — solid ON
    CLOUD_DISCONNECTED         // was confirmed, MQTT/cloud link dropped — distinct pulse
};

// Call once from setup(). Configures the LED pins (see config.h for
// LED_PIN_POWER / LED_PIN_STATUS) and turns the power LED on.
void begin();

// Call every loop() iteration — advances whichever blink/pulse
// pattern the current state uses. Non-blocking.
void loop();

// Change the status LED's behavior. Cheap to call repeatedly with
// the same state — it only resets timing when the state actually
// changes.
void setState(State state);

State getState();

}  // namespace LedStates
