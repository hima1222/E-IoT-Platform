#include "account_pairing.h"
#include "config.h"
 
#include <Preferences.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#if ENABLE_BLE_PAIRING
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#endif
 
namespace AccountPairing {
 
// Part A — Device identity
namespace {
    Preferences prefs;
    String deviceMac;
    String deviceUuid;
    String modelName = "smart-device";
    String fwVersion = "0.1.0";
 
    // Random v4-style UUID, generated once and persisted in NVS.
    String generateUuid() {
        uint8_t b[16];
        for (int i = 0; i < 16; i++) b[i] = (uint8_t)esp_random();
        b[6] = (b[6] & 0x0F) | 0x40;  // version 4
        b[8] = (b[8] & 0x3F) | 0x80;  // variant 10
 
        char buf[37];
        snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
            b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
        return String(buf);
    }
 
    bool identityInitialized = false;
 
    // Reads MAC, loads/creates UUID from NVS. Idempotent (guarded above).
    void identityBegin() {
        if (identityInitialized) return;  // safe to call from multiple places
        identityInitialized = true;
 
        deviceMac = WiFi.macAddress();  // valid before WiFi.begin()
 
        prefs.begin("dev_id", false);
        deviceUuid = prefs.getString("uuid", "");
        if (deviceUuid.length() == 0) {
            deviceUuid = generateUuid();
            prefs.putString("uuid", deviceUuid);
        }
        prefs.end();
    }
}  // namespace
 
String getMac() { return deviceMac; }
String getUuid() { return deviceUuid; }
 
void initIdentity() {
    identityBegin();
}
 
void setModelInfo(const String &model, const String &fw) {
    modelName = model;
    fwVersion = fw;
}
 
String getRegistrationPayload() {
    String json = "{";
    json += "\"mac\":\"" + deviceMac + "\",";
    json += "\"uuid\":\"" + deviceUuid + "\",";
    json += "\"model\":\"" + modelName + "\",";
    json += "\"fw\":\"" + fwVersion + "\"";
    json += "}";
    return json;
}
 
// Part B — WiFi AP pairing API (JSON only, no UI — app team owns the UI; ESP32 just needs to answer HTTP calls the app makes
// after it joins this device's SoftAP)
//
// Endpoints:
//   GET  /info      -> 200 { "mac":"..", "uuid":"..", "model":"..", "fw":".." }
//                      Lets the app confirm which physical device it's
//                      talking to before it commits to pairing it.
//
//   GET  /networks  -> 200 [ { "ssid":"..", "rssi":-54 }, ... ]
//                      Scan results — the app renders this list itself
//                      (Section 3's "device QR select" flow, dropdown
//                      etc. is entirely app-side).
//
//   POST /pair      body: { "ssid1":"..", "pass1":"..",
//                            "ssid2":"..", "pass2":".." }  (ssid2/pass2 optional)
//                    -> 202 { "status":"connecting" }         (accepted, async)
//                    -> 400 { "error":"ssid1 required" }      (bad body)
//                    Result of the connection attempt itself is
//                    reported through the AccountPairing::begin()
//                    callback, not this HTTP response — the app
//                    should poll GET /status or reconnect once its
//                    own network switches back.
//
//   GET  /status    -> 200 { "state":"idle|connecting|connected|failed",
//                             "ssid":".." }
//                      Lets the app poll if it doesn't want to rely on
//                      timing around the WiFi handover.
namespace {
    WebServer portalServer(AP_PORTAL_PORT);
    bool portalActive = false;
 
    String pendingSsid1, pendingPass1, pendingSsid2, pendingPass2;
    bool portalHasCandidates = false;
 
    String pairState = "idle";
    String pairSsid = "";
 
    PairBodyListener pairBodyListener = nullptr;
 
    // Small helper so every handler below doesn't repeat the content-type.
    void sendJson(int code, const String &body) {
        portalServer.send(code, "application/json", body);
    }
 
    // GET /info — device identity for the app to confirm before pairing.
    void handleInfo() {
        sendJson(200, getRegistrationPayload());
    }
 
    // GET /networks — returns the latest WiFi scan as JSON.
    void handleNetworks() {
        int n = WiFi.scanComplete();
        if (n == -2) {
            WiFi.scanNetworks(true);
            sendJson(202, "[]");  // scan just kicked off — app can retry shortly
            return;
        }
        if (n < 0) {
            sendJson(200, "[]");
            return;
        }
        JsonDocument doc;
        JsonArray arr = doc.to<JsonArray>();
        for (int i = 0; i < n; i++) {
            JsonObject net = arr.add<JsonObject>();
            net["ssid"] = WiFi.SSID(i);
            net["rssi"] = WiFi.RSSI(i);
        }
        WiFi.scanDelete();
        String out;
        serializeJson(doc, out);
        sendJson(200, out);
    }
 
