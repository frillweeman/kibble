#include "dispenser.h"
#include "storage.h"
#include "webhook.h"
#include "debuglog.h"
#include "config.h"
#include <Arduino.h>

// Confirm button state is stable for this many ms before treating it as real.
static const unsigned long DEBOUNCE_MS = 20;

static bool readDebounced(HardwareInterface* hw) {
    bool first = hw->readButton();
    unsigned long start = millis();
    while (millis() - start < DEBOUNCE_MS) {
        if (hw->readButton() != first) return !first; // bouncing, not stable
        delay(2);
    }
    return first;
}

Dispenser::Dispenser(HardwareInterface* hw)
    : _hw(hw), _mx(xSemaphoreCreateMutex()), _nextId(0) {}

uint32_t Dispenser::requestDispense(int clicks) {
    uint32_t id = 0;
    xSemaphoreTake(_mx, portMAX_DELAY);
    // Busy while a previous request is still queued or running, so each id's
    // result stays intact until the caller has seen it.
    if (_res.id == 0 || _res.state == DispenseState::Done) {
        id = ++_nextId;
        _res = DispenseResult();
        _res.id        = id;
        _res.state     = DispenseState::Queued;
        _res.requested = clicks;
    }
    xSemaphoreGive(_mx);
    if (id) debugLog("DISPENSE", "manual request #%lu queued: %d click(s)", (unsigned long)id, clicks);
    else    debugLog("DISPENSE", "manual request rejected: busy");
    return id;
}

bool Dispenser::getResult(uint32_t id, DispenseResult& out) {
    bool found = false;
    xSemaphoreTake(_mx, portMAX_DELAY);
    if (id != 0 && _res.id == id) {
        out   = _res;
        found = true;
    }
    xSemaphoreGive(_mx);
    return found;
}

int Dispenser::poll() {
    uint32_t id = 0;
    int clicks  = 0;
    xSemaphoreTake(_mx, portMAX_DELAY);
    if (_res.id != 0 && _res.state == DispenseState::Queued) {
        _res.state = DispenseState::Running;
        id     = _res.id;
        clicks = _res.requested;
    }
    xSemaphoreGive(_mx);
    if (id == 0) return 0;

    debugLog("DISPENSE", "manual request #%lu running", (unsigned long)id);
    int delivered = dispense(clicks);
    time_t ts = time(nullptr);

    char err[64] = "";
    if (delivered < clicks) {
        snprintf(err, sizeof(err), "only %d/%d clicks detected", delivered, clicks);
        appendFeedLog(ts, delivered, err);
        notifyFailure(ts, clicks, delivered, err);
    } else {
        appendFeedLog(ts, delivered);
    }
    debugLog("DISPENSE", "manual request #%lu done: %d/%d%s%s", (unsigned long)id,
             delivered, clicks, err[0] ? " — " : "", err);

    xSemaphoreTake(_mx, portMAX_DELAY);
    _res.delivered = delivered;
    strlcpy(_res.error, err, sizeof(_res.error));
    _res.state = DispenseState::Done;   // last, so readers never see a half-filled result as Done
    xSemaphoreGive(_mx);
    return delivered;
}

int Dispenser::dispense(int clicks) {
    if (clicks <= 0) return 0;

    Serial.printf("[Dispenser] Starting dispense: %d click(s) requested\n", clicks);
    _hw->setMotor(true);

    int clicksDetected   = 0;
    bool lastButtonState = readDebounced(_hw);
    unsigned long clickStart = millis();  // reset after each detected click

    while (clicksDetected < clicks) {
        // ── Per-click safety timeout ──────────────────────────────────────────
        if (millis() - clickStart > MOTOR_TIMEOUT_MS) {
            Serial.printf("[Dispenser] TIMEOUT waiting for click %d/%d — cutting motor!\n",
                          clicksDetected + 1, clicks);
            break;
        }

        bool buttonNow = readDebounced(_hw);

        // ── Detect rising edge (button goes not-pressed → pressed) ────────────
        if (buttonNow && !lastButtonState) {
            clicksDetected++;
            Serial.printf("[Dispenser] Click %d/%d detected\n", clicksDetected, clicks);
            clickStart = millis();  // restart timeout for the next click

            // Hold motor for the padding duration so the cam aligns cleanly.
            if (BUTTON_PADDING_MS > 0) delay(BUTTON_PADDING_MS);

            // Wait for button to release before watching for the next press.
            unsigned long releaseStart = millis();
            while (readDebounced(_hw)) {
                if (millis() - releaseStart > MOTOR_TIMEOUT_MS) {
                    Serial.println("[Dispenser] Button stuck — aborting.");
                    _hw->setMotor(false);
                    return clicksDetected;
                }
                delay(5);
            }
        }

        lastButtonState = buttonNow;
        delay(5);
    }

    _hw->setMotor(false);
    Serial.printf("[Dispenser] Done. %d/%d clicks delivered.\n", clicksDetected, clicks);
    return clicksDetected;
}
