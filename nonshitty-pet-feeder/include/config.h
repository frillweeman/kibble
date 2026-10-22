#pragma once

// ── Real hardware pins ────────────────────────────────────────────────────────
// MOSFET gate (motor control) and cam-switch button input.
#define MOTOR_PIN        3
#define BUTTON_PIN       9

// PWM duty cycle for the motor (0–255).
// Lower values = less inrush current = less voltage droop on the supply rail.
// Start around 200 and tune down until the motor runs reliably without
// browning out the ESP32, then tune back up for adequate torque.
#define MOTOR_PWM_DUTY   255

// ── Simulation hardware ───────────────────────────────────────────────────────
// Onboard RGB NeoPixel pin.  Common values on ESP32-S3 dev boards:
//   GPIO48 — Espressif ESP32-S3-DevKitC-1
//   GPIO38 — some third-party WROOM-1 boards
//   GPIO39 — less common
// Change this one define until the LED lights up blue during a dispense.
#define NEOPIXEL_PIN   48

// ── Network ───────────────────────────────────────────────────────────────────
// mDNS hostname — device will be reachable at <MDNS_HOSTNAME>.local
#define MDNS_HOSTNAME        "feeder"

// ── NTP / time ────────────────────────────────────────────────────────────────
#define NTP_SERVER           "pool.ntp.org"
#define GMT_OFFSET_SEC       (-7 * 3600)   // UTC-7 (MST)
#define DAYLIGHT_OFFSET_SEC  (3600)        // +1 hr during DST

// ── Dispenser tuning ──────────────────────────────────────────────────────────
// How long to keep the motor running after the button is pressed.
// Increase to nudge the cam mechanism perfectly into its rest position.
#define BUTTON_PADDING_MS   0

// Maximum time (ms) to wait for each individual button click before aborting.
// Resets after every detected click, so dispensing N portions is allowed up
// to N × MOTOR_TIMEOUT_MS total before the safety cut-off triggers.
#define MOTOR_TIMEOUT_MS    10000

// ── Storage ───────────────────────────────────────────────────────────────────
#define SCHEDULE_FILE    "/schedule.json"
#define LOG_FILE         "/feedlog.json"
#define MAX_LOG_ENTRIES  50

// ── Webhook (failure notifications) ──────────────────────────────────────────
// Set to a full URL to enable HTTP POST notifications on dispense failures.
// Leave empty to disable (will only print to Serial).
#define WEBHOOK_URL         ""
#define WEBHOOK_TIMEOUT_MS  3000

// ── Power-loss recovery ───────────────────────────────────────────────────────
// How far back (seconds) to look for missed scheduled feeds on boot.
#define RECOVERY_LOOKBACK_SEC  (24 * 3600)
// A log entry within this many seconds of a scheduled time counts as coverage.
#define RECOVERY_WINDOW_SEC    90

// ── Clock persistence (no RTC) ────────────────────────────────────────────────
// The last known time is saved to flash so a reboot without network can resume
// from (slightly behind) the real time until NTP corrects it.
#define CLOCK_FILE               "/lastclock.json"
#define CLOCK_SAVE_INTERVAL_SEC  600
// Any epoch before this (2024-01-01 UTC) means the clock was never set.
#define MIN_VALID_EPOCH          1704067200

// ── Diagnostics log ───────────────────────────────────────────────────────────
// Rotates current -> old when current exceeds DEBUG_LOG_MAX_BYTES.
#define DEBUG_LOG_FILE        "/debuglog.txt"
#define DEBUG_LOG_OLD_FILE    "/debuglog.old"
#define DEBUG_BOOT_FILE       "/bootcount.txt"
#define DEBUG_LOG_MAX_BYTES   32768
#define DEBUG_LOG_FLUSH_MS    10000
#define HEARTBEAT_SERIAL_MS   (60UL * 1000UL)
// Stored in the diagnostics log as a "board was alive" marker.
#define HEARTBEAT_LOG_MS      (60UL * 60UL * 1000UL)
