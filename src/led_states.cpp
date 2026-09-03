// SECTION 2 — LED STATES (implementation)

#include "led_states.h"
#include "config.h"

namespace LedStates {

namespace {
    State currentState = State::NOT_CONNECTED;
    uint32_t stateEnteredAt = 0;   // millis() when currentState was set
    bool ledOn = false;

    // ---- Pattern timing constants ----
    constexpr uint32_t BLINK_INTERVAL_MS = 500;    // 1Hz blink (500 on / 500 off) — "blink every 1s"
    constexpr uint32_t PORTAL_BLINK_MS   = 150;     // fast blink — visibly distinct from normal blink
    // Disconnected pulse: short-short-long-off, repeating (heartbeat-style)
    constexpr uint32_t PULSE_PATTERN_MS[] = {100, 100, 100, 700};  // on, off, on, off
    constexpr uint8_t  PULSE_PATTERN_LEN  = 4;

    void writeStatusLed(bool on) {
        ledOn = on;
        digitalWrite(LED_PIN_STATUS, on ? HIGH : LOW);
    }

    // Simple 50/50 blink at a given half-period.
    void applyBlink(uint32_t halfPeriodMs) {
        uint32_t elapsed = millis() - stateEnteredAt;
        bool shouldBeOn = (elapsed / halfPeriodMs) % 2 == 0;
        if (shouldBeOn != ledOn) writeStatusLed(shouldBeOn);
    }

    // Walks PULSE_PATTERN_MS on a loop, on/off/on/off with the given
    // per-segment durations, for the distinct "disconnected" pulse.
    void applyPulsePattern() {
        uint32_t cycleLen = 0;
        for (uint8_t i = 0; i < PULSE_PATTERN_LEN; i++) cycleLen += PULSE_PATTERN_MS[i];

        uint32_t elapsed = (millis() - stateEnteredAt) % cycleLen;
        uint32_t acc = 0;
        for (uint8_t i = 0; i < PULSE_PATTERN_LEN; i++) {
            acc += PULSE_PATTERN_MS[i];
            if (elapsed < acc) {
                bool shouldBeOn = (i % 2 == 0);  // segments 0,2 = on; 1,3 = off
                if (shouldBeOn != ledOn) writeStatusLed(shouldBeOn);
                return;
            }
        }
    }
}  // namespace

void begin() {
    pinMode(LED_PIN_POWER, OUTPUT);
    pinMode(LED_PIN_STATUS, OUTPUT);

    digitalWrite(LED_PIN_POWER, HIGH);  // power LED: on whenever the board has power
    writeStatusLed(false);

    stateEnteredAt = millis();
}

void setState(State state) {
    if (state == currentState) return;
    currentState = state;
    stateEnteredAt = millis();

    // States with a fixed (non-animated) output can be set immediately;
    // animated ones are driven every loop() call from here on.
    switch (currentState) {
        case State::NOT_CONNECTED:
            writeStatusLed(false);
            break;
        case State::CLOUD_CONFIRMED:
            writeStatusLed(true);
            break;
        default:
            break;  // CONFIG_PORTAL_ACTIVE / CONNECTING_WIFI / CLOUD_PENDING / CLOUD_DISCONNECTED animate in loop()
    }
}

State getState() {
    return currentState;
}

void loop() {
    switch (currentState) {
        case State::NOT_CONNECTED:
        case State::CLOUD_CONFIRMED:
            break;  // static — already set in setState()
        case State::CONFIG_PORTAL_ACTIVE:
            applyBlink(PORTAL_BLINK_MS);
            break;
        case State::CONNECTING_WIFI:
        case State::CLOUD_PENDING:
            applyBlink(BLINK_INTERVAL_MS);
            break;
        case State::CLOUD_DISCONNECTED:
            applyPulsePattern();
            break;
    }
}

}  // namespace LedStates
