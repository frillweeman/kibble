#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// Webhook — failure notifications
//
// If WEBHOOK_URL is non-empty in config.h, notifyFailure() POSTs a JSON body:
//   { "ts": <unix>, "requested": N, "delivered": M, "error": "..." }
// to that URL.  On a failed HTTP request or an empty URL it only logs to Serial.
// ─────────────────────────────────────────────────────────────────────────────
void notifyFailure(time_t ts, int requested, int delivered, const String& error);
