#pragma once
#include "scheduler.h"
#include "dispenser.h"

// ─────────────────────────────────────────────────────────────────────────────
// Web server
//
// Initialise once in setup() after Wi-Fi is connected.
// ESPAsyncWebServer runs on core 0 and is fully non-blocking.
//
// Routes
// ──────
//  GET  /                → Hello World placeholder (replace with your page)
//  GET  /api/status      → { currentTime, lastFeedTime, nextFeedTime }
//  GET  /api/schedule    → current schedule JSON array
//  POST /api/schedule    → replace schedule (body: JSON array)
//  GET  /api/log         → feed log JSON array
//  POST /api/dispense    → manual dispense  (body: {"clicks": N}, default 1)
// ─────────────────────────────────────────────────────────────────────────────
void initWebServer(Scheduler* scheduler, Dispenser* dispenser);
