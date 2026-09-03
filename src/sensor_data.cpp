#include "sensor_data.h"
#include "mqtt_manager.h"
#include <ArduinoJson.h>

namespace SensorData {

namespace {
    String telemetryTopic;
    uint32_t intervalMs = 10000;
    uint32_t lastPublishAt = 0;
    bool enabled = false;

    // Fixed set of sample strings for the "string" random value —
    // placeholder until a real sensor supplies an actual string
    // reading (e.g. a status code or device state string).
    const char *SAMPLE_STRINGS[] = {"ok", "idle", "active", "warn", "nominal"};
    constexpr uint8_t SAMPLE_STRINGS_LEN = 5;

    // 5 random values (int, double, string — mixed per the spec),
    // fixed key names so the backend schema can rely on them (see
    // Section 12 — Backend Alignment for keeping these two in sync).
    String buildPayload() {
        JsonDocument doc;
        doc["sensor1_int"]    = random(0, 1000);
        doc["sensor2_int"]    = random(-100, 100);
        doc["sensor3_double"] = random(0, 10000) / 100.0;
        doc["sensor4_double"] = random(0, 100000) / 1000.0;
        doc["sensor5_string"] = SAMPLE_STRINGS[random(0, SAMPLE_STRINGS_LEN)];

        String out;
        serializeJson(doc, out);
        return out;
    }
}  // namespace

void begin(const String &topic, uint32_t interval) {
    telemetryTopic = topic;
    intervalMs = interval;
    lastPublishAt = 0;  // publish on first eligible loop() rather than waiting a full interval
}

void setInterval(uint32_t interval) {
    intervalMs = interval;
}

void setEnabled(bool e) {
    enabled = e;
}

void loop() {
    if (!enabled || !MqttManager::isConnected()) return;
    if (millis() - lastPublishAt < intervalMs) return;

    lastPublishAt = millis();
    MqttManager::publish(telemetryTopic, buildPayload(), /*retained=*/false, /*qos=*/0);
}

}  // namespace SensorData
