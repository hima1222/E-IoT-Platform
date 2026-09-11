#pragma once
/*
 * logger.h
 * --------
 * Just the DEBUG_ENABLE toggle + DBG/DBGLN/DBGF/DBG_INIT macros,
 * kept separate from config.h so config.h only holds settings, not
 * logging plumbing. No file I/O here — saving output to a .log file
 * is handled entirely by PlatformIO's monitor_filters = log2file in
 * platformio.ini, not by firmware code (see that file's comment).
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
