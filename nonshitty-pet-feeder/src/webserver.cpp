#include "webserver.h"
#include "storage.h"
#include "config.h"
#include "timekeeping.h"
#include "debuglog.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>

static AsyncWebServer _server(80);
static Scheduler*  _scheduler  = nullptr;
static Dispenser*  _dispenser  = nullptr;

// ─── Utility: format a time_t as ISO-8601 local string ───────────────────────
static String fmtTime(time_t t) {
    if (t == 0) return "never";
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
    return String(buf);
}

// ─── GET / ────────────────────────────────────────────────────────────────────
static void handleRoot(AsyncWebServerRequest* req) {
    if (LittleFS.exists("/index.html")) {
        req->send(LittleFS, "/index.html", "text/html");
    } else {
        req->send(200, "text/html",
            "<!DOCTYPE html><html><body>"
            "<h1>Pet Feeder</h1>"
            "<p>Upload data filesystem to enable the UI.</p>"
            "</body></html>");
    }
}

// ─── GET /api/status ─────────────────────────────────────────────────────────
static void handleStatus(AsyncWebServerRequest* req) {
    struct tm now;
    String currentTimeStr = "syncing...";
    if (getNow(&now)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &now);
        currentTimeStr = String(buf);
    }

    JsonDocument doc;
    doc["currentTime"]  = currentTimeStr;
    doc["lastFeedTime"] = fmtTime(lastFeedTime());
    doc["nextFeedTime"] = fmtTime(_scheduler->getNextFeedTime());
    doc["clockSource"]  = clockSourceName();

    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// ─── GET /api/schedule ───────────────────────────────────────────────────────
static void handleGetSchedule(AsyncWebServerRequest* req) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& e : _scheduler->getSchedule()) {
        JsonObject obj = arr.add<JsonObject>();
        obj["hour"]   = e.hour;
        obj["minute"] = e.minute;
        obj["clicks"] = e.clicks;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// ─── POST /api/schedule ──────────────────────────────────────────────────────
// Body: JSON array of { "hour": 0-23, "minute": 0-59, "clicks": N }
static void handlePostScheduleBody(AsyncWebServerRequest* req,
                                   uint8_t* data, size_t len,
                                   size_t index, size_t total)
{
    if (index + len < total) return; // wait for full body

    String body = String((char*)data, len);
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, body);

    if (err || !doc.is<JsonArray>()) {
        req->send(400, "application/json",
                  "{\"error\":\"Invalid JSON array\"}");
        return;
    }

    std::vector<ScheduleEntry> entries;
    for (JsonObject obj : doc.as<JsonArray>()) {
        int h = obj["hour"]   | -1;
        int m = obj["minute"] | -1;
        int c = obj["clicks"] | 1;
        if (h < 0 || h > 23 || m < 0 || m > 59 || c < 1) {
            req->send(400, "application/json",
                      "{\"error\":\"Invalid entry values\"}");
            return;
        }
        entries.push_back({h, m, c});
    }

    if (_scheduler->setSchedule(entries)) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Save failed\"}");
    }
}

// ─── GET /api/log ─────────────────────────────────────────────────────────────
static void handleGetLog(AsyncWebServerRequest* req) {
    req->send(200, "application/json", loadFeedLogJson());
}
// ─── DELETE /api/log ───────────────────────────────────────────────────
static void handleDeleteLog(AsyncWebServerRequest* req) {
    if (clearFeedLog()) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Clear failed\"}");
    }
}
// ─── POST /api/dispense ───────────────────────────────────────────────────────
// Optional body: { "clicks": N }  — defaults to 1.
// Returns an id; poll GET /api/dispense/{id} for the outcome.
static void handleDispenseBody(AsyncWebServerRequest* req,
                                uint8_t* data, size_t len,
                                size_t index, size_t total)
{
    if (index + len < total) return; // wait for full body

    int clicks = 1;
    if (len > 0) {
        JsonDocument doc;
        if (!deserializeJson(doc, (char*)data, len)) {
            clicks = doc["clicks"] | 1;
        }
    }
    if (clicks < 1) clicks = 1;

    uint32_t id = _dispenser->requestDispense(clicks);
    if (id == 0) {
        req->send(409, "application/json",
                  "{\"ok\":false,\"error\":\"A dispense is already in progress\"}");
        return;
    }

    JsonDocument resp;
    resp["ok"]        = true;
    resp["id"]        = id;
    resp["requested"] = clicks;
    resp["queued"]    = true;
    String out;
    serializeJson(resp, out);
    req->send(200, "application/json", out);
}

