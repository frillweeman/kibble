#pragma once
#include "types.h"
#include <Arduino.h>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// Storage — LittleFS-backed persistence for schedule and feed log.
//
// Call initStorage() once in setup() before any other function here.
// ─────────────────────────────────────────────────────────────────────────────

// Mount LittleFS (formats on first use). Returns false on hard failure.
bool initStorage();

// Schedule ────────────────────────────────────────────────────────────────────
std::vector<ScheduleEntry> loadSchedule();
bool saveSchedule(const std::vector<ScheduleEntry>& schedule);
// Returns the unix epoch when the schedule was last saved, or 0 if unknown.
time_t getScheduleCreatedAt();

// Feed log ────────────────────────────────────────────────────────────────────
// error:    leave empty for a successful feed; pass a description on failure.
// retryFor: if non-zero, records the scheduled time that this feed is retrying.
bool appendFeedLog(time_t timestamp, int clicks, const String& error = "", time_t retryFor = 0);

// Returns the raw JSON string of the log (for direct HTTP response).
String loadFeedLogJson();

// Returns the timestamp of the most-recent log entry, or 0 if log is empty.
time_t lastFeedTime();

// Clears the feed log (writes an empty array).
bool clearFeedLog();
