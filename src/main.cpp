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
// Press the knob to open the diagnostics menu (scan/clear DTCs).
// =============================================================================

GaugeDisplay   display;
OBD2           obd2;
RotaryEncoder  encoder;

int currentGauge = 0;          // Index into GAUGES[]
float currentValue = 0.0f;     // Latest decoded value
bool needsFullRedraw = true;   // Force full gauge redraw on switch

// Poll timing
uint32_t lastPollTime = 0;
constexpr uint32_t POLL_INTERVAL_MS = 100; // 10 Hz polling rate

// Sleep: turn off backlight after no CAN data for this long
uint32_t lastCanDataTime = 0;
constexpr uint32_t SLEEP_TIMEOUT_MS = 30000; // 30 seconds
bool sleeping = false;

// =============================================================================
// Menu state machine
// =============================================================================
enum AppState {
    STATE_GAUGE,        // Normal gauge display
    STATE_DTC_MENU,     // DTC menu shown
    STATE_DTC_SCAN,     // Scanning in progress
    STATE_DTC_RESULTS,  // Showing scan results
    STATE_DTC_CLEAR,    // Clearing in progress
    STATE_DTC_CLEARED,  // Showing clear result
};

AppState appState = STATE_GAUGE;
int menuSelection = 0;
DTC dtcList[MAX_DTCS];
int dtcCount = 0;

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

    Serial.println("[INIT] Ready! Turn knob to switch gauges, press for diagnostics.");
}

void loop() {
    int dir = encoder.getDirection();
    bool pressed = encoder.wasPressed();

    // --- Wake from sleep on any input or CAN data ---
    if (sleeping && (dir != 0 || pressed)) {
        sleeping = false;
        display.setBrightness(70);
        needsFullRedraw = true;
        lastCanDataTime = millis();
        Serial.println("[SLEEP] Waking up (user input)");
        // Consume the input so it doesn't also trigger menu/gauge switch
        dir = 0;
        pressed = false;
    }

    switch (appState) {

    // =========================================================================
    // Normal gauge view
    // =========================================================================
    case STATE_GAUGE: {
        // Encoder rotation → switch gauge
        if (dir != 0) {
            currentGauge += dir;
            if (currentGauge >= NUM_GAUGES) currentGauge = 0;
            if (currentGauge < 0) currentGauge = NUM_GAUGES - 1;

            Serial.printf("[ENC] Switched to: %s (PID 0x%02X)\n",
                          GAUGES[currentGauge].name, GAUGES[currentGauge].pid);
            currentValue = GAUGES[currentGauge].minVal;
            needsFullRedraw = true;
        }

        // Button press → open DTC menu
        if (pressed) {
            Serial.println("[MENU] Opening diagnostics menu");
            appState = STATE_DTC_MENU;
            menuSelection = 0;
            display.drawDTCMenu(menuSelection);
            break;
        }

        // Poll OBD2
        uint32_t now = millis();
        if (now - lastPollTime >= POLL_INTERVAL_MS) {
            lastPollTime = now;

            const GaugeConfig& gauge = GAUGES[currentGauge];
            uint8_t dataA = 0, dataB = 0;

            if (obd2.requestPID(gauge.mode, gauge.pid, &dataA, &dataB, 50)) {
                currentValue = decodeOBD2(gauge, dataA, dataB);
                lastCanDataTime = now;

                // Wake up if we were sleeping
                if (sleeping) {
                    sleeping = false;
                    display.setBrightness(70);
                    needsFullRedraw = true;
                    Serial.println("[SLEEP] Waking up (CAN data)");
                }
            }

            if (!sleeping) {
                display.drawGauge(gauge, currentValue, needsFullRedraw);
                needsFullRedraw = false;

                if (!obd2.isConnected()) {
                    display.drawNoCanStatus();
                }

                // Sleep if no CAN data for too long
                if (now - lastCanDataTime > SLEEP_TIMEOUT_MS && lastCanDataTime > 0) {
                    sleeping = true;
                    display.setBrightness(0);
                    Serial.println("[SLEEP] No CAN data - going to sleep");
                }
            }
        }
        break;
    }

    // =========================================================================
    // DTC menu — rotate to select, press to confirm
    // =========================================================================
    case STATE_DTC_MENU: {
        if (dir != 0) {
            menuSelection += dir;
            if (menuSelection < 0) menuSelection = 2;
            if (menuSelection > 2) menuSelection = 0;
            display.drawDTCMenu(menuSelection);
        }

        if (pressed) {
            if (menuSelection == 0) {
                // Scan codes
                Serial.println("[DTC] Scanning for trouble codes...");
                display.drawDTCScanning();
                appState = STATE_DTC_SCAN;
            } else if (menuSelection == 1) {
                // Clear codes
                Serial.println("[DTC] Clearing trouble codes...");
                display.drawDTCClearing();
                appState = STATE_DTC_CLEAR;
            } else {
                // Back
                Serial.println("[MENU] Returning to gauge");
                appState = STATE_GAUGE;
                needsFullRedraw = true;
            }
        }
        break;
    }

    // =========================================================================
    // Scanning — runs once, then shows results
    // =========================================================================
    case STATE_DTC_SCAN: {
        dtcCount = obd2.scanDTCs(dtcList, MAX_DTCS);
        if (dtcCount < 0) dtcCount = 0;

        Serial.printf("[DTC] Found %d codes\n", dtcCount);
        for (int i = 0; i < dtcCount; i++) {
            Serial.printf("[DTC]   %s\n", dtcList[i].code);
        }

        display.drawDTCResults(dtcList, dtcCount);
        appState = STATE_DTC_RESULTS;
        break;
    }

    // =========================================================================
    // Showing results — press to go back to menu
    // =========================================================================
    case STATE_DTC_RESULTS: {
        if (pressed) {
            appState = STATE_DTC_MENU;
            menuSelection = 0;
            display.drawDTCMenu(menuSelection);
        }
        break;
    }

    // =========================================================================
    // Clearing — runs once, then shows result
    // =========================================================================
    case STATE_DTC_CLEAR: {
        bool ok = obd2.clearDTCs();
        Serial.printf("[DTC] Clear %s\n", ok ? "OK" : "FAILED");
        display.drawDTCCleared(ok);
        appState = STATE_DTC_CLEARED;
        break;
    }

    // =========================================================================
    // Showing clear result — press to go back to menu
    // =========================================================================
    case STATE_DTC_CLEARED: {
        if (pressed) {
            appState = STATE_DTC_MENU;
            menuSelection = 0;
            display.drawDTCMenu(menuSelection);
        }
        break;
    }

    } // end switch
}
