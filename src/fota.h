#pragma once
 
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Update.h>
#include <Preferences.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <mbedtls/sha256.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
 
#include "config.h"
#include "account_pairing.h"
#include "mqtt_manager.h"
 
namespace Fota {
 
// ---------------------- Public API ----------------------
 
// Call once from setup(), as early as possible (before other modules
// finish initializing) — logs boot/partition info, handles double-
// reset manual rollback, runs the pending-verify self-test/confirm,
// and creates the dedicated OTA task. May call esp_restart()
// internally (DRD rollback, or self-test failure).
inline void begin();
 
// Call from Ownership Flow right after MQTT connects — subscribes to
// the update-notice topics and reports the current firmware version
// as an attribute.
inline void onMqttConnected();
 
// Call from Ownership Flow's MQTT message dispatcher when the topic
// matches getUpdateTopicAll() or getUpdateTopicDevice(). Just flags
// an update check as pending; the OTA task picks it up.
inline void handleUpdateNotice(const String &payload);
 
inline String getCurrentVersion();
inline String getUpdateTopicAll();      // "devices/all/update" — broadcast to the whole fleet
inline String getUpdateTopicDevice();    // "devices/<uuid>/update" — this device only

// FreeRTOS task handle for the FOTA task, exposed so Section
// health-check monitoring can read its stack high-water mark.
// Returns nullptr before begin() has run.
inline TaskHandle_t getTaskHandle();
 
// ---------------------- Internal ----------------------
 
namespace detail {
 
inline TaskHandle_t taskHandle = nullptr;
inline volatile bool updatePending = false;
inline String attributesTopic;  // "devices/<uuid>/attributes/firmware"
 
// ---- version storage (NVS) ----
// Reads the stored firmware version from NVS, seeding "0.0.0" on first boot.
inline String getCurrentVersionImpl() {
    Preferences prefs;
    prefs.begin("fota", false);
    String v;
    if (prefs.isKey("fw_ver")) {
        v = prefs.getString("fw_ver", "0.0.0");
    } else {
        v = "0.0.0";
        prefs.putString("fw_ver", v);  // seed so future reads never miss
    }
    prefs.end();
    return v;
}
 
// Persists the version string after a successful update.
inline void setCurrentVersion(const String &v) {
    Preferences prefs;
    prefs.begin("fota", false);
    prefs.putString("fw_ver", v);
    prefs.end();
}
 
// Formats a raw SHA-256 digest as lowercase hex for comparison against metadata.
inline String sha256ToHex(const uint8_t hash[32]) {
    static const char *hex = "0123456789abcdef";
    String out; out.reserve(64);
    for (int i = 0; i < 32; i++) { out += hex[(hash[i] >> 4) & 0xF]; out += hex[hash[i] & 0xF]; }
    return out;
}
 
struct FirmwareMeta {
    String version, sha256, url, releaseNotes;
    bool hwCompatible = false, valid = false;
};
 
// Fetches + validates the update-metadata JSON (version/sha256/url/hw list).
inline FirmwareMeta fetchMetadata() {
    FirmwareMeta meta;
    WiFiClientSecure client;
    client.setInsecure();  // TODO: pin your real CA before fleet rollout — see mqtt_manager.h's TLS note for the same caveat
 
    HTTPClient http;
    String url = String(FOTA_BASE_URL) + FOTA_METADATA_PATH
                 + "?hw=" + HW_ID + "&deviceId=" + AccountPairing::getUuid();
    if (!http.begin(client, url)) { DBGLN("[FOTA] metadata http.begin() failed"); return meta; }
    http.setTimeout(FOTA_HTTP_TIMEOUT_MS);
 
    int code = http.GET();
    if (code != HTTP_CODE_OK) { DBGF("[FOTA] metadata fetch HTTP %d\n", code); http.end(); return meta; }
 
    String payload = http.getString();
    http.end();
 
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) { DBGF("[FOTA] metadata JSON parse failed: %s\n", err.c_str()); return meta; }
 
    meta.version = doc["version"] | "";
    meta.sha256 = doc["sha256"] | "";
    meta.url = doc["url"] | "";
    meta.releaseNotes = doc["release_notes"] | "";
 
    if (meta.url.startsWith("http://")) meta.url = "https://" + meta.url.substring(7);
 
