#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_system.h>

#include "secrets.h"
#include "config.h"
#include "hardware.h"
#include "dispenser.h"
#include "scheduler.h"
#include "storage.h"
#include "webserver.h"
#include "timekeeping.h"
#include "debuglog.h"

// ─── Module instances (heap-allocated in setup) ───────────────────────────────
static HardwareInterface* hw         = nullptr;
static Dispenser*         dispenser  = nullptr;
static Scheduler*         scheduler  = nullptr;

// ─── Network state ────────────────────────────────────────────────────────────
// Wi-Fi events arrive on the WiFi/event task; they only set flags.  Anything
// heavier (mDNS, web server, recovery) runs from networkTick() on loop().
static volatile bool g_gotIp        = false;
static bool          g_webStarted   = false;
static bool          g_mdnsStarted  = false;
static bool          g_recovered    = false;

static uint8_t       g_lastDiscReason = 0;
static unsigned long g_lastDiscLogMs  = 0;

static const char* resetReasonName(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_POWERON:   return "power-on";
        case ESP_RST_SW:        return "software reset";
        case ESP_RST_PANIC:     return "panic/crash";
        case ESP_RST_INT_WDT:   return "interrupt watchdog";
        case ESP_RST_TASK_WDT:  return "task watchdog";
        case ESP_RST_WDT:       return "other watchdog";
        case ESP_RST_BROWNOUT:  return "BROWNOUT";
        case ESP_RST_DEEPSLEEP: return "deep sleep wake";
        case ESP_RST_EXT:       return "external reset pin";
        default:                return "unknown";
    }
}

static void onWiFiEvent(arduino_event_id_t event, arduino_event_info_t info) {
    switch (event) {
        case ARDUINO_EVENT_WIFI_STA_CONNECTED:
            debugLog("WIFI", "associated with AP");
            g_lastDiscReason = 0;
            break;
        case ARDUINO_EVENT_WIFI_STA_GOT_IP:
            debugLog("WIFI", "got IP %s (RSSI %d)",
                     IPAddress(info.got_ip.ip_info.ip.addr).toString().c_str(), WiFi.RSSI());
            g_gotIp = true;
            break;
        case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
            // Auto-reconnect retries every few seconds while the router is down;
            // log only when the reason changes or every 5 min, to keep the log useful.
            uint8_t reason = info.wifi_sta_disconnected.reason;
            if (reason != g_lastDiscReason || millis() - g_lastDiscLogMs > 300000UL) {
                debugLog("WIFI", "disconnected, reason %u (%s); auto-reconnect is retrying",
                         reason, WiFi.disconnectReasonName((wifi_err_reason_t)reason));
                g_lastDiscReason = reason;
                g_lastDiscLogMs  = millis();
            }
            break;
        }
        default: break;
    }
}

// ─── Network bring-up (non-blocking) ──────────────────────────────────────────
static void startWiFi() {
    WiFi.onEvent(onWiFiEvent);
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    // Arduino-ESP32 already defaults to true; stated explicitly because the whole
    // design relies on the stack retrying forever while the router is down.
    WiFi.setAutoReconnect(true);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    debugLog("WIFI", "connecting to '%s' in the background", WIFI_SSID);
}

// Runs from loop(): reacts to the network coming up, whenever that happens.
static void networkTick() {
    if (g_gotIp) {
        g_gotIp = false;

        // (Re)start mDNS on every (re)connect — the responder can lose its netif.
        if (g_mdnsStarted) MDNS.end();
        g_mdnsStarted = MDNS.begin(MDNS_HOSTNAME);
        debugLog("NET", "mDNS %s (http://%s.local)", g_mdnsStarted ? "started" : "FAILED", MDNS_HOSTNAME);

        if (!g_webStarted) {
            initWebServer(scheduler, dispenser);
            g_webStarted = true;
        }

        // Nudge SNTP so we don't wait out its retry backoff after an outage.
        if (clockSource() != CLOCK_NTP) {
            configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
        }
    }

    // Once NTP has set the clock, look for feeds missed while we were offline.
    if (!g_recovered && clockSource() == CLOCK_NTP) {
        g_recovered = true;
        scheduler->recoverMissedFeeds();
    }
}

// ─── Heartbeat (Serial every minute; stored log only hourly to avoid crowding out events)
static unsigned long g_lastLoopMs   = 0;
static unsigned long g_maxLoopGapMs = 0;
static unsigned long g_lastHb       = 0;
static unsigned long g_lastHbLog    = 0;

static void heartbeat() {
    unsigned long now = millis();
    if (g_lastLoopMs) {
        unsigned long gap = now - g_lastLoopMs;
        if (gap > g_maxLoopGapMs) g_maxLoopGapMs = gap;
    }
    g_lastLoopMs = now;

    if (now - g_lastHb >= HEARTBEAT_SERIAL_MS) {
        g_lastHb = now;
        Serial.printf("[HB] up=%lus wifi=%d clock=%s heap=%u minHeap=%u maxLoopGap=%lums\n",
                      now / 1000, (int)WiFi.status(), clockSourceName(),
                      ESP.getFreeHeap(), ESP.getMinFreeHeap(), g_maxLoopGapMs);
        if (now - g_lastHbLog >= HEARTBEAT_LOG_MS) {
            g_lastHbLog = now;
            debugLog("HB", "up=%lus wifi=%s clock=%s heap=%u minHeap=%u maxLoopGap=%lums",
                     now / 1000, WiFi.status() == WL_CONNECTED ? "up" : "down",
                     clockSourceName(), ESP.getFreeHeap(), ESP.getMinFreeHeap(), g_maxLoopGapMs);
        }
        g_maxLoopGapMs = 0;
    }
}

// ─── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // TinyUSB CDC needs a moment to enumerate on the host side.
    unsigned long t0 = millis();
    while (!Serial && millis() - t0 < 5000) delay(10);
    delay(100);

    Serial.println("\n== Pet Feeder Booting ==");

    // Storage — non-fatal: if it fails we continue without persistence
    bool storageOk = initStorage();
    if (!storageOk) {
        Serial.println("[Boot] Storage failed — running without LittleFS");
    }
    initDebugLog();
    debugLog("BOOT", "boot #%lu, reset reason: %s, storage %s", bootCount(),
             resetReasonName(esp_reset_reason()), storageOk ? "ok" : "FAILED");

    // SNTP + timezone are configured up front and sync whenever the network
    // appears; boot never waits on them.
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    initTimekeeping();   // restores the last saved time if the clock is unset

    // Hardware abstraction (sim or real, chosen at compile time)
    hw        = createHardware();
    dispenser = new Dispenser(hw);
    scheduler = new Scheduler(dispenser);
    scheduler->reload();

    // Network comes up in the background; the schedule runs regardless.
    startWiFi();

    debugLogFlush();
    Serial.println("== Pet Feeder Ready ==");
}

// ─── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    // Let the simulation poll serial for ON / OFF commands.
    // In real-hardware builds, update() is a no-op.
    hw->update();

    // Execute any dispense queued by the web server (must run on this core,
    // not inside the async web callback which runs on the WiFi/TCP task).
    dispenser->poll();

    // Check whether any scheduled feed is due right now.
    scheduler->tick();

    networkTick();
    timekeepingTick();
    heartbeat();
    debugLogTick();

    delay(100);
}
