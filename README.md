# E-IoT Platform — ESP32 Firmware

One module per numbered section of `esp32_firmware_checklist.docx`.
Each section is a `src/<name>.h` + `src/<name>.cpp` pair (or, for
Section 10, a single header-only file) with one public API;
`main.cpp` only calls into that API and never touches a section's
internals. Section 9 (Offline Buffering) is descoped by product
decision and not implemented.

## Module map

| Section | Files | Covers |
|---|---|---|
| 1 — Account & Pairing | `account_pairing.h/.cpp` | Device identity (MAC/UUID), WiFi AP pairing (JSON REST API), BLE alt-pairing |
| 2 — LED States | `led_states.h/.cpp` | Power/status LED patterns for every connection state |
| 3 — Ownership Flow | `ownership_flow.h/.cpp` | Flash-persisted WiFi + user config, MQTT birth/LWT/registration, dispatches to 7/8/10 |
| 4 — On-Demand Portal | `on_demand_portal.h/.cpp` | Button press classification (short/long/very-long) |
| 5 — Reset | `reset.h/.cpp` | Soft reset (WiFi creds only) / hard reset (all user config) |
| 6 — MQTT | `mqtt_manager.h/.cpp` | Generic MQTT engine (QoS 0/1/2, TLS, Will) other sections build on |
| 7 — Sensor Data | `sensor_data.h/.cpp` | Dummy-stage telemetry: 5 fixed-key values, published on interval |
| 8 — Actuator Control | `actuator_control.h/.cpp` | GPIO control via MQTT command topic + ack |
| 10 — FOTA | `fota.h` (single file) | Task-based OTA: partitioning, SHA-256 verify, rollback safety, version reporting |
| 11 — Edge/SmartConfig Variant | `edge_smartconfig.h/.cpp` | Alternate path: SmartConfig pairing + Firebase RTDB instead of AP-portal + MQTT |
| 12 — Backend Alignment | `docs/BACKEND_CONTRACT.md` (if present) | Topic/payload contract reference, not firmware code |

Plus two cross-cutting pieces that aren't numbered sections:

- **`config.h`** — every constant, credential placeholder, and
  feature toggle in one place. Includes the `DEBUG_ENABLE` / `DBG*`
  macro system (see below) and two compile-time size toggles:
  `ENABLE_BLE_PAIRING` and `USE_EDGE_SMARTCONFIG_VARIANT`.
- **`health_check.h/.cpp`** — periodic heap and per-task stack
  logging. Tracks the main loop task automatically; other tasks
  (currently just Section 10's FOTA task) are registered via
  `HealthCheck::trackTask()`.

## Two build variants, one compile-time flag

`config.h`'s `USE_EDGE_SMARTCONFIG_VARIANT` picks between:
- **0 (default): the primary path** — Sections 1–8 + 10, AP-portal/BLE
  pairing, MQTT backend.
- **1: the Edge/SmartConfig variant** — Section 11 only, SmartConfig
  pairing, Firebase backend.

They're mutually exclusive at both runtime (`main.cpp`'s `#if`
branches) and compile time (`edge_smartconfig.cpp`'s FirebaseClient
dependency is entirely excluded from the binary when this is 0 — see
"Storage" below).

## Debug logging

Every `Serial.print*` call in the project goes through `config.h`'s
`DBG` / `DBGLN` / `DBGF` macros (and `Serial.begin()` through
`DBG_INIT`), gated by a single `DEBUG_ENABLE` flag. Set it to `0` for
a release build with zero logging overhead — the macros compile to
nothing, rather than just going silent at runtime.

## Storage

Two things keep this project's flash footprint under control on the
default 4MB ESP32:
- `platformio.ini` sets `board_build.partitions = min_spiffs.csv`
  (bigger OTA app partitions than the default table — this project
  doesn't use SPIFFS, config lives in NVS via `Preferences`).
- `ENABLE_BLE_PAIRING` and `USE_EDGE_SMARTCONFIG_VARIANT` in
  `config.h` aren't just runtime switches — set either to `0` and
  that feature's entire library (ESP32 BLE Arduino, or FirebaseClient
  + its bundled TLS stack) is excluded from the build, not just
  disabled at runtime.

## Health check monitor

Logs free heap, min-ever-free heap, largest allocatable block, and
stack high-water mark for the main loop task + any tracked task,
once every `HEALTH_CHECK_INTERVAL_MS` (`config.h`, default 60s).
Warns if a task's free stack drops below `STACK_WARN_WORDS`. Register
additional tasks to watch with `HealthCheck::trackTask(name, handle)`.

## Build

```
pio run
pio run -t upload
pio device monitor
```

Before building, fill in every `TODO`/placeholder value in `config.h`
— broker host, Firebase credentials, FOTA backend URL, CA certs, and
your actual GPIO pin assignments.
