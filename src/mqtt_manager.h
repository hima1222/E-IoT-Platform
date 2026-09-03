#pragma once

#include <Arduino.h>

namespace MqttManager {

struct WillConfig {
    String topic;
    String payload;
    bool retained = true;
    uint8_t qos = 1;
};

using MessageCallback = void (*)(const String &topic, const String &payload);

// Call once, before connect(). host/port/useTls describe the broker;
// onMessage is invoked for every message on any topic this client is
// subscribed to.
void begin(const char *host, uint16_t port, bool useTls, MessageCallback onMessage);

// Optional — call before connect() if you want a Last Will message
// sent by the broker on our behalf if we drop off ungracefully.
void setWill(const WillConfig &will);

// Connects with the given client ID. Returns false if the connect
// attempt failed (caller decides retry policy — this module doesn't
// loop/retry on its own). Logs which MQTT protocol version this
// client implements, per the "confirm broker version" checklist item
// — actual negotiation is the broker's job; this just makes the
// firmware's side explicit and visible in the serial log.
bool connect(const String &clientId);

// Publishes a birth message — a small wrapper around publish() with
// retained=true, qos=1, so every section that connects doesn't have
// to remember those defaults itself.
void publishBirth(const String &topic, const String &payload);

// qos: 0, 1, or 2 — all three are supported by the underlying client.
void publish(const String &topic, const String &payload, bool retained, uint8_t qos);

void subscribe(const String &topic, uint8_t qos);

// Call every loop() iteration once connected — pumps the underlying
// client so incoming messages and QoS handshakes get processed.
void loop();

bool isConnected();

}  // namespace MqttManager
