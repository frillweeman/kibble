# Pet Feeder

An ESP32-S3 pet feeder with a small web UI. It dispenses on a daily schedule,
lets you feed on demand from your phone, and keeps working through power and
Wi-Fi outages.

## Repository layout

| Path | What it is |
| --- | --- |
| `firmware/` | PlatformIO firmware project (ESP32-S3). Almost all real work happens here. |
| `frontend/index.html` | The web UI (plain HTML/CSS/JS, no build step). `firmware/data/index.html` is a symlink to it. |
| `openapi.yaml` | The HTTP API contract. Keep it in step with `src/webserver.cpp`. |
| `CLAUDE.md` | Architecture notes for contributors and coding agents. |

## Setup

1. Install [PlatformIO Core](https://platformio.org/install/cli).
2. Create your Wi-Fi credentials file (it is gitignored):

   ```bash
   cd firmware
   cp include/secrets.h.example include/secrets.h
   # edit include/secrets.h and set WIFI_SSID / WIFI_PASS
   ```

3. Check pins and timing in `include/config.h` (motor pin, cam-switch pin, motor
   PWM duty, timezone offsets, NTP server).

## Build and flash

Run from `firmware/`:

```bash
pio run                  # compile
pio run -t upload        # flash the firmware
pio run -t uploadfs      # upload data/ (web UI, icons) to LittleFS
pio device monitor       # serial monitor, 115200 baud
```

After a UI change, run `pio run -t uploadfs` again.

## Using it

Open `http://feeder.local` (or the IP shown in the serial monitor). From there you
can feed now, edit the schedule, read the feed log, switch light/dark/auto theme,
and open **Diagnostics** to read the device's troubleshooting log.

## Offline behaviour

- The schedule and feed log live in flash, so they survive power loss.
- Boot never waits for Wi-Fi. Wi-Fi reconnects in the background, and the web UI and
  mDNS start as soon as it connects.
- There is no RTC. The last known time is saved to flash every 10 minutes; after a
  reboot with no network the feeder resumes from that time (slightly behind real
  time, so feeds may run late but not early) until NTP corrects it. The UI marks
  the time as approximate until then.
- Once NTP syncs, the feeder checks the last 24 hours for scheduled feeds it missed
  and dispenses the most recent one. A slot already in the feed log is never fed
  twice.

## Troubleshooting

The **Diagnostics** card (or `GET /api/debuglog`) shows boot count and reset reason
(power-on, brownout, crash), Wi-Fi connects and disconnects, clock restores and NTP
syncs, every scheduled and manual feed, and an hourly heartbeat. If the
feeder seems stuck, read it first.
