/* SECTION 3 — OWNERSHIP FLOW (implementation)
  Organized into:
    Part A — flash-backed config store (WiFi + user/ownership fields)
    Part B — pair-body listener (pulls ownership fields out of
             Section 1's POST /pair JSON body)
    Part C — MQTT: connect, birth/LWT, registration publish (QoS2),
             ack subscribe, dispatch to Section 7/8
    Part D — public begin()/loop()/autoConnect glue
 
  Requires the 256dpi/MQTT library (via mqtt_manager.h/.cpp).
 */
 
#include "ownership_flow.h"
#include "account_pairing.h"
#include "led_states.h"
#include "config.h"
#include "sensor_data.h"
#include "actuator_control.h"
#include "mqtt_manager.h"
#include "fota.h"
 
#include <Preferences.h>
#include <ArduinoJson.h>
#include <WiFi.h>
 
namespace OwnershipFlow {
 
// Part A — flash-backed config store
namespace {
    Preferences ownPrefs;
 
    String cfgSsid, cfgPass;
    String cfgDeviceQr, cfgUserName, cfgUserEmail, cfgUserMobile;
    uint32_t cfgIntervalMs = SENSOR_PUBLISH_INTERVAL_DEFAULT_MS;
    bool cfgPresent = false;
 
    // Captured from the same POST /pair body Section 1 parses, so
    // that once AccountPairing reports *which* SSID connected, this
    // module can look up its matching password without needing
    // Section 1's callback signature to carry it through.
    String candSsid1, candPass1, candSsid2, candPass2;
 
    void loadConfig() {
        ownPrefs.begin("own_cfg", true);  // read-only open
        cfgPresent = ownPrefs.getBool("configured", false);
        cfgSsid       = ownPrefs.getString("ssid", "");
        cfgPass       = ownPrefs.getString("pass", "");
        cfgDeviceQr   = ownPrefs.getString("qr", "");
        cfgUserName   = ownPrefs.getString("name", "");
        cfgUserEmail  = ownPrefs.getString("email", "");
        cfgUserMobile = ownPrefs.getString("mobile", "");
        cfgIntervalMs = ownPrefs.getUInt("interval", SENSOR_PUBLISH_INTERVAL_DEFAULT_MS);
        ownPrefs.end();
    }
 
    void saveConfig(const String &ssid, const String &pass) {
        ownPrefs.begin("own_cfg", false);
        ownPrefs.putBool("configured", true);
        ownPrefs.putString("ssid", ssid);
        ownPrefs.putString("pass", pass);
        ownPrefs.putString("qr", cfgDeviceQr);
        ownPrefs.putString("name", cfgUserName);
        ownPrefs.putString("email", cfgUserEmail);
        ownPrefs.putString("mobile", cfgUserMobile);
        ownPrefs.putUInt("interval", cfgIntervalMs);
        ownPrefs.end();
 
        cfgSsid = ssid;
        cfgPass = pass;
        cfgPresent = true;
    }
}  // namespace
 
bool hasSavedConfig() {
    return cfgPresent;
}
 
// Part B — pair-body listener: pulls ownership fields out of the
// same POST /pair JSON body Section 1 already parses for ssid/pass.
namespace {
    // Very light validation — real validation (email format, mobile
    // format) is expected to happen app-side before it ever reaches
    // the device; this is just a sanity backstop.
    bool looksLikeEmail(const String &s) {
        int at = s.indexOf('@');
        return at > 0 && s.indexOf('.', at) > at;
    }
 
    void onPairBody(const String &rawJsonBody) {
        JsonDocument doc;
        if (deserializeJson(doc, rawJsonBody)) return;  // Section 1 already rejects bad JSON
 
        cfgDeviceQr   = doc["deviceQr"]   | "";
        cfgUserName   = doc["name"]        | "";
        cfgUserEmail  = doc["email"]        | "";
        cfgUserMobile = doc["mobile"]        | "";
        cfgIntervalMs = doc["intervalMs"] | SENSOR_PUBLISH_INTERVAL_DEFAULT_MS;
 
        // Same fields Section 1 reads for ssid1/pass1/ssid2/pass2 —
        // duplicated here (not passed via callback) so this module
        // can later match connectedSsid to the right password.
        candSsid1 = doc["ssid1"] | "";
        candPass1 = doc["pass1"] | "";
        candSsid2 = doc["ssid2"] | "";
        candPass2 = doc["pass2"] | "";
 
        if (cfgUserEmail.length() > 0 && !looksLikeEmail(cfgUserEmail)) {
            DBGLN("[OwnershipFlow] warning: email field doesn't look valid: " + cfgUserEmail);
        }
    }
}  // namespace
 
// Part C — MQTT: connect, birth/LWT, registration (QoS2), ack,
// dispatch to Section 7 (sensor data) / Section 8 (actuator)
namespace {
    String topicStatus, topicRegister, topicRegisterAck, topicTelemetry, topicCmd, topicCmdAck;
    bool mqttConnected = false;
    bool cloudAckReceived = false;
 
