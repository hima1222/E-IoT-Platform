#pragma once

#include <Arduino.h>

namespace HealthCheck {

// Call once from setup(), after the tasks you want tracked have been
// created (e.g. after Fota::begin(), since that's what creates the
// FOTA task). Safe to call even if no extra tasks exist yet — the
// main loop task is always tracked automatically.
void begin();

// Call every loop() iteration. Logs at most once per
// HEALTH_CHECK_INTERVAL_MS (see config.h); cheap to call more often.
void loop();

// Registers an additional FreeRTOS task to monitor by name + handle
// (beyond the main loop task, which is tracked automatically).
// Handles that are nullptr at registration time (e.g. a task created
// later) are skipped silently — call this again once the task
// actually exists, or check back via loop() after creation.
void trackTask(const char *name, TaskHandle_t handle);

}  // namespace HealthCheck
