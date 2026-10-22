#include "hardware.h"
#include "config.h"

// ═════════════════════════════════════════════════════════════════════════════
//  SIMULATION MODE
//  Motor  → built-in NeoPixel (blue = running, off = stopped)
//  Button → persistent boolean state toggled by serial commands:
//              ON   — simulates the cam pressing the switch
//              OFF  — simulates the cam releasing the switch
// ═════════════════════════════════════════════════════════════════════════════
#ifdef SIMULATION_MODE

#include <Adafruit_NeoPixel.h>

static Adafruit_NeoPixel _pixel(1, NEOPIXEL_PIN, NEO_GRB + NEO_KHZ800);
static volatile bool      _simButtonState = false;
static String             _serialBuffer;

class SimulatedHardware : public HardwareInterface {
public:
    SimulatedHardware() {
        _pixel.begin();
        _pixel.setBrightness(80);
        _pixel.clear();
        _pixel.show();

        Serial.println("[SIM] Hardware simulation active.");
        Serial.println("[SIM] Serial commands: ON | OFF");
    }

    void setMotor(bool on) override {
        if (on) {
            _pixel.setPixelColor(0, _pixel.Color(0, 0, 255)); // blue
        } else {
            _pixel.clear();
        }
        _pixel.show();
        Serial.printf("[SIM] Motor %s\n", on ? "ON (LED blue)" : "OFF");
    }

    bool readButton() override {
        return _simButtonState;
    }

    // Poll serial each loop() for button-state commands.
    void update() override {
        while (Serial.available()) {
            char c = Serial.read();
            if (c == '\n' || c == '\r') {
                _serialBuffer.trim();
                if (_serialBuffer.equalsIgnoreCase("ON")) {
                    _simButtonState = true;
                    Serial.println("[SIM] Button state → ON (pressed)");
                } else if (_serialBuffer.equalsIgnoreCase("OFF")) {
                    _simButtonState = false;
                    Serial.println("[SIM] Button state → OFF (released)");
                } else if (_serialBuffer.length() > 0) {
                    Serial.printf("[SIM] Unknown command: '%s'\n", _serialBuffer.c_str());
                }
                _serialBuffer = "";
            } else {
                _serialBuffer += c;
            }
        }
    }
};

HardwareInterface* createHardware() {
    return new SimulatedHardware();
}

// ═════════════════════════════════════════════════════════════════════════════
//  REAL HARDWARE
//  Motor  → MOSFET gate on MOTOR_PIN (active HIGH)
//  Button → cam-switch on BUTTON_PIN (INPUT_PULLDOWN; HIGH when pressed)
// ═════════════════════════════════════════════════════════════════════════════
#else

// LEDC channel reserved for motor PWM
static const int MOTOR_LEDC_CHANNEL  = 0;
static const int MOTOR_LEDC_FREQ_HZ  = 20000; // 20 kHz — above audible range
static const int MOTOR_LEDC_RES_BITS = 8;      // 0-255 duty range

class RealHardware : public HardwareInterface {
public:
    RealHardware() {
        // Motor on LEDC PWM — soft-starts to reduce inrush current and
        // supply-rail droop that would otherwise trigger the brownout detector.
        ledcSetup(MOTOR_LEDC_CHANNEL, MOTOR_LEDC_FREQ_HZ, MOTOR_LEDC_RES_BITS);
        ledcAttachPin(MOTOR_PIN, MOTOR_LEDC_CHANNEL);
        ledcWrite(MOTOR_LEDC_CHANNEL, 0); // off

        // Button wired: one lead → GPIO, other lead → GND.
        // INPUT_PULLUP: HIGH when open, LOW when pressed.
        pinMode(BUTTON_PIN, INPUT_PULLUP);

        Serial.println("[HW] Real hardware initialised.");
        Serial.printf("[HW] Motor pin: %d (PWM duty %d/255) | Button pin: %d (active-low)\n",
                      MOTOR_PIN, MOTOR_PWM_DUTY, BUTTON_PIN);
    }

    void setMotor(bool on) override {
        ledcWrite(MOTOR_LEDC_CHANNEL, on ? MOTOR_PWM_DUTY : 0);
    }

    bool readButton() override {
        // Active-low: pressed = LOW
        return digitalRead(BUTTON_PIN) == LOW;
    }
};

HardwareInterface* createHardware() {
    return new RealHardware();
}

#endif // SIMULATION_MODE