// ─── GET /api/dispense/{id} ───────────────────────────────────────────────────
static void handleDispenseResult(AsyncWebServerRequest* req) {
    String url = req->url();
    uint32_t id = (uint32_t)url.substring(url.lastIndexOf('/') + 1).toInt();

    DispenseResult r;
    if (!_dispenser->getResult(id, r)) {
        req->send(404, "application/json",
                  "{\"error\":\"Unknown or expired dispense id\"}");
        return;
    }

    JsonDocument doc;
    doc["id"]        = r.id;
    doc["state"]     = r.state == DispenseState::Queued  ? "queued"
                     : r.state == DispenseState::Running ? "running" : "done";
    doc["requested"] = r.requested;
    if (r.state == DispenseState::Done) {
        doc["delivered"] = r.delivered;
        if (r.error[0]) doc["error"] = r.error;
    }
    String out;
    serializeJson(doc, out);
    req->send(200, "application/json", out);
}

// ─── GET /api/debuglog?lines=N&file=current|old ───────────────────────────────
static void handleGetDebugLog(AsyncWebServerRequest* req) {
    int lines = req->hasParam("lines") ? req->getParam("lines")->value().toInt() : 200;
    bool old  = req->hasParam("file") && req->getParam("file")->value() == "old";
    req->send(200, "text/plain; charset=utf-8", debugLogTail(lines, old));
}

// ─── DELETE /api/debuglog ─────────────────────────────────────────────────────
static void handleDeleteDebugLog(AsyncWebServerRequest* req) {
    if (clearDebugLog()) {
        req->send(200, "application/json", "{\"ok\":true}");
    } else {
        req->send(500, "application/json", "{\"error\":\"Clear failed\"}");
    }
}

// ─── Public init ──────────────────────────────────────────────────────────────
void initWebServer(Scheduler* scheduler, Dispenser* dispenser) {
    _scheduler = scheduler;
    _dispenser = dispenser;

    _server.on("/", HTTP_GET, handleRoot);
    _server.on("/api/status",   HTTP_GET,  handleStatus);
    _server.on("/api/schedule", HTTP_GET,  handleGetSchedule);
    _server.on("/api/log",      HTTP_GET,    handleGetLog);
    _server.on("/api/log",      HTTP_DELETE, handleDeleteLog);
    _server.on("/api/debuglog", HTTP_GET,    handleGetDebugLog);
    _server.on("/api/debuglog", HTTP_DELETE, handleDeleteDebugLog);
    _server.on("/api/dispense/*", HTTP_GET,  handleDispenseResult);

    // POST handlers: empty request handler + nullptr file handler + body handler
    _server.on("/api/schedule", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        handlePostScheduleBody);

    _server.on("/api/dispense", HTTP_POST,
        [](AsyncWebServerRequest* req) {},
        nullptr,
        handleDispenseBody);

    // Static assets stored in LittleFS
    _server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/favicon.ico", "image/x-icon");
    });
    _server.on("/apple-touch-icon-180x180.png", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/apple-touch-icon-180x180.png", "image/png");
    });

    _server.onNotFound([](AsyncWebServerRequest* req) {
        req->send(404, "application/json", "{\"error\":\"Not found\"}");
    });

    _server.begin();
    debugLog("WEB", "listening on port 80");
}
