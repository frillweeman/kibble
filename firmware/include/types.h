#pragma once
#include <Arduino.h>
#include <vector>

// One entry in the daily feed schedule.
struct ScheduleEntry {
    int hour;    // 0-23
    int minute;  // 0-59
    int clicks;  // number of cam-switch presses (portions) to dispense
};

// One entry in the persistent feed log.
struct FeedLogEntry {
    time_t timestamp;  // Unix epoch (UTC)
    int    clicks;
    String error;      // empty string = success; non-empty = failure description
};
