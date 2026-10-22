#pragma once
#include "hardware.h"
#include <stdint.h>
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Dispenser
//
// dispense() is blocking and MUST only be called from loop() (core 1).
// Web callbacks (core 0) must use requestDispense() instead, which queues the
// request; loop() runs it via poll().  Each manual request gets an id so the UI
// can ask for exactly *its* result (see getResult()).
// ─────────────────────────────────────────────────────────────────────────────

enum class DispenseState { Queued, Running, Done };

// Snapshot of the most recent manual dispense request.
struct DispenseResult {
    uint32_t      id        = 0;
    DispenseState state     = DispenseState::Done;
    int           requested = 0;
    int           delivered = 0;
    char          error[64] = "";   // empty on success
};

class Dispenser {
public:
    explicit Dispenser(HardwareInterface* hw);

    // Queue a dispense from any context (safe to call from web callbacks).
    // Returns the request id (> 0), or 0 if a request is already queued/running.
    uint32_t requestDispense(int clicks);

    // Copies the latest request's state into `out` if its id matches.
    // Only the most recent request is remembered.
    bool getResult(uint32_t id, DispenseResult& out);

    // Call from loop() only. Executes a queued request and returns clicks
    // delivered, or 0 if nothing was pending.
    int poll();

    // Blocking dispense — call from loop() only.
    int dispense(int clicks);

private:
    HardwareInterface* _hw;
    SemaphoreHandle_t  _mx;
    DispenseResult     _res;     // guarded by _mx
    uint32_t           _nextId;  // guarded by _mx
};
