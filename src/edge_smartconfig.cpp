/*
 * ==========================================================
 *  SECTION 11 — EDGE / SMARTCONFIG VARIANT (implementation)
 * ==========================================================
 * Organized into:
 *   Part A — SmartConfig WiFi pairing
 *   Part B — Firebase (mobizt/FirebaseClient) auth + RTDB helpers
 *   Part C — dynamic sensor/actuator config (pulled from Firebase,
 *            no reflash needed to add/remove either)
 *   Part D — telemetry + state push
 *   Part E — public begin()/loop() glue
 *
 * Uses mobizt/FirebaseClient (https://github.com/mobizt/FirebaseClient)
 * per your team's requirement, in "await" (blocking) mode — matching
 * the synchronous style the rest of this project already uses,
 * rather than the library's async/callback mode.
 *
 * IMPORTANT — auth model changed from the REST-only version:
 * This library's documented Realtime Database auth is UserAuth
 * (email + password + your Firebase project's Web API Key) — a real
 * Firebase Authentication user, not a single static "database
 * secret" token. That's a genuine setup change on the Firebase side
 * (create a dedicated Auth user for this device fleet to sign in
 * as), not just a firmware change. See config.h edits in the
 * accompanying instructions.
 *
 * STORAGE NOTE: everything in this file — including the
 * FirebaseClient / ENABLE_USER_AUTH / ENABLE_DATABASE include below
 * — is wrapped in #if USE_EDGE_SMARTCONFIG_VARIANT. When that flag
 * is 0 (the default — see config.h), FirebaseClient.h is never
 * parsed and its bundled TLS stack (ESP_SSLClient/BearSSL) is never
 * linked into the binary at all, not just "present but unused." This
 * is the file responsible for most of this project's flash usage
 * when the variant isn't active.
 */

#include "edge_smartconfig.h"
#include "config.h"

#if USE_EDGE_SMARTCONFIG_VARIANT

// Enable only the modules this file uses — keeps the library's
// memory/flash footprint down. Must be defined before <FirebaseClient.h>
// is included anywhere in this translation unit.
#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include "account_pairing.h"

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <ArduinoJson.h>
#include <vector>

namespace EdgeSmartConfig {

// ---------------------------------------------------------------
// Part A — SmartConfig WiFi pairing
// ---------------------------------------------------------------
namespace {
    bool wifiReady = false;

    bool runSmartConfig() {
        WiFi.mode(WIFI_STA);
        WiFi.beginSmartConfig();

        DBGLN("[EdgeSmartConfig] Waiting for app to send credentials via SmartConfig...");
        uint32_t start = millis();
        while (!WiFi.smartConfigDone()) {
            if (millis() - start > SMARTCONFIG_TIMEOUT_MS) {
                DBGLN("[EdgeSmartConfig] SmartConfig timed out.");
                WiFi.stopSmartConfig();
                return false;
            }
            delay(200);
        }
        DBGLN("[EdgeSmartConfig] Credentials received, connecting...");

        start = millis();
        while (WiFi.status() != WL_CONNECTED) {
            if (millis() - start > WIFI_CONNECT_TIMEOUT_MS_FALLBACK) {
                DBGLN("[EdgeSmartConfig] WiFi connect timed out after SmartConfig.");
                return false;
            }
            delay(250);
        }
        DBGLN("[EdgeSmartConfig] WiFi connected: " + WiFi.SSID());
        return true;
    }
}  // namespace

// ---------------------------------------------------------------
// Part B — Firebase auth + RTDB helpers (mobizt/FirebaseClient)
// ---------------------------------------------------------------
namespace {
    UserAuth firebaseUserAuth(FIREBASE_API_KEY, FIREBASE_USER_EMAIL, FIREBASE_USER_PASSWORD);
    FirebaseApp firebaseApp;
    WiFiClientSecure firebaseSslClient;
    AsyncClientClass firebaseClient(firebaseSslClient);
    RealtimeDatabase Database;

