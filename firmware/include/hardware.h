#pragma once
#include <Arduino.h>

// ─────────────────────────────────────────────────────────────────────────────
// HardwareInterface
//
// Pure-virtual abstraction over the motor output and cam-switch button input.
// Swap the concrete implementation — SimulatedHardware vs RealHardware — by
// toggling the -D SIMULATION_MODE build flag in platformio.ini. No other code
// needs to change.
// ─────────────────────────────────────────────────────────────────────────────
class HardwareInterface {
public:
    virtual ~HardwareInterface() = default;

    // Drive the motor (or its simulation) on or off.
    virtual void setMotor(bool on) = 0;

    // Returns true while the cam-switch button is pressed.
    virtual bool readButton() = 0;

    // Called once per loop() iteration.  Used by simulation for serial polling.
    virtual void update() {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Factory — returns the right implementation for the current build mode.
// Called once in setup(); the pointer is owned by main.cpp.
// ─────────────────────────────────────────────────────────────────────────────
HardwareInterface* createHardware();
