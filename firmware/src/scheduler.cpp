#include "scheduler.h"
#include "storage.h"
#include "webhook.h"
#include "config.h"
#include "timekeeping.h"
#include "debuglog.h"
#include <Arduino.h>
#include <ArduinoJson.h>

Scheduler::Scheduler(Dispenser* dispenser)
    : _dispenser(dispenser) {}

// True if any log entry (or the original slot of a retried entry) lies within
// RECOVERY_WINDOW_SEC of `scheduled`.
static bool isCovered(const std::vector<time_t>& loggedTs, time_t scheduled) {
    for (time_t lt : loggedTs) {
        if (labs((long)(lt - scheduled)) <= RECOVERY_WINDOW_SEC) return true;
    }
    return false;
}

static std::vector<time_t> loadLoggedTimes() {
    std::vector<time_t> loggedTs;
    String logJson = loadFeedLogJson();
    JsonDocument logDoc;
    if (!deserializeJson(logDoc, logJson) && logDoc.is<JsonArray>()) {
        for (JsonObject entry : logDoc.as<JsonArray>()) {
            loggedTs.push_back((time_t)(entry["ts"] | 0));
            // Also cover the original missed time for already-retried entries.
            time_t rf = (time_t)(entry["retryFor"] | 0);
            if (rf) loggedTs.push_back(rf);
        }
    }
    return loggedTs;
}

void Scheduler::reload() {
    _schedule = loadSchedule();
    Serial.printf("[Scheduler] Loaded %d entries\n", _schedule.size());
}

bool Scheduler::setSchedule(const std::vector<ScheduleEntry>& entries) {
    if (!saveSchedule(entries)) return false;
    _schedule = entries;
    Serial.printf("[Scheduler] Schedule updated: %d entries\n", _schedule.size());
    return true;
}

void Scheduler::tick() {
    if (_schedule.empty()) return;

    struct tm now;
    if (!getNow(&now)) return; // clock not set yet (no saved time and no NTP)

    // Only fire at second 0 of the matching minute to avoid thrashing.
    if (now.tm_sec != 0) return;

    // Guard: only fire once per minute regardless of loop speed.
    if (now.tm_yday  == _lastFiredDay    &&
        now.tm_hour  == _lastFiredHour   &&
        now.tm_min   == _lastFiredMinute) {
        return;
    }

    for (const auto& entry : _schedule) {
        if (entry.hour == now.tm_hour && entry.minute == now.tm_min) {
            _lastFiredDay    = now.tm_yday;
            _lastFiredHour   = now.tm_hour;
            _lastFiredMinute = now.tm_min;

            // A flash-restored clock can sit behind a feed that already happened
            // (fed at real 8:00, rebooted, clock restored to 7:55 -> reaches 8:00
            // again).  The feed log carries NTP-accurate timestamps, so skip a slot
            // that is already logged.  Not needed once NTP has set the clock.
            if (clockSource() == CLOCK_FLASH) {
                struct tm slot = now;
                slot.tm_sec = 0;
                if (isCovered(loadLoggedTimes(), mktime(&slot))) {
                    debugLog("SCHED", "slot %02d:%02d already in feed log, skipping (approx clock)",
                             entry.hour, entry.minute);
                    break;
                }
            }

            debugLog("SCHED", "firing %02d:%02d, %d click(s)%s", entry.hour, entry.minute,
                     entry.clicks, clockSource() == CLOCK_NTP ? "" : " (approx clock)");

            time_t ts = time(nullptr);
            int delivered = _dispenser->dispense(entry.clicks);
            if (delivered < entry.clicks) {
                String err = String("only ") + delivered + "/" + entry.clicks + " clicks detected";
                appendFeedLog(ts, delivered, err);
                notifyFailure(ts, entry.clicks, delivered, err);
            } else {
                appendFeedLog(ts, delivered);
            }
            debugLog("SCHED", "%02d:%02d result: %d/%d clicks", entry.hour, entry.minute,
                     delivered, entry.clicks);
            break; // only one entry per minute makes sense
        }
    }
}