    if (doc["hw_compatibility"].is<JsonArray>()) {
        for (JsonVariant v : doc["hw_compatibility"].as<JsonArray>()) {
            if (String(v.as<const char *>()) == HW_ID) { meta.hwCompatible = true; break; }
        }
    }
    meta.valid = meta.version.length() > 0 && meta.sha256.length() == 64 && meta.url.length() > 0;
    return meta;
}
 
// Streams the binary into the inactive OTA partition while hashing it,
// then refuses to boot it if the hash doesn't match.
inline bool downloadFlashAndVerify(const FirmwareMeta &meta) {
    WiFiClientSecure client;
    client.setInsecure();
 
    HTTPClient http;
    if (!http.begin(client, meta.url)) { DBGLN("[FOTA] firmware http.begin() failed"); return false; }
    http.setTimeout(FOTA_HTTP_TIMEOUT_MS);
 
    int code = http.GET();
    if (code != HTTP_CODE_OK) { DBGF("[FOTA] firmware fetch HTTP %d\n", code); http.end(); return false; }
 
    int contentLength = http.getSize();
    if (contentLength <= 0) { DBGLN("[FOTA] invalid content length"); http.end(); return false; }
 
    if (!Update.begin(contentLength, U_FLASH)) {  // always targets the INACTIVE OTA partition
        DBGF("[FOTA] Update.begin failed: %s\n", Update.errorString());
        http.end();
        return false;
    }
 
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    mbedtls_sha256_starts(&sha, 0);
 
    WiFiClient *stream = http.getStreamPtr();
    uint8_t buf[1024];
    int written = 0;
    uint32_t lastData = millis();
 
    while (http.connected() && written < contentLength) {
        size_t avail = stream->available();
        if (avail == 0) {
            if (millis() - lastData > FOTA_HTTP_TIMEOUT_MS) {
                DBGLN("[FOTA] stream stalled");
                Update.abort(); mbedtls_sha256_free(&sha); http.end();
                return false;
            }
            delay(5);
            continue;
        }
        int n = stream->readBytes(buf, min(avail, sizeof(buf)));
        if (n <= 0) continue;
        lastData = millis();
        mbedtls_sha256_update(&sha, buf, n);
        if (Update.write(buf, n) != (size_t)n) {
            DBGF("[FOTA] flash write failed: %s\n", Update.errorString());
            Update.abort(); mbedtls_sha256_free(&sha); http.end();
            return false;
        }
        written += n;
    }
    http.end();
 
    if (written != contentLength) {
        DBGLN("[FOTA] incomplete download");
        Update.abort(); mbedtls_sha256_free(&sha);
        return false;
    }
 
    uint8_t hash[32];
    mbedtls_sha256_finish(&sha, hash);
    mbedtls_sha256_free(&sha);
    String computed = sha256ToHex(hash);
    if (!computed.equalsIgnoreCase(meta.sha256)) {
        DBGLN("[FOTA] SHA-256 MISMATCH — refusing to boot this image");
        Update.abort();
        return false;
    }
    if (!Update.end(true)) {
        DBGF("[FOTA] Update.end failed: %s\n", Update.errorString());
        return false;
    }
 
    DBGLN("[FOTA] verified + flashed successfully");
    return true;
}
 
// Full update cycle: fetch metadata, compare versions, download+flash, reboot.
inline void checkAndUpdate() {
    if (WiFi.status() != WL_CONNECTED) { DBGLN("[FOTA] no WiFi, skipping check"); return; }
 
    DBGLN("[FOTA] checking for update...");
    FirmwareMeta meta = fetchMetadata();
    if (!meta.valid) return;
 
    String current = getCurrentVersionImpl();
    DBGF("[FOTA] current=%s available=%s\n", current.c_str(), meta.version.c_str());
 
    if (meta.version == current) { DBGLN("[FOTA] already up to date"); return; }
    if (!meta.hwCompatible) { DBGF("[FOTA] v%s not compatible with %s\n", meta.version.c_str(), HW_ID); return; }
 
    if (downloadFlashAndVerify(meta)) {
        setCurrentVersion(meta.version);
        delay(200);
        esp_restart();  // boots into the new image; self-test/confirm runs again on next begin()
    }
}
 
// ---- rollback safety: pending-verify self-test/confirm ----
// Runs once per boot on a pending-verify image: confirm valid or roll back.
inline void runSelfTestAndConfirm() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (esp_ota_get_state_partition(running, &state) != ESP_OK) return;
    if (state != ESP_OTA_IMG_PENDING_VERIFY) return;  // already confirmed, or first-ever flash
 
    DBGLN("[FOTA] new image pending verification — running self-test...");
 
    // Self-test heuristic: does WiFi come up within a bounded window?
    // This module never calls WiFi.begin() itself (that's Section
    // 1/3's job) — it just observes whatever those modules are doing
    // concurrently on boot. If WiFi never comes up, treat the new
    // image as broken and roll back.
    const uint32_t SELF_TEST_TIMEOUT_MS = 20000;
    uint32_t start = millis();
    bool selfTestPassed = false;
    while (millis() - start < SELF_TEST_TIMEOUT_MS) {
        if (WiFi.status() == WL_CONNECTED) { selfTestPassed = true; break; }
        delay(200);
    }
 
    if (selfTestPassed) {
        esp_ota_mark_app_valid_cancel_rollback();
        DBGLN("[FOTA] self-test passed, image confirmed valid");
    } else {
        DBGLN("[FOTA] self-test FAILED (no WiFi) — rolling back");
        esp_ota_mark_app_invalid_rollback_and_reboot();
    }
}
 