    void firebaseAuthCallback(AsyncResult &aResult) {
        // Only logs — nothing here blocks startup; FirebaseApp::ready()
        // is what callers actually gate on.
        if (aResult.isError()) {
            DBGF("[EdgeSmartConfig] Firebase auth error: %s\n", aResult.error().message().c_str());
        }
    }

    void initFirebase() {
        firebaseSslClient.setInsecure();  // TODO: pin Firebase's real CA before production

        initializeApp(firebaseClient, firebaseApp, getAuth(firebaseUserAuth), firebaseAuthCallback, "authTask");
        firebaseApp.getApp<RealtimeDatabase>(Database);
        Database.url(FIREBASE_DATABASE_URL);
    }

    // Blocking ("await" mode — no callback/AsyncResult passed) GET.
    // Returns "" on failure or if not yet authenticated.
    String firebaseGet(const String &path) {
        if (!firebaseApp.ready()) return "";
        String value = Database.get<String>(firebaseClient, path);
        if (firebaseClient.lastError().code() != 0) {
            DBGF("[EdgeSmartConfig] Firebase GET %s failed: %s\n",
                          path.c_str(), firebaseClient.lastError().message().c_str());
            return "";
        }
        return value;
    }

    // Blocking PUT (full overwrite at path) of a raw JSON object body.
    bool firebasePut(const String &path, const String &jsonBody) {
        if (!firebaseApp.ready()) return false;
        bool ok = Database.set<object_t>(firebaseClient, path, object_t(jsonBody));
        if (!ok) {
            DBGF("[EdgeSmartConfig] Firebase PUT %s failed: %s\n",
                          path.c_str(), firebaseClient.lastError().message().c_str());
        }
        return ok;
    }
}  // namespace

// ---------------------------------------------------------------
// Part C — dynamic sensor/actuator config (no reflash to add/remove)
// ---------------------------------------------------------------
namespace {
    struct SensorDef {
        String id;
        uint8_t pin;
        String type;  // "analog" | "digital" | anything else -> dummy value
    };
    struct ActuatorDef {
        String id;
        uint8_t pin;
    };

    std::vector<SensorDef> sensors;
    std::vector<ActuatorDef> actuators;
    uint32_t configIntervalMs = SENSOR_PUBLISH_INTERVAL_DEFAULT_MS;

    String configPath() { return "/devices/" + AccountPairing::getUuid() + "/config"; }
    String telemetryPath() { return "/devices/" + AccountPairing::getUuid() + "/telemetry"; }
    String statePath() { return "/devices/" + AccountPairing::getUuid() + "/state"; }

    // Expected Firebase config shape:
    // { "intervalMs": 10000,
    //   "sensors":   [ {"id":"s1","pin":34,"type":"analog"}, ... ],
    //   "actuators": [ {"id":"a1","pin":26}, ... ] }
    void pullConfig() {
        String body = firebaseGet(configPath());
        if (body.length() == 0 || body == "null") {
            DBGLN("[EdgeSmartConfig] No config at Firebase path yet (or not authenticated).");
            return;
        }

        JsonDocument doc;
        if (deserializeJson(doc, body)) {
            DBGLN("[EdgeSmartConfig] Config JSON parse failed.");
            return;
        }

        configIntervalMs = doc["intervalMs"] | SENSOR_PUBLISH_INTERVAL_DEFAULT_MS;

        sensors.clear();
        if (doc["sensors"].is<JsonArray>()) {
            for (JsonObject s : doc["sensors"].as<JsonArray>()) {
                SensorDef def;
                def.id   = s["id"]   | "";
                def.pin  = s["pin"]  | 0;
                def.type = s["type"] | "dummy";
                if (def.id.length() == 0) continue;
                if (def.type == "analog" || def.type == "digital") pinMode(def.pin, INPUT);
                sensors.push_back(def);
            }
        }

        actuators.clear();
        if (doc["actuators"].is<JsonArray>()) {
            for (JsonObject a : doc["actuators"].as<JsonArray>()) {
                ActuatorDef def;
                def.id  = a["id"]  | "";
                def.pin = a["pin"] | 0;
                if (def.id.length() == 0) continue;
                pinMode(def.pin, OUTPUT);
                digitalWrite(def.pin, LOW);
                actuators.push_back(def);
            }
        }

        DBGF("[EdgeSmartConfig] Config applied: %u sensor(s), %u actuator(s), interval=%ums\n",
                       (unsigned)sensors.size(), (unsigned)actuators.size(), (unsigned)configIntervalMs);
    }
}  // namespace

// ---------------------------------------------------------------
// Part D — telemetry + state push
// ---------------------------------------------------------------
namespace {
    uint32_t lastTelemetryAt = 0;
    uint32_t lastConfigPullAt = 0;

