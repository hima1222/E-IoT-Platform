#pragma once

#define DEBUG_ENABLE 1

#if DEBUG_ENABLE
  #define DBG_INIT(baud)  Serial.begin(baud)
  #define DBG(...)        Serial.print(__VA_ARGS__)
  #define DBGLN(...)      Serial.println(__VA_ARGS__)
  #define DBGF(...)       Serial.printf(__VA_ARGS__)
#else
  #define DBG_INIT(baud)
  #define DBG(...)
  #define DBGLN(...)
  #define DBGF(...)
#endif

#define ENABLE_BLE_PAIRING          1   // 0 = drop the whole ESP32 BLE Arduino library from the build
#define USE_EDGE_SMARTCONFIG_VARIANT 0  // 1 = build the Firebase/SmartConfig variant instead of the primary path;
                                          // when 0, FirebaseClient + its bundled TLS stack are excluded entirely

#define AP_SSID        "espAP"
#define AP_PASSWORD    "esp32"   // replace with fixed provisioning password
#define AP_PORTAL_PORT 80

#define BLE_DEVICE_NAME_PREFIX "SD-"       // advertised name becomes "SD-<last 4 MAC bytes>"

#define LED_PIN_POWER   2   // red power LED
#define LED_PIN_STATUS  4   // blue status LED

#define MQTT_BROKER_HOST   "test.mosquitto.org"   // TODO: your broker
#define MQTT_BROKER_PORT   8883
#define MQTT_USE_TLS       1
#define SENSOR_PUBLISH_INTERVAL_DEFAULT_MS 10000
#define GPIO_ACTUATOR_PIN  26              

// ---- Section 4 / 5 — shared provisioning button ----
#define BUTTON_PIN                 0     // TODO: your actual GPIO
#define BUTTON_DEBOUNCE_MS         50
#define BUTTON_LONG_PRESS_MS       3000   // held this long -> soft reset
#define BUTTON_VERY_LONG_PRESS_MS  8000   // held this long -> hard reset

// ---- Section 6 — MQTT ----
#define MQTT_BROKER_PROTOCOL "3.1.1"
// NOTE: must be a #define (macro), not `static const char*` — mqtt_manager.cpp
// checks it with #if defined(...), which only works on macros.
#define MQTT_CA_CERT \
"-----BEGIN CERTIFICATE-----\n" \
"REPLACE_WITH_YOUR_BROKER_CA_CERT\n" \
"-----END CERTIFICATE-----\n"

// ---- Section 10 — FOTA ----
#define HW_ID                   "esp32-devkit-v1"
#define FOTA_BASE_URL           "https://your-backend.example.com"   // TODO
#define FOTA_METADATA_PATH      "/api/firmware/latest"
#define FOTA_CHECK_INTERVAL_MS  (3UL * 60UL * 60UL * 1000UL)   // periodic check, every 3h
#define FOTA_HTTP_TIMEOUT_MS    15000
#define FOTA_TASK_STACK_WORDS   8192
#define FOTA_TASK_PRIORITY      1
#define FOTA_TASK_CORE          1

// ---- Section 11 — Edge/SmartConfig Variant (Firebase) ----
#define FIREBASE_API_KEY       "your-web-api-key"    // Firebase Console → Project Settings → Web API Key
#define FIREBASE_USER_EMAIL    "devices@yourapp.example"   // TODO — a dedicated Firebase Auth user for this device fleet
#define FIREBASE_USER_PASSWORD "..."                         // TODO
#define FIREBASE_DATABASE_URL  "https://your-project-id-default-rtdb.firebaseio.com/"

#define SMARTCONFIG_TIMEOUT_MS           60000
#define WIFI_CONNECT_TIMEOUT_MS_FALLBACK 15000
#define FIREBASE_CONFIG_POLL_MS          (5UL * 60UL * 1000UL)

// ---- Health Check Monitor ----
#define HEALTH_CHECK_INTERVAL_MS   (60UL * 1000UL)   // how often to log heap/stack stats
#define STACK_WARN_WORDS           256                // warn if a task's free stack drops below this (~1KB on ESP32)
