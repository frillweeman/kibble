#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostics log — timestamped lines kept in RAM, flushed to LittleFS every
// DEBUG_LOG_FLUSH_MS, rotated (current -> old) at DEBUG_LOG_MAX_BYTES, and
// mirrored to Serial.  Safe to call from any task.  Readable via /api/debuglog.
// ─────────────────────────────────────────────────────────────────────────────

// Call in setup() after initStorage().  Increments and logs the boot counter.
void initDebugLog();

// printf-style.  tag is a short subsystem name, e.g. "WIFI".
void debugLog(const char* tag, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Call every loop(): flushes buffered lines to flash when due.
void debugLogTick();

// Force a flush now (e.g. end of setup).
void debugLogFlush();

// Last `maxLines` lines of the current (or previous, if old) log file, plus any
// not-yet-flushed lines for the current log.
String debugLogTail(int maxLines, bool old);

bool clearDebugLog();

unsigned long bootCount();
