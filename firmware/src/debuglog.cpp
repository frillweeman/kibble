#include "debuglog.h"
#include "timekeeping.h"
#include "config.h"
#include <LittleFS.h>
#include <stdarg.h>

static SemaphoreHandle_t _mx        = nullptr;
static String            _buf;
static unsigned long     _lastFlush = 0;
static unsigned long     _boot      = 0;
static bool              _fsReady   = false;

static const size_t BUF_CAP = 8192;   // drop oldest unflushed text beyond this

unsigned long bootCount() { return _boot; }

static void lock()   { if (_mx) xSemaphoreTake(_mx, portMAX_DELAY); }
static void unlock() { if (_mx) xSemaphoreGive(_mx); }

void initDebugLog() {
    _mx = xSemaphoreCreateMutex();
    _fsReady = true;

    // Boot counter lives in its own tiny file so it survives log clears/rotation.
    unsigned long n = 0;
    File f = LittleFS.open(DEBUG_BOOT_FILE, "r");
    if (f) { n = f.readString().toInt(); f.close(); }
    _boot = n + 1;
    File w = LittleFS.open(DEBUG_BOOT_FILE, "w");
    if (w) { w.print(_boot); w.close(); }
}

void debugLog(const char* tag, const char* fmt, ...) {
    char msg[224];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char stamp[24] = "?";
    struct tm tm;
    if (getNow(&tm)) {
        char t[20];
        strftime(t, sizeof(t), "%m-%d %H:%M:%S", &tm);
        // '~' marks a clock that has not been confirmed by NTP.
        snprintf(stamp, sizeof(stamp), "%s%s", clockSource() == CLOCK_NTP ? "" : "~", t);
    }

    unsigned long up = millis() / 1000;
    char line[300];
    snprintf(line, sizeof(line), "[b%lu +%lus %s] [%s] %s\n", _boot, up, stamp, tag, msg);

    Serial.print(line);

    lock();
    _buf += line;
    if (_buf.length() > BUF_CAP) _buf.remove(0, _buf.length() - BUF_CAP);
    unlock();
}

static void rotateIfNeeded() {
    File f = LittleFS.open(DEBUG_LOG_FILE, "r");
    if (!f) return;
    size_t sz = f.size();
    f.close();
    if (sz < DEBUG_LOG_MAX_BYTES) return;
    LittleFS.remove(DEBUG_LOG_OLD_FILE);
    LittleFS.rename(DEBUG_LOG_FILE, DEBUG_LOG_OLD_FILE);
}

void debugLogFlush() {
    if (!_fsReady) return;
    lock();
    String pending = _buf;
    _buf = "";
    unlock();
    _lastFlush = millis();
    if (pending.length() == 0) return;

    rotateIfNeeded();
    File f = LittleFS.open(DEBUG_LOG_FILE, "a");
    if (!f) {
        Serial.println("[DebugLog] could not open log file for append");
        return;
    }
    f.print(pending);
    f.close();
}

void debugLogTick() {
    if (!_fsReady) return;
    if (millis() - _lastFlush < DEBUG_LOG_FLUSH_MS) return;
    lock();
    bool any = _buf.length() > 0;
    unlock();
    if (any) debugLogFlush();
    else _lastFlush = millis();
}

static String lastLines(const String& text, int maxLines) {
    if (maxLines <= 0) return "";
    int count = 0;
    int i = (int)text.length() - 1;
    if (i >= 0 && text[i] == '\n') i--;   // ignore trailing newline
    for (; i >= 0; i--) {
        if (text[i] == '\n' && ++count >= maxLines) { i++; break; }
    }
    if (i < 0) i = 0;
    return text.substring(i);
}

String debugLogTail(int maxLines, bool old) {
    if (maxLines < 1) maxLines = 1;
    if (maxLines > 2000) maxLines = 2000;

    String text;
    File f = LittleFS.open(old ? DEBUG_LOG_OLD_FILE : DEBUG_LOG_FILE, "r");
    if (f) { text = f.readString(); f.close(); }
    if (!old) {
        lock();
        text += _buf;
        unlock();
    }
    return lastLines(text, maxLines);
}

bool clearDebugLog() {
    lock();
    _buf = "";
    unlock();
    LittleFS.remove(DEBUG_LOG_OLD_FILE);
    File f = LittleFS.open(DEBUG_LOG_FILE, "w");
    if (!f) return false;
    f.close();
    return true;
}
