#include "timekeeping.h"
#include "debuglog.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <sys/time.h>
#include "esp_sntp.h"

static volatile bool _ntpSynced = false;   // set from the SNTP callback (tcpip task)
static bool          _ntpHandled = false;
static ClockSource   _restored   = CLOCK_NONE;
static time_t        _lastSeen   = 0;      // last time() value observed before NTP sync
static unsigned long _lastSaveMs = 0;
static bool          _everSaved  = false;

static void onSntpSync(struct timeval*) { _ntpSynced = true; }

bool timeIsValid() {
    return time(nullptr) >= MIN_VALID_EPOCH;
}

bool getNow(struct tm* out) {
    time_t t = time(nullptr);
    if (t < MIN_VALID_EPOCH) return false;
    localtime_r(&t, out);
    return true;
}

ClockSource clockSource() {
    return _ntpSynced ? CLOCK_NTP : _restored;
}

const char* clockSourceName() {
    switch (clockSource()) {
        case CLOCK_NTP:   return "ntp";
        case CLOCK_FLASH: return "flash";
        default:          return "none";
    }
}

static void saveClock() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_EPOCH) return;
    File f = LittleFS.open(CLOCK_FILE, "w");
    if (!f) return;
    f.printf("{\"ts\":%lld}", (long long)now);
    f.close();
    _lastSaveMs = millis();
    _everSaved  = true;
}

static time_t loadSavedClock() {
    File f = LittleFS.open(CLOCK_FILE, "r");
    if (!f) return 0;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) return 0;
    long long ts = doc["ts"] | 0LL;
    // Reject garbage: before MIN_VALID_EPOCH or after year 2100.
    if (ts < MIN_VALID_EPOCH || ts > 4102444800LL) return 0;
    return (time_t)ts;
}

void initTimekeeping() {
    sntp_set_time_sync_notification_cb(onSntpSync);

    time_t saved   = loadSavedClock();
    time_t current = timeIsValid() ? time(nullptr) : 0;  // may survive a soft reset

    if (saved > current) {
        struct timeval tv = { saved, 0 };
        settimeofday(&tv, nullptr);
        _restored = CLOCK_FLASH;
        debugLog("CLOCK", "restored from flash: %lld (no NTP yet, time is approximate)",
                 (long long)saved);
    } else if (current > 0) {
        _restored = CLOCK_FLASH;
        debugLog("CLOCK", "system clock already valid at boot (soft reset); treating as approximate");
    } else {
        _restored = CLOCK_NONE;
        debugLog("CLOCK", "no saved time; waiting for NTP before the schedule can run");
    }
}

void timekeepingTick() {
    // NTP just completed its first sync since boot.
    if (_ntpSynced && !_ntpHandled) {
        _ntpHandled = true;
        time_t now = time(nullptr);
        if (_lastSeen > 0) {
            debugLog("CLOCK", "NTP synced; clock stepped by ~%+lld s (was %s)",
                     (long long)(now - _lastSeen),
                     _restored == CLOCK_FLASH ? "flash-restored" : "unset");
        } else {
            debugLog("CLOCK", "NTP synced; clock set");
        }
        saveClock();
        return;
    }

    if (!_ntpSynced && timeIsValid()) _lastSeen = time(nullptr);

    // Persist periodically while the clock is trustworthy enough to save.
    if (clockSource() != CLOCK_NONE && timeIsValid()) {
        if (!_everSaved || millis() - _lastSaveMs >= CLOCK_SAVE_INTERVAL_SEC * 1000UL) {
            saveClock();
        }
    }
}
