#include "storage.h"
#include "config.h"
#include "timekeeping.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

// ─── Helpers ─────────────────────────────────────────────────────────────────

static void ensureFile(const char* path, const char* defaultContent) {
    if (!LittleFS.exists(path)) {
        File f = LittleFS.open(path, "w");
        if (f) {
            f.print(defaultContent);
            f.close();
            Serial.printf("[Storage] Created %s with defaults\n", path);
        } else {
            Serial.printf("[Storage] ERROR: could not create %s\n", path);
        }
    }
}

// ─── Public API ──────────────────────────────────────────────────────────────

bool initStorage() {
    // "spiffs" is the partition label in most ESP32 Arduino partition tables
    // (even when the FS type is LittleFS). Try it first, then fall back to
    // the default label so we work across different board partition layouts.
    bool mounted = LittleFS.begin(true, "/littlefs", 10, "spiffs");
    if (!mounted) {
        Serial.println("[Storage] 'spiffs' label failed, trying default label...");
        mounted = LittleFS.begin(true, "/littlefs", 10, "ffat");
    }
    if (!mounted) {
        mounted = LittleFS.begin(true); // last resort: let the driver pick
    }
    if (!mounted) {
        Serial.println("[Storage] LittleFS mount FAILED — all labels tried");
        return false;
    }
    Serial.println("[Storage] LittleFS mounted");

    ensureFile(SCHEDULE_FILE, "{\"createdAt\":0,\"entries\":[]}");
    ensureFile(LOG_FILE,      "[]");
    return true;
}

// ─── Schedule ─────────────────────────────────────────────────────────────────

std::vector<ScheduleEntry> loadSchedule() {
    std::vector<ScheduleEntry> entries;

    File f = LittleFS.open(SCHEDULE_FILE, "r");
    if (!f) {
        Serial.println("[Storage] Could not open schedule file");
        return entries;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[Storage] Schedule parse error: %s\n", err.c_str());
        return entries;
    }

    for (JsonObject obj : doc["entries"].as<JsonArray>()) {
        ScheduleEntry e;
        e.hour   = obj["hour"]   | 0;
        e.minute = obj["minute"] | 0;
        e.clicks = obj["clicks"] | 1;
        entries.push_back(e);
    }

    Serial.printf("[Storage] Loaded %d schedule entries\n", entries.size());
    return entries;
}

time_t getScheduleCreatedAt() {
    File f = LittleFS.open(SCHEDULE_FILE, "r");
    if (!f) return 0;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return 0;
    return (time_t)(doc["createdAt"] | 0);
}

bool saveSchedule(const std::vector<ScheduleEntry>& schedule) {
    File f = LittleFS.open(SCHEDULE_FILE, "w");
    if (!f) {
        Serial.println("[Storage] Could not write schedule file");
        return false;
    }

    JsonDocument doc;
    JsonObject root    = doc.to<JsonObject>();
    root["createdAt"]  = (long long)time(nullptr);
    JsonArray arr      = root["entries"].to<JsonArray>();
    for (const auto& e : schedule) {
        JsonObject obj = arr.add<JsonObject>();
        obj["hour"]   = e.hour;
        obj["minute"] = e.minute;
        obj["clicks"] = e.clicks;
    }

    serializeJson(doc, f);
    f.close();
    Serial.printf("[Storage] Saved %d schedule entries (createdAt=%lld)\n",
                  schedule.size(), (long long)time(nullptr));
    return true;
}

// ─── Feed log ─────────────────────────────────────────────────────────────────

bool appendFeedLog(time_t timestamp, int clicks, const String& error, time_t retryFor) {
    // Load existing log
    File rf = LittleFS.open(LOG_FILE, "r");
    JsonDocument doc;
    if (rf) {
        deserializeJson(doc, rf);
        rf.close();
    }

    JsonArray arr = doc.is<JsonArray>() ? doc.as<JsonArray>() : doc.to<JsonArray>();

    // Append new entry
    JsonObject obj = arr.add<JsonObject>();
    obj["ts"]     = (long long)timestamp;
    obj["clicks"] = clicks;
    if (error.length() > 0) {
        obj["error"] = error;
    }
    if (retryFor != 0) {
        obj["retryFor"] = (long long)retryFor;
    }
    // Timestamps from a flash-restored clock are behind real time until NTP syncs.
    if (clockSource() != CLOCK_NTP) {
        obj["clock"] = "approx";
    }

    // Trim to MAX_LOG_ENTRIES (keep the most recent)
    while ((int)arr.size() > MAX_LOG_ENTRIES) {
        arr.remove(0);
    }

    File wf = LittleFS.open(LOG_FILE, "w");
    if (!wf) {
        Serial.println("[Storage] Could not write log file");
        return false;
    }
    serializeJson(doc, wf);
    wf.close();

    if (error.length() > 0) {
        Serial.printf("[Storage] Feed logged (FAILED): ts=%lld clicks=%d error=%s\n",
                      (long long)timestamp, clicks, error.c_str());
    } else {
        Serial.printf("[Storage] Feed logged: ts=%lld clicks=%d\n", (long long)timestamp, clicks);
    }
    return true;
}

bool clearFeedLog() {
    File f = LittleFS.open(LOG_FILE, "w");
    if (!f) {
        Serial.println("[Storage] Could not clear log file");
        return false;
    }
    f.print("[]");
    f.close();
    Serial.println("[Storage] Feed log cleared");
    return true;
}

String loadFeedLogJson() {
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return "[]";
    String json = f.readString();
    f.close();
    return json;
}

time_t lastFeedTime() {
    File f = LittleFS.open(LOG_FILE, "r");
    if (!f) return 0;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || !doc.is<JsonArray>()) return 0;

    JsonArray arr = doc.as<JsonArray>();
    if (arr.size() == 0) return 0;

    return (time_t)(arr[arr.size() - 1]["ts"] | 0);
}
