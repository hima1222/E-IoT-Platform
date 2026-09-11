#pragma once
/*
 * logger.h
 * --------
 * All Serial-based debug logging lives here, separate from config.h.
 * Every module includes this (directly or via config.h) and logs
 * through these macros instead of calling Serial.* directly, so
 * DEBUG_ENABLE is the one switch that turns logging on/off everywhere.
 *
 * DEBUG_ENABLE = 1 -> macros call Serial as normal.
 * DEBUG_ENABLE = 0 -> macros compile to nothing (zero cost, not just silent).
 */

#define DEBUG_ENABLE 1

#if DEBUG_ENABLE
  #define DBG_INIT(baud)  Serial.begin(baud)   // call once in setup()
  #define DBG(...)        Serial.print(__VA_ARGS__)
  #define DBGLN(...)      Serial.println(__VA_ARGS__)
  #define DBGF(...)       Serial.printf(__VA_ARGS__)   // printf-style, e.g. DBGF("x=%d\n", x)
#else
  #define DBG_INIT(baud)
  #define DBG(...)
  #define DBGLN(...)
  #define DBGF(...)
#endif
