#include "on_demand_portal.h"
#include "config.h"

namespace OnDemandPortal {

namespace {
    uint8_t pin;
    PressCallback callback = nullptr;

    bool lastRawState = HIGH;     // HIGH = not pressed (INPUT_PULLUP, active-low button)
    bool debouncedState = HIGH;
    uint32_t lastEdgeAt = 0;
    uint32_t pressStartedAt = 0;
}  // namespace

void begin(uint8_t buttonPin, PressCallback onPress) {
    pin = buttonPin;
    callback = onPress;
    pinMode(pin, INPUT_PULLUP);
    lastRawState = digitalRead(pin);
    debouncedState = lastRawState;
}

void loop() {
    bool raw = digitalRead(pin);

    if (raw != lastRawState) {
        lastEdgeAt = millis();
        lastRawState = raw;
    }

    if ((millis() - lastEdgeAt) >= BUTTON_DEBOUNCE_MS && raw != debouncedState) {
        debouncedState = raw;

        if (debouncedState == LOW) {
            // Button just pressed.
            pressStartedAt = millis();
        } else {
            // Button just released — classify the hold duration.
            uint32_t heldMs = millis() - pressStartedAt;
            PressType type;
            if (heldMs >= BUTTON_VERY_LONG_PRESS_MS) {
                type = PressType::VERY_LONG;
            } else if (heldMs >= BUTTON_LONG_PRESS_MS) {
                type = PressType::LONG;
            } else {
                type = PressType::SHORT;
            }
            if (callback) callback(type);
        }
    }
}

}  // namespace OnDemandPortal