    // POST /pair — validates the body, stores candidates, notifies the pair-body listener.
    void handlePair() {
        if (portalServer.method() != HTTP_POST) {
            sendJson(405, "{\"error\":\"POST required\"}");
            return;
        }
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, portalServer.arg("plain"));
        if (err || !doc["ssid1"].is<const char*>()) {
            sendJson(400, "{\"error\":\"ssid1 required\"}");
            return;
        }
 
        pendingSsid1 = doc["ssid1"] | "";
        pendingPass1 = doc["pass1"] | "";
        pendingSsid2 = doc["ssid2"] | "";
        pendingPass2 = doc["pass2"] | "";
        portalHasCandidates = true;
        pairState = "connecting";
 
        if (pairBodyListener) pairBodyListener(portalServer.arg("plain"));
 
        sendJson(202, "{\"status\":\"connecting\"}");
    }
 
    // GET /status — lets the app poll instead of guessing timing.
    void handleStatus() {
        String out = "{\"state\":\"" + pairState + "\",\"ssid\":\"" + pairSsid + "\"}";
        sendJson(200, out);
    }
 
    // Catch-all for unknown routes.
    void handleNotFound() {
        sendJson(404, "{\"error\":\"not found\"}");
    }
 
    // Brings up the SoftAP + HTTP routes.
    void wifiPortalStart() {
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASSWORD);
        WiFi.scanNetworks(true);
 
        portalServer.on("/info", HTTP_GET, handleInfo);
        portalServer.on("/networks", HTTP_GET, handleNetworks);
        portalServer.on("/pair", HTTP_POST, handlePair);
        portalServer.on("/status", HTTP_GET, handleStatus);
        portalServer.onNotFound(handleNotFound);
 
        portalServer.begin();
        portalActive = true;
    }
 
    // Tears down the SoftAP + HTTP server once pairing resolves.
    void wifiPortalStop() {
        portalServer.stop();
        WiFi.softAPdisconnect(true);
        portalActive = false;
    }
}  // namespace
 
#if ENABLE_BLE_PAIRING
// Part C — BLE alt-method pairing
namespace {
    const char *BLE_SERVICE_UUID   = "8c9a0001-6b1e-4a6a-9a6a-1e6b1e6b1e6b";
    const char *BLE_IDENTITY_UUID  = "8c9a0002-6b1e-4a6a-9a6a-1e6b1e6b1e6b";
    const char *BLE_CREDS_UUID     = "8c9a0003-6b1e-4a6a-9a6a-1e6b1e6b1e6b";
    const char *BLE_STATUS_UUID    = "8c9a0004-6b1e-4a6a-9a6a-1e6b1e6b1e6b";
 
    BLEServer *bleServer = nullptr;
    BLECharacteristic *bleStatusChar = nullptr;
    bool bleActive = false;
 
    String blePendingSsid, blePendingPass;
    bool bleHasCandidate = false;
 
    // Pushes a status string to the BLE status characteristic.
    void bleSetStatus(const String &s) {
        if (!bleStatusChar) return;
        bleStatusChar->setValue(s.c_str());
        bleStatusChar->notify();
    }
 
    class CredsWriteCallback : public BLECharacteristicCallbacks {
        void onWrite(BLECharacteristic *c) override {
            // Expected write format: "ssid,password"
            String value = String(c->getValue().c_str());
            int comma = value.indexOf(',');
            if (comma < 0) {
                bleSetStatus("failed");
                return;
            }
            blePendingSsid = value.substring(0, comma);
            blePendingPass = value.substring(comma + 1);
            bleHasCandidate = true;
            bleSetStatus("connecting");
        }
    };
    CredsWriteCallback bleCredsCallback;
 
