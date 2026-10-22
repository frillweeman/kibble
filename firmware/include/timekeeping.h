#pragma once
#include <Arduino.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────────────────────
// Timekeeping — wall-clock helpers and flash-backed clock persistence.
//
// There is no RTC, so system time is lost on power loss.  The last known time is
// saved to flash periodically; after a reboot without network we resume from it
// (always slightly BEHIND real time) until NTP corrects the clock.
// ─────────────────────────────────────────────────────────────────────────────

enum ClockSource {
    CLOCK_NONE,   // clock never set; schedule cannot run
    CLOCK_FLASH,  // restored from flash, approximate (behind real time)
    CLOCK_NTP     // synced from NTP
};

// True once the system clock holds a plausible time (not 1970).
bool timeIsValid();

// Non-blocking replacement for getLocalTime(): false if the clock isn't valid.
bool getNow(struct tm* out);

ClockSource clockSource();
const char* clockSourceName();

// Call in setup() after initStorage() and after configTime() (so TZ is set).
void initTimekeeping();

// Call every loop(): saves the clock periodically and notices NTP syncs.
void timekeepingTick();