// ---- double-reset detection (DRD): press physical reset twice
// quickly to force a manual rollback regardless of self-test state ----
constexpr uint32_t DRD_MAGIC = 0xD12DE5E7;
constexpr uint32_t DRD_WINDOW_MS = 3000;
 
}  // namespace detail
}  // namespace Fota
 
inline RTC_NOINIT_ATTR uint32_t g_fotaDrdFlag;  // survives reset, not power-loss — inline so it merges across translation units (fota.h is included by both main.cpp and ownership_flow.cpp)
 
namespace Fota {
namespace detail {
 
inline esp_timer_handle_t drdDisarmTimer = nullptr;
 
inline void drdDisarmCallback(void *) { g_fotaDrdFlag = 0; }
 
// Detects a double physical reset within the DRD window; arms the flag otherwise.
inline bool drdCheckAndArm() {
    bool doubleReset = (g_fotaDrdFlag == DRD_MAGIC);
    if (doubleReset) {
        DBGLN("[FOTA][DRD] Double reset detected -> manual recovery mode");
        g_fotaDrdFlag = 0;
        return true;
    }
    g_fotaDrdFlag = DRD_MAGIC;
    esp_timer_create_args_t args = {};
    args.callback = &drdDisarmCallback;
    args.name = "fota_drd_disarm";
    if (esp_timer_create(&args, &drdDisarmTimer) == ESP_OK) {
        esp_timer_start_once(drdDisarmTimer, (uint64_t)DRD_WINDOW_MS * 1000ULL);
    }
    return false;
}
 
// Forces boot into the other OTA partition, regardless of its verify state.
inline void manualRollback() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *target = esp_ota_get_next_update_partition(nullptr);
    if (!target || target == running) { DBGLN("[FOTA] no alternate partition to roll back to"); return; }
    DBGF("[FOTA] DRD manual rollback -> %s\n", target->label);
    if (esp_ota_set_boot_partition(target) == ESP_OK) { delay(200); esp_restart(); }
}
 
// One-line partition/version summary, printed at every boot.
inline void logBootInfo() {
    const esp_partition_t *running = esp_ota_get_running_partition();
    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_ota_get_state_partition(running, &state);
    const char *s = (state == ESP_OTA_IMG_VALID) ? "valid" :
                     (state == ESP_OTA_IMG_PENDING_VERIFY) ? "pending-verify" :
                     (state == ESP_OTA_IMG_INVALID) ? "invalid" : "unknown";
    DBGF("[FOTA][BOOT] partition: %s @ 0x%06x, state: %s, stored version: %s\n",
                  running->label, (unsigned)running->address, s, getCurrentVersionImpl().c_str());
}
 
// ---- the dedicated task (checklist: "run in separate task/thread") ----
// The dedicated FOTA task: periodic check + processes any pending trigger.
inline void taskFn(void *) {
    DBGF("[FOTA][Core %d] task started\n", xPortGetCoreID());
 
    uint32_t lastIntervalCheck = 0;
    for (;;) {
        if (millis() - lastIntervalCheck >= FOTA_CHECK_INTERVAL_MS) {
            lastIntervalCheck = millis();
            updatePending = true;
        }
        if (updatePending) {
            updatePending = false;
            checkAndUpdate();
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
 
}  // namespace detail
 
inline String getCurrentVersion() {
    return detail::getCurrentVersionImpl();
}
 
inline String getUpdateTopicAll() {
    return "devices/all/update";
}
 
inline String getUpdateTopicDevice() {
    return "devices/" + AccountPairing::getUuid() + "/update";
}

inline TaskHandle_t getTaskHandle() {
    return detail::taskHandle;
}
 
inline void handleUpdateNotice(const String & /*payload*/) {
    DBGLN("[FOTA] update notice received via MQTT");
    detail::updatePending = true;
}
 
inline void onMqttConnected() {
    detail::attributesTopic = "devices/" + AccountPairing::getUuid() + "/attributes/firmware";
    MqttManager::subscribe(getUpdateTopicAll(), 1);
    MqttManager::subscribe(getUpdateTopicDevice(), 1);
    // "Report firmware version as attribute" — retained so the
    // backend/app always sees the last-known version even if they
    // connect after this message was sent.
    MqttManager::publish(detail::attributesTopic, getCurrentVersion(), /*retained=*/true, /*qos=*/1);
}
 
inline void begin() {
    detail::logBootInfo();
 
    if (detail::drdCheckAndArm()) {
        detail::manualRollback();  // restarts internally if a target partition exists; otherwise falls through
    }
    detail::runSelfTestAndConfirm();  // may call esp_ota_mark_app_invalid_rollback_and_reboot() internally
 
    xTaskCreatePinnedToCore(
        detail::taskFn, "TaskFota", FOTA_TASK_STACK_WORDS, nullptr,
        FOTA_TASK_PRIORITY, &detail::taskHandle, FOTA_TASK_CORE);
}
 
}  // namespace Fota