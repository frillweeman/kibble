# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository layout

This repo has two parts that together form one product:

- `firmware/` — the PlatformIO firmware project (ESP32-S3) that runs on the
  physical feeder. This is where almost all real work happens.
- `frontend/index.html` + `openapi.yaml` — reference copies of the web UI and the HTTP API
  contract, kept at the repo root for easy viewing/editing outside the embedded filesystem.
  `firmware/data/index.html` is a symlink to `frontend/index.html`, so there is
  one file to edit; run `pio run -t uploadfs` to ship it.

## Build / upload commands

All commands run from `firmware/` (requires PlatformIO Core / `pio` CLI):

```bash
pio run                      # compile firmware
pio run -t upload            # compile + flash firmware to the board
pio run -t uploadfs          # upload data/ (web UI, favicon, icon) to LittleFS
pio device monitor           # serial monitor at 115200 baud
pio run -t clean             # clean build artifacts
```

There is no unit test suite (`test/` is the stock PlatformIO placeholder) and no JS/web
build step — `frontend/index.html` is plain HTML/CSS/JS with no bundler.

### Required local file not in git

`include/secrets.h` is gitignored and must exist locally (it is `#include`d by `main.cpp`)
before the project will compile. It must define `WIFI_SSID` and `WIFI_PASS`; copy
`include/secrets.h.example` to start. Never create, read, or print the contents of the real
file.

## Firmware architecture

Single-core-sensitive Arduino app (`src/main.cpp` is the entry point) split into small
modules under `include/`/`src/` with one header/cpp pair each:

- **`hardware.{h,cpp}`** — `HardwareInterface` is a pure-virtual abstraction over "motor on/off"
  and "is the cam-switch button pressed". `createHardware()` returns either `RealHardware`
  (LEDC PWM on `MOTOR_PIN`, pulldown button on `BUTTON_PIN`) or `SimulatedHardware` (onboard
  NeoPixel + serial `ON`/`OFF` commands), selected at **compile time** by whether
  `-D SIMULATION_MODE` is present in `platformio.ini`'s `build_flags`. It is currently absent,
  i.e. the default env builds for real hardware. No other code depends on which one is active.
- **`dispenser.{h,cpp}`** — `Dispenser::dispense()` is a *blocking* loop that watches for
  cam-switch rising edges and must only be called from `loop()` (core 1). Web requests run on
  a different task/core, so they call `requestDispense()`, which queues the request under a
  mutex and returns an id (0 if one is already queued/running); `loop()` drains it via
  `Dispenser::poll()`. The latest request's `{id, state, delivered, error}` is exposed by
  `getResult()` / `GET /api/dispense/{id}`. Each click has its own
  `MOTOR_TIMEOUT_MS` safety cutoff, reset on every detected click.
- **`scheduler.{h,cpp}`** — owns the in-memory `ScheduleEntry` list, fires `tick()` once per
  matching minute (guarded against double-firing within the same loop-second), computes
  `getNextFeedTime()`, and runs `recoverMissedFeeds()` once after boot/NTP sync: it looks back
  up to `RECOVERY_LOOKBACK_SEC` for a scheduled time with no matching log entry (within
  `RECOVERY_WINDOW_SEC`) and dispenses it once, logging both the original miss and the retry
  (`retryFor` links them). Recovery only considers times after the schedule's `createdAt`, so
  a never-saved schedule is never retroactively dispensed.
- **`timekeeping.{h,cpp}`** — no RTC, so the clock is restored from `CLOCK_FILE` (saved every
  `CLOCK_SAVE_INTERVAL_SEC`) when unset at boot (`clockSource()` = `flash`, always behind real
  time) and replaced by NTP when it arrives (`ntp`). Use `getNow()`/`timeIsValid()`, never
  `getLocalTime()` (it blocks up to 5 s). While `flash`, `Scheduler::tick()` skips a slot already
  in the feed log to avoid double feeds; `recoverMissedFeeds()` runs once, only after NTP.
- **`debuglog.{h,cpp}`** — `debugLog(tag, fmt, ...)` buffers lines, mirrors to Serial, flushes to
  `DEBUG_LOG_FILE` every `DEBUG_LOG_FLUSH_MS` and rotates at `DEBUG_LOG_MAX_BYTES`. Served by
  `/api/debuglog`. Prefer it over bare `Serial.printf` for anything worth knowing in the field.
- **`storage.{h,cpp}`** — all LittleFS persistence. Schedule is stored as
  `{"createdAt": <epoch>, "entries": [...]}` in `SCHEDULE_FILE`; the feed log is a flat JSON
  array in `LOG_FILE`, trimmed to `MAX_LOG_ENTRIES` (oldest dropped first). `initStorage()`
  tries several partition-label fallbacks (`spiffs`, `ffat`, default) because board partition
  tables vary.
- **`webhook.{h,cpp}`** — fire-and-log-only POST to `WEBHOOK_URL` on dispense failures; no-op
  if `WEBHOOK_URL` is empty (the default).
- **`webserver.{h,cpp}`** — `ESPAsyncWebServer` on port 80, non-blocking, runs on core 0.
  Mirrors `openapi.yaml` exactly; POST handlers use the body-callback pattern (empty request
  handler + `nullptr` upload handler + body handler that checks `index + len < total` to wait
  for the full body before parsing).
- **`config.h`** — all tunables (pins, timeouts, NTP server/offsets, file paths, recovery
  windows). Change hardware pins, motor PWM duty, and timing here rather than hardcoding
  elsewhere.
- **`types.h`** — the two POD structs (`ScheduleEntry`, `FeedLogEntry`) shared across modules.

### Network bring-up

`setup()` never blocks on Wi-Fi or NTP: it starts `WiFi.begin()` (Arduino-ESP32 auto-reconnect
keeps retrying while the router is down) and calls `configTime()` up front. `networkTick()` in
`main.cpp` reacts to the GOT_IP flag set by the Wi-Fi event handler: it (re)starts mDNS, starts
the web server once, and runs `recoverMissedFeeds()` once NTP has synced. The schedule runs
whether or not the network is up.

### Core/task split to keep in mind

`loop()` (core 1) every ~100ms: `hw->update()` (serial polling, simulation only),
`dispenser->poll()` (drain any web-queued dispense), `scheduler->tick()` (check the schedule),
then `networkTick()`, `timekeepingTick()`, the heartbeat (Serial each minute, stored log hourly) and `debugLogTick()`. Anything that touches hardware directly or blocks must happen here, not inside an
`AsyncWebServerRequest` handler.

## API contract

`openapi.yaml` is the source of truth for the HTTP API (`/api/status`, `/api/schedule`,
`/api/log`, `/api/dispense`, `/api/dispense/{id}`, `/api/debuglog`) and should be updated alongside any change to `webserver.cpp`'s
routes or JSON shapes.
