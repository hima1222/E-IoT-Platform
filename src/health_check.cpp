//HEALTH CHECK MONITOR

#include "health_check.h"
#include "config.h"

namespace HealthCheck {

namespace {
    constexpr uint8_t MAX_TRACKED_TASKS = 4;

    struct TrackedTask {
        const char *name;
        TaskHandle_t handle;
    };

    TrackedTask tasks[MAX_TRACKED_TASKS];
    uint8_t trackedCount = 0;

    uint32_t lastCheckAt = 0;

    // One-line heap summary.
    void logHeap() {
        DBGF("[Health] heap: free=%u  min_ever=%u  largest_block=%u\n",
             (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
    }

    // Stack high-water mark for one task, with an early-warning threshold.
    void logTask(const char *name, TaskHandle_t handle) {
        if (!handle) return;  // not created yet — skip quietly, try again next interval
        UBaseType_t w = uxTaskGetStackHighWaterMark(handle);
        DBGF("[Health] %s free stack: %u words\n", name, (unsigned)w);
        if (w < STACK_WARN_WORDS) {
            DBGF("[Health] WARNING: %s stack running low! (%u words free, warn threshold %u)\n",
                 name, (unsigned)w, (unsigned)STACK_WARN_WORDS);
        }
    }
}  // namespace

void begin() {
    lastCheckAt = millis();
    // The main Arduino loop task is always tracked, under its real
    // FreeRTOS name — no explicit trackTask() call needed for it.
}

void trackTask(const char *name, TaskHandle_t handle) {
    if (trackedCount >= MAX_TRACKED_TASKS) {
        DBGLN("[Health] trackTask: MAX_TRACKED_TASKS reached, dropping registration.");
        return;
    }
    tasks[trackedCount].name = name;
    tasks[trackedCount].handle = handle;
    trackedCount++;
}

void loop() {
    if (millis() - lastCheckAt < HEALTH_CHECK_INTERVAL_MS) return;
    lastCheckAt = millis();

    logHeap();

    // Main loop task — call from within loop() itself, so
    // xTaskGetCurrentTaskHandle() correctly resolves to it.
    logTask("MainLoop", xTaskGetCurrentTaskHandle());

    for (uint8_t i = 0; i < trackedCount; i++) {
        logTask(tasks[i].name, tasks[i].handle);
    }
}

}  // namespace HealthCheck