    float readSensor(const SensorDef &s) {
        if (s.type == "analog") return analogRead(s.pin);
        if (s.type == "digital") return digitalRead(s.pin);
        return random(0, 10000) / 100.0;  // dummy fallback for an unrecognized type
    }

    void pushTelemetry() {
        if (sensors.empty()) return;

        JsonDocument doc;
        for (auto &s : sensors) {
            doc[s.id] = readSensor(s);
        }
        String out;
        serializeJson(doc, out);
        firebasePut(telemetryPath(), out);
    }

    void pushActuatorState() {
        if (actuators.empty()) return;

        JsonDocument doc;
        for (auto &a : actuators) {
            doc[a.id] = (digitalRead(a.pin) == HIGH);
        }
        String out;
        serializeJson(doc, out);
        firebasePut(statePath(), out);
    }
}  // namespace

// ---------------------------------------------------------------
// Part E — public API
// ---------------------------------------------------------------
void begin() {
    wifiReady = runSmartConfig();
    if (!wifiReady) return;

    initFirebase();

    // FirebaseApp::loop() drives the auth handshake; block briefly
    // here (bounded) so pullConfig() below has a real chance of
    // succeeding on first boot instead of racing authentication.
    DBGLN("[EdgeSmartConfig] Waiting for Firebase auth...");
    uint32_t start = millis();
    while (!firebaseApp.ready() && millis() - start < 15000) {
        firebaseApp.loop();
        delay(100);
    }
    if (!firebaseApp.ready()) {
        DBGLN("[EdgeSmartConfig] Firebase auth not ready yet — will keep retrying in loop().");
    }

    pullConfig();
    pushActuatorState();  // initial state report — all actuators start LOW
    lastTelemetryAt = millis();
    lastConfigPullAt = millis();
}

void loop() {
    if (!wifiReady) return;

    firebaseApp.loop();  // required — drives auth/token-refresh regardless of mode

    if (WiFi.status() != WL_CONNECTED) {
        DBGLN("[EdgeSmartConfig] WiFi dropped — this variant doesn't auto-reconnect; power-cycle or extend this if needed.");
        return;
    }

    if (!firebaseApp.ready()) return;  // keep waiting; next loop() will check again

    if (millis() - lastConfigPullAt >= FIREBASE_CONFIG_POLL_MS) {
        lastConfigPullAt = millis();
        pullConfig();  // picks up any add/remove sensor/actuator edits made in Firebase, no reflash
    }

    if (millis() - lastTelemetryAt >= configIntervalMs) {
        lastTelemetryAt = millis();
        pushTelemetry();
    }
}

}  // namespace EdgeSmartConfig

#else  // !USE_EDGE_SMARTCONFIG_VARIANT

// Stub build: FirebaseClient and everything above are excluded
// entirely. main.cpp's own #if USE_EDGE_SMARTCONFIG_VARIANT guard
// means these never actually get called in that configuration, but
// they're defined here too so this translation unit always satisfies
// edge_smartconfig.h's declarations, regardless of what main.cpp
// looks like.
namespace EdgeSmartConfig {
    void begin() {}
    void loop() {}
}  // namespace EdgeSmartConfig

#endif  // USE_EDGE_SMARTCONFIG_VARIANT
