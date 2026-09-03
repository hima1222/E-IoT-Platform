#pragma once

#include <Arduino.h>

namespace OnDemandPortal {

enum class PressType {
    SHORT,        // >= debounce, < BUTTON_LONG_PRESS_MS      -> reopen portal
    LONG,          // >= BUTTON_LONG_PRESS_MS, < VERY_LONG_MS   -> suggested: soft reset
    VERY_LONG       // >= BUTTON_VERY_LONG_PRESS_MS               -> suggested: hard reset
};

using PressCallback = void (*)(PressType type);

// Call once from setup(). Configures buttonPin as INPUT_PULLUP
// (button expected to short to GND when pressed — adjust in the .cpp
// if your hardware is active-high).
void begin(uint8_t buttonPin, PressCallback onPress);

// Call every loop() iteration. Non-blocking; classifies press length
// and fires the callback once, on release.
void loop();

}  // namespace OnDemandPortal
