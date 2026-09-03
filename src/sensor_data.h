#pragma once

#include <Arduino.h>

namespace SensorData {

// Call once, after the telemetry topic is known (i.e. after
// AccountPairing::getUuid() is available and topics are built).
// intervalMs is the publish interval — Section 3 passes the value
// captured from the pairing flow's "data send interval selector".
void begin(const String &telemetryTopic, uint32_t intervalMs);

// Update the interval at runtime (e.g. if the app changes it later
// via a config message — not implemented yet, but the hook exists).
void setInterval(uint32_t intervalMs);

// Gate publishing on/off. Ownership Flow calls this true once cloud
// ack is received, false if the MQTT connection drops.
void setEnabled(bool enabled);

// Call every loop() iteration. Publishes at most once per interval,
// only while enabled and MqttManager::isConnected().
void loop();

}  // namespace SensorData