    // Brings up the BLE GATT service (identity/creds/status characteristics).
    void blePairingStart() {
        String devName = String(BLE_DEVICE_NAME_PREFIX) + deviceMac.substring(9);
        BLEDevice::init(devName.c_str());
 
        bleServer = BLEDevice::createServer();
        BLEService *service = bleServer->createService(BLE_SERVICE_UUID);
 
        BLECharacteristic *identityChar = service->createCharacteristic(
            BLE_IDENTITY_UUID, BLECharacteristic::PROPERTY_READ);
        identityChar->setValue(getRegistrationPayload().c_str());
 
        BLECharacteristic *credsChar = service->createCharacteristic(
            BLE_CREDS_UUID, BLECharacteristic::PROPERTY_WRITE);
        credsChar->setCallbacks(&bleCredsCallback);
 
        bleStatusChar = service->createCharacteristic(
            BLE_STATUS_UUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY);
        bleStatusChar->addDescriptor(new BLE2902());
        bleStatusChar->setValue("idle");
 
        service->start();
 
        BLEAdvertising *advertising = BLEDevice::getAdvertising();
        advertising->addServiceUUID(BLE_SERVICE_UUID);
        advertising->setScanResponse(true);
        advertising->setMinPreferred(0x06); // Assists iOS connection stability
        advertising->setMaxPreferred(0x12); 

        BLEDevice::startAdvertising(); 
        DBGLN("[BLE] Radio advertising successfully started!");
        bleActive = true;
    }
 
    // Tears down BLE advertising + deinits the stack once pairing resolves.
    void blePairingStop() {
        if (!bleActive) return;
        BLEDevice::getAdvertising()->stop();
        BLEDevice::deinit(true);
        bleActive = false;
    }
}  // namespace
#else
// BLE disabled at compile time (ENABLE_BLE_PAIRING 0 in config.h) —
// stub implementations so Part D / begin() / loop() below don't need
// to know BLE is missing. The real BLE library is never #included in
// this build, saving a substantial amount of flash.
namespace {
    bool bleHasCandidate = false;
    String blePendingSsid, blePendingPass;
    void blePairingStart() {}
    void blePairingStop() {}
    void bleSetStatus(const String &) {}
}  // namespace
#endif
 
// Part D — begin()/loop() glue: race WiFi portal vs BLE
namespace {
    ResultCallback userCallback = nullptr;
    bool resolved = false;
 
    // Blocking WiFi.begin() + wait, used by both the portal and BLE paths.
    bool tryConnect(const String &ssid, const String &pass, uint32_t timeoutMs = 12000) {
        if (ssid.length() == 0) return false;
        WiFi.begin(ssid.c_str(), pass.c_str());
        uint32_t start = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
            if (portalActive) portalServer.handleClient();
            delay(250);
        }
        return WiFi.status() == WL_CONNECTED;
    }
 
    // Whichever transport wins calls this once; tears down the other.
    void finish(Result result, const String &ssid) {
        if (resolved) return;
        resolved = true;
        pairState = (result == Result::CONNECTED) ? "connected" : "failed";
        pairSsid = ssid;
        wifiPortalStop();
        blePairingStop();
        if (userCallback) userCallback(result, ssid);
    }
 
    // Tries ssid1, falls back to ssid2 if needed.
    void resolvePortalCandidates() {
        WiFi.mode(WIFI_AP_STA);  // keep AP alive while attempting STA connect
        bool ok = tryConnect(pendingSsid1, pendingPass1);
        String connected = ok ? pendingSsid1 : "";
        if (!ok && pendingSsid2.length() > 0) {
            ok = tryConnect(pendingSsid2, pendingPass2);
            connected = ok ? pendingSsid2 : "";
        }
        portalHasCandidates = false;
        finish(ok ? Result::CONNECTED : Result::FAILED, connected);
    }
 
    // Same strongest-first logic as the portal, for the single BLE candidate.
    void resolveBleCandidate() {
        WiFi.mode(WIFI_AP_STA);
        bool ok = tryConnect(blePendingSsid, blePendingPass);
        bleSetStatus(ok ? "connected" : "failed");
        bleHasCandidate = false;
        finish(ok ? Result::CONNECTED : Result::FAILED, ok ? blePendingSsid : "");
    }
}  // namespace
 
void setPairBodyListener(PairBodyListener listener) {
    pairBodyListener = listener;
}
 
void begin(ResultCallback onResult) {
    userCallback = onResult;
    resolved = false;
 
    identityBegin();
 
    wifiPortalStart();
    blePairingStart();
}
 
void loop() {
    if (resolved) return;
 
    if (portalActive) {
        portalServer.handleClient();
        if (portalHasCandidates) resolvePortalCandidates();
    }
    if (bleHasCandidate) {
        resolveBleCandidate();
    }
}
 
bool isPairing() {
    return !resolved;
}
 
}  // namespace AccountPairing