#include "webhook.h"
#include "config.h"
#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

void notifyFailure(time_t ts, int requested, int delivered, const String& error) {
    Serial.printf("[Webhook] Failure — ts=%lld requested=%d delivered=%d error=\"%s\"\n",
                  (long long)ts, requested, delivered, error.c_str());

    if (strlen(WEBHOOK_URL) == 0) return;
    // Build JSON payload
    JsonDocument doc;
    doc["ts"]        = (long long)ts;
    doc["requested"] = requested;
    doc["delivered"] = delivered;
    doc["error"]     = error;
    String body;
    serializeJson(doc, body);

    HTTPClient http;
    http.begin(WEBHOOK_URL);
    http.setTimeout(WEBHOOK_TIMEOUT_MS);
    http.addHeader("Content-Type", "application/json");
    int code = http.POST(body);
    if (code > 0) {
        Serial.printf("[Webhook] POST %s → HTTP %d\n", WEBHOOK_URL, code);
    } else {
        Serial.printf("[Webhook] POST %s failed: %s\n", WEBHOOK_URL, http.errorToString(code).c_str());
    }
    http.end();
}
