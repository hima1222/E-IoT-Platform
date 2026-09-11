#include "actuator_control.h"
#include "mqtt_manager.h"

namespace ActuatorControl {

namespace {
    uint8_t pin;
    String ackTopic;
    bool state = false;
}  // namespace

// Configures the GPIO and stores the ack topic. Starts OFF.
void begin(uint8_t gpioPin, const String &topic) {
    pin = gpioPin;
    ackTopic = topic;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    state = false;
}

// "1" -> HIGH, anything else -> LOW. Always acks the resulting state.
void handleCommand(const String &payload) {
    state = (payload == "1");
    digitalWrite(pin, state ? HIGH : LOW);
    MqttManager::publish(ackTopic, state ? "1" : "0", /*retained=*/false, /*qos=*/1);
}

bool isOn() {
    return state;
}

}  // namespace ActuatorControl