    // SensorData::begin() / ActuatorControl::begin() only need to run
    // once per boot (topics don't change), not on every reconnect —
    // re-running ActuatorControl::begin() on a reconnect would force
    // any actuator that was ON back to LOW, which isn't something a
    // brief WiFi/MQTT blip should do to physical hardware.
    bool sectionsInitialized = false;
 
    void buildTopics() {
        String uuid = AccountPairing::getUuid();
        topicStatus      = "devices/" + uuid + "/status";
        topicRegister    = "devices/" + uuid + "/register";
        topicRegisterAck = "devices/" + uuid + "/register/ack";
        topicTelemetry   = "devices/" + uuid + "/telemetry";
        topicCmd         = "devices/" + uuid + "/cmd";
        topicCmdAck      = "devices/" + uuid + "/cmd/ack";
    }
 
    void onMqttMessage(const String &topic, const String &payload) {
        if (topic == topicRegisterAck) {
            if (!cloudAckReceived) {
                cloudAckReceived = true;
                DBGLN("[OwnershipFlow] Cloud ack received.");
                LedStates::setState(LedStates::State::CLOUD_CONFIRMED);
                MqttManager::subscribe(topicCmd, 1);  // now safe to accept actuator commands
                SensorData::setEnabled(true);          // start publishing telemetry
            }
        } else if (topic == topicCmd) {
            ActuatorControl::handleCommand(payload);
        } else if (topic == Fota::getUpdateTopicAll() || topic == Fota::getUpdateTopicDevice()) {
            Fota::handleUpdateNotice(payload);
        }
    }
 
    bool connectMqtt() {
        if (!sectionsInitialized) {
            sectionsInitialized = true;
            SensorData::begin(topicTelemetry, cfgIntervalMs);
            ActuatorControl::begin(GPIO_ACTUATOR_PIN, topicCmdAck);
        }
 
        MqttManager::begin(MQTT_BROKER_HOST, MQTT_BROKER_PORT, MQTT_USE_TLS, onMqttMessage);
 
        MqttManager::WillConfig will;
        will.topic = topicStatus;
        will.payload = "offline";
        will.retained = true;
        will.qos = 1;
        MqttManager::setWill(will);
 
        String clientId = "esp32-" + AccountPairing::getUuid();
 
        if (!MqttManager::connect(clientId)) {
            DBGLN("[OwnershipFlow] MQTT connect failed.");
            return false;
        }
 
        // Birth message — we're alive and it's not the LWT.
        MqttManager::publishBirth(topicStatus, "online");
 
        // Registration payload at QoS2 per the spec (Section 6:
        // "Use QoS2 for provisioning/config messages").
        JsonDocument doc;
        deserializeJson(doc, AccountPairing::getRegistrationPayload());
        doc["name"] = cfgUserName;
        doc["email"] = cfgUserEmail;
        doc["mobile"] = cfgUserMobile;
        doc["deviceQr"] = cfgDeviceQr;
        doc["intervalMs"] = cfgIntervalMs;
        String regPayload;
        serializeJson(doc, regPayload);
        MqttManager::publish(topicRegister, regPayload, /*retained=*/false, /*qos=*/2);
 
        MqttManager::subscribe(topicRegisterAck, 1);
        Fota::onMqttConnected();  // subscribes to update topics and publishes firmware version attribute
 
        mqttConnected = true;
        return true;
    }
}  // namespace
 
// Part D — public API
void begin() {
    loadConfig();
    AccountPairing::setPairBodyListener(onPairBody);
}
 
void onFreshPairingConnected(const String &connectedSsid) {
    // Match the SSID AccountPairing reports as connected back to
    // whichever candidate (1 or 2) it came from, so we persist the
    // right password.
    String pass = (connectedSsid == candSsid1) ? candPass1
                : (connectedSsid == candSsid2) ? candPass2
                : "";
    saveConfig(connectedSsid, pass);
 
    buildTopics();
    connectMqtt();
}
 
void autoConnectFromSavedConfig() {
    LedStates::setState(LedStates::State::CONNECTING_WIFI);
    WiFi.mode(WIFI_STA);
    WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
 
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
        delay(250);
    }
 
    if (WiFi.status() != WL_CONNECTED) {
        DBGLN("[OwnershipFlow] Saved-config WiFi connect failed.");
        LedStates::setState(LedStates::State::NOT_CONNECTED);
        return;
    }
 
    LedStates::setState(LedStates::State::CLOUD_PENDING);
    buildTopics();
    connectMqtt();
}
 
void loop() {
    if (!mqttConnected) return;
 
    MqttManager::loop();
 
    if (!MqttManager::isConnected()) {
        if (mqttConnected) {
            mqttConnected = false;
            cloudAckReceived = false;
            SensorData::setEnabled(false);
            LedStates::setState(LedStates::State::CLOUD_DISCONNECTED);
            // Section 6 formalizes reconnect/backoff strategy; a bare
            // retry here would be reasonable but is left out to avoid
            // this module reaching ahead of its section.
        }
        return;
    }
 
    SensorData::loop();
}
 
}  // namespace OwnershipFlow