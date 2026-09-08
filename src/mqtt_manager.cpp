#include "mqtt_manager.h"
#include "config.h"

#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <MQTT.h>  // 256dpi/MQTT

namespace MqttManager {

namespace {
    WiFiClientSecure secureClient;
    WiFiClient plainClient;
    bool usingTls = false;

    MQTTClient client(512);  // payload buffer size in bytes — raise if you send larger JSON
    MessageCallback userCallback = nullptr;
    bool hasWill = false;
    WillConfig will;
    bool connected = false;

    void onRawMessage(String &topic, String &payload) {
        if (userCallback) userCallback(topic, payload);
    }
}  // namespace

void begin(const char *host, uint16_t port, bool useTls, MessageCallback onMessage) {
    usingTls = useTls;
    userCallback = onMessage;

    if (usingTls) {
#if defined(MQTT_CA_CERT)
        secureClient.setCACert(MQTT_CA_CERT);
#else
        // No CA cert configured — falls back to no server verification.
        // Fine for bring-up, NOT fine for production. Define
        // MQTT_CA_CERT in config.h with your broker's real CA cert.
        secureClient.setInsecure();
        DBGLN("[MqttManager] WARNING: TLS enabled but no CA cert set — connection is unverified.");
#endif
        client.begin(host, port, secureClient);
    } else {
        client.begin(host, port, plainClient);
    }

    client.onMessage(onRawMessage);

    DBG("[MqttManager] Configured for broker protocol ");
    DBG(MQTT_BROKER_PROTOCOL);
    DBGLN(" (256dpi/MQTT client — v3.1.1 only; see mqtt_manager.h for the v5.0 caveat).");
}

void setWill(const WillConfig &w) {
    will = w;
    hasWill = true;
}

bool connect(const String &clientId) {
    if (hasWill) {
        client.setWill(will.topic.c_str(), will.payload.c_str(), will.retained, will.qos);
    }

    if (!client.connect(clientId.c_str())) {
        DBGF("[MqttManager] Connect failed. lwmqtt error=%d, return code=%d\n",
            (int)client.lastError(), (int)client.returnCode());
        connected = false;
        return false;
    }

    DBGLN("[MqttManager] Connected successfully.");   
    connected = true;
    return true;
}

void publishBirth(const String &topic, const String &payload) {
    publish(topic, payload, /*retained=*/true, /*qos=*/1);
}

void publish(const String &topic, const String &payload, bool retained, uint8_t qos) {
    client.publish(topic, payload, retained, qos);
}

void subscribe(const String &topic, uint8_t qos) {
    client.subscribe(topic, qos);
}

void loop() {
    client.loop();
    if (connected && !client.connected()) {
        connected = false;  // caller's next isConnected() check will see the drop
    }
}

bool isConnected() {
    return connected && client.connected();
}

}  // namespace MqttManager
