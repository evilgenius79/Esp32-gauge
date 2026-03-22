#include <Arduino.h>
#include "pins.h"
#include "gauges.h"
#include "display.h"
#include "obd2.h"
#include "encoder.h"

// =============================================================================
// ESP32 OBD2 Gauge - Main Program
//
// CrowPanel 1.28" HMI ESP32-S3 Rotary Display + M5Stack Mini CAN Unit
//
// Turn the rotary knob to switch between gauges.
// Only the currently displayed gauge polls the CAN bus.
// Press the knob to toggle between display brightness levels.
// =============================================================================

GaugeDisplay   display;
OBD2           obd2;
RotaryEncoder  encoder;

int currentGauge = 0;          // Index into GAUGES[]
float currentValue = 0.0f;     // Latest decoded value
bool needsFullRedraw = true;   // Force full gauge redraw on switch

// Brightness cycling
static const uint8_t BRIGHTNESS_LEVELS[] = { 30, 50, 70, 100 };
static const int NUM_BRIGHTNESS = sizeof(BRIGHTNESS_LEVELS) / sizeof(BRIGHTNESS_LEVELS[0]);
int brightnessIdx = 2; // Start at 70%

// Poll timing
uint32_t lastPollTime = 0;
constexpr uint32_t POLL_INTERVAL_MS = 100; // 10 Hz polling rate

// Reconnect timing
uint32_t lastReconnectTime = 0;
constexpr uint32_t RECONNECT_INTERVAL_MS = 3000;

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 OBD2 Gauge ===");
    Serial.println("CrowPanel 1.28\" + M5Stack Mini CAN");

    // Power indicator LED
    pinMode(PIN_PWR_LIGHT, OUTPUT);
    digitalWrite(PIN_PWR_LIGHT, HIGH);

    // Initialize display
    Serial.println("[INIT] Display...");
    display.begin();

    // Initialize rotary encoder
    Serial.println("[INIT] Encoder...");
    encoder.begin(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);

    // Initialize CAN bus
    Serial.println("[INIT] CAN bus...");
    if (obd2.begin(PIN_CAN_TX, PIN_CAN_RX)) {
        Serial.println("[INIT] CAN bus OK");
    } else {
        Serial.println("[INIT] CAN bus FAILED - check wiring");
    }

    // Draw initial gauge
    Serial.printf("[INIT] Starting with gauge: %s\n", GAUGES[currentGauge].name);
    currentValue = GAUGES[currentGauge].minVal;
    display.drawGauge(GAUGES[currentGauge], currentValue, true);

    Serial.println("[INIT] Ready! Turn knob to switch gauges.");
}

void loop() {
    // --- Handle encoder rotation (gauge switching) ---
    int dir = encoder.getDirection();
    if (dir != 0) {
        currentGauge += dir;
        // Wrap around
        if (currentGauge >= NUM_GAUGES) currentGauge = 0;
        if (currentGauge < 0) currentGauge = NUM_GAUGES - 1;

        Serial.printf("[ENC] Switched to: %s (PID 0x%02X)\n",
                      GAUGES[currentGauge].name, GAUGES[currentGauge].pid);

        currentValue = GAUGES[currentGauge].minVal;
        needsFullRedraw = true;
    }

    // --- Handle button press (brightness cycling) ---
    if (encoder.wasPressed()) {
        brightnessIdx = (brightnessIdx + 1) % NUM_BRIGHTNESS;
        display.setBrightness(BRIGHTNESS_LEVELS[brightnessIdx]);
        Serial.printf("[BTN] Brightness: %d%%\n", BRIGHTNESS_LEVELS[brightnessIdx]);
    }

    // --- Poll OBD2 for the active gauge only ---
    uint32_t now = millis();
    if (now - lastPollTime >= POLL_INTERVAL_MS) {
        lastPollTime = now;

        const GaugeConfig& gauge = GAUGES[currentGauge];
        uint8_t dataA = 0, dataB = 0;

        if (obd2.requestPID(gauge.mode, gauge.pid, &dataA, &dataB, 50)) {
            currentValue = decodeOBD2(gauge, dataA, dataB);
        }

        // Update display
        display.drawGauge(gauge, currentValue, needsFullRedraw);
        needsFullRedraw = false;

        // If disconnected, force redraw next frame to show status
        if (!obd2.isConnected()) {
            display.drawNoCanStatus();
        }
    }
}
