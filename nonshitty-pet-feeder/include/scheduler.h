#pragma once
#include "types.h"
#include "dispenser.h"
#include <vector>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Scheduler
//
// Holds the daily feed schedule and fires the Dispenser at the right times.
// Call tick() from loop() every iteration; it is cheap when nothing is due.
// ─────────────────────────────────────────────────────────────────────────────
class Scheduler {
public:
    Scheduler(Dispenser* dispenser);

    // Reload the schedule from storage (also called on startup and after
    // a schedule POST from the web UI).
    void reload();

    // Check whether any schedule entry is due right now.  Must be called from
    // loop() continuously.
    void tick();

    // Replace the in-memory schedule and persist it.
    bool setSchedule(const std::vector<ScheduleEntry>& entries);

    const std::vector<ScheduleEntry>& getSchedule() const { return _schedule; }

    // Returns the next upcoming feed time (local epoch), or 0 if the schedule
    // is empty.
    time_t getNextFeedTime() const;

    // Call once after NTP sync to dispense the most recently missed feed (if any).
    void recoverMissedFeeds();

private:
    Dispenser*               _dispenser;
    std::vector<ScheduleEntry> _schedule;

    // Tracks the last minute we already fired, so a slow loop() doesn't
    // double-trigger within the same minute.
    int  _lastFiredDay    = -1;
    int  _lastFiredHour   = -1;
    int  _lastFiredMinute = -1;
};
