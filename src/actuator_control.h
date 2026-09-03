#pragma once

#include <Arduino.h>

namespace ActuatorControl {

// Call once, after the command-ack topic is known (after topics are
// built from the device UUID). Configures gpioPin as OUTPUT.
void begin(uint8_t gpioPin, const String &ackTopic);

// Call with the raw payload received on the command topic. "1" drives
// the GPIO HIGH, anything else drives it LOW — then publishes the
// resulting state back on the ack topic.
void handleCommand(const String &payload);

bool isOn();

}  // namespace ActuatorControl