time_t Scheduler::getNextFeedTime() const {
    if (_schedule.empty()) return 0;

    struct tm now;
    if (!getNow(&now)) return 0;

    int nowMinutes = now.tm_hour * 60 + now.tm_min;

    // Find the soonest entry later today; otherwise wrap to tomorrow.
    int bestDelta = INT32_MAX;
    const ScheduleEntry* best = nullptr;

    for (const auto& entry : _schedule) {
        int entryMinutes = entry.hour * 60 + entry.minute;
        int delta = entryMinutes - nowMinutes;
        if (delta <= 0) delta += 24 * 60; // wrap to tomorrow
        if (delta < bestDelta) {
            bestDelta = delta;
            best = &entry;
        }
    }

    if (!best) return 0;

    // Build a struct tm for the next occurrence.
    struct tm next = now;
    next.tm_hour   = best->hour;
    next.tm_min    = best->minute;
    next.tm_sec    = 0;
    if (bestDelta > nowMinutes) {
        // Wraps to the next calendar day
        next.tm_mday += 1;
    }
    // mktime normalises the struct (handles month/year rollovers).
    return mktime(&next);
}

void Scheduler::recoverMissedFeeds() {
    if (_schedule.empty()) return;

    struct tm now;
    if (!getNow(&now)) {
        debugLog("SCHED", "recoverMissedFeeds: clock not set, skipping");
        return;
    }

    time_t nowTs = time(nullptr);

    // Only recover feeds scheduled after the schedule was saved.
    // createdAt == 0 means the schedule has never been saved from the UI — skip.
    time_t scheduleCreatedAt = getScheduleCreatedAt();
    if (scheduleCreatedAt == 0) {
        debugLog("SCHED", "recoverMissedFeeds: schedule not yet saved, skipping");
        return;
    }

    // Flat list of logged timestamps (and retried slots) for coverage lookups.
    std::vector<time_t> loggedTs = loadLoggedTimes();

    // Find the single most-recent past scheduled time that has no log coverage.
    time_t bestMissedTs = 0;
    const ScheduleEntry* bestEntry = nullptr;

    for (const auto& entry : _schedule) {
        // Build up to two candidate times: today and yesterday.
        for (int dayOffset = 0; dayOffset <= 1; dayOffset++) {
            struct tm candidate = now;
            candidate.tm_hour  = entry.hour;
            candidate.tm_min   = entry.minute;
            candidate.tm_sec   = 0;
            candidate.tm_mday -= dayOffset;
            time_t candidateTs = mktime(&candidate);

            // Must be in the past and within the lookback window.
            if (candidateTs >= nowTs) continue;
            if (nowTs - candidateTs > RECOVERY_LOOKBACK_SEC) continue;

            // Only recover feeds that were scheduled after the schedule was created.
            if (candidateTs < scheduleCreatedAt) continue;

            if (!isCovered(loggedTs, candidateTs) && candidateTs > bestMissedTs) {
                bestMissedTs = candidateTs;
                bestEntry    = &entry;
            }
        }
    }

    if (!bestEntry) {
        debugLog("SCHED", "recoverMissedFeeds: no missed feeds");
        return;
    }

    struct tm missedTm;
    localtime_r(&bestMissedTs, &missedTm);
    debugLog("SCHED", "recovering missed feed from %02d:%02d, %d click(s)",
             missedTm.tm_hour, missedTm.tm_min, bestEntry->clicks);

    // 1. Log the original missed event as a failure.
    appendFeedLog(bestMissedTs, 0, "missed feed \u2014 device was offline");

    // 2. Dispense now and log at the actual time, referencing the missed slot.
    time_t retryTs  = time(nullptr);
    int delivered   = _dispenser->dispense(bestEntry->clicks);
    if (delivered < bestEntry->clicks) {
        String err = String("only ") + delivered + "/" + bestEntry->clicks + " clicks detected";
        appendFeedLog(retryTs, delivered, err, bestMissedTs);
        notifyFailure(retryTs, bestEntry->clicks, delivered, err);
    } else {
        appendFeedLog(retryTs, delivered, "", bestMissedTs);
    }
}
