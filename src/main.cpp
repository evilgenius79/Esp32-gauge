#include <Arduino.h>
#include "pins.h"
#include "gauges.h"
#include "display.h"
#include "obd2.h"

// =============================================================================
// ESP32 OBD2 Gauge — Dual-target main
//
// CrowPanel 1.28": Single gauge, rotary encoder, DTC diagnostics
// M5Stack Tab5:    Four-gauge 2x2 dashboard, touch to cycle gauges
// =============================================================================

GaugeDisplay display;
OBD2         obd2;

// Shared: sleep on CAN bus inactivity
uint32_t lastCanDataTime = 0;
constexpr uint32_t SLEEP_TIMEOUT_MS = 30000;  // 30 seconds
bool sleeping = false;

// Poll timing
uint32_t lastPollTime = 0;
constexpr uint32_t POLL_INTERVAL_MS = 100;     // 10 Hz


// #############################################################################
//  TAB5 — 4-gauge dashboard with touch input
// #############################################################################

#ifdef TARGET_TAB5

#include <M5Unified.h>

int  gaugeIndices[4] = {0, 1, 3, 4};  // RPM, Speed, Boost, Throttle
float gaugeValues[4] = {0, 0, 0, 0};
bool needsFullRedraw = true;
int  pollSlot = 0;  // Round-robin: poll one gauge per cycle

// DTC state machine (Tab5)
enum Tab5State {
    T5_GAUGE,
    T5_DTC_MENU,
    T5_DTC_SCAN,
    T5_DTC_RESULTS,
    T5_DTC_CLEAR,
    T5_DTC_CLEARED,
};

Tab5State tab5State = T5_GAUGE;
DTC dtcList[MAX_DTCS];
int dtcCount = 0;
int dtcScrollOffset = 0;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    Serial.println("\n=== ESP32 OBD2 Multi-Gauge (Tab5) ===");

    display.begin();

    Serial.println("[INIT] CAN bus...");
    if (obd2.begin(PIN_CAN_TX, PIN_CAN_RX)) {
        Serial.println("[INIT] CAN bus OK");
    } else {
        Serial.println("[INIT] CAN bus FAILED - check wiring");
    }

    // Init values to gauge minimums
    for (int i = 0; i < 4; i++) {
        gaugeValues[i] = GAUGES[gaugeIndices[i]].minVal;
    }

    display.drawAllGauges(gaugeIndices, gaugeValues, true);
    display.drawDTCButton();
    Serial.println("[INIT] Ready! Tap gauge to cycle, tap DTC for diagnostics.");
}

void loop() {
    M5.update();

    switch (tab5State) {

    case T5_GAUGE: {
        // Check DTC button first
        if (display.dtcButtonTapped()) {
            if (sleeping) {
                sleeping = false;
                display.setBrightness(70);
                needsFullRedraw = true;
                lastCanDataTime = millis();
                Serial.println("[SLEEP] Waking up (DTC button)");
            } else {
                Serial.println("[DTC] Opening diagnostics menu");
                tab5State = T5_DTC_MENU;
                display.clearScreen();
                display.drawDTCMenuTab5(-1);
                break;
            }
        }

        // Touch: tap a gauge to cycle its PID
        int tapped = display.touchedGauge();
        if (tapped >= 0) {
            if (sleeping) {
                sleeping = false;
                display.setBrightness(70);
                needsFullRedraw = true;
                lastCanDataTime = millis();
                Serial.println("[SLEEP] Waking up (touch)");
            } else {
                gaugeIndices[tapped] = (gaugeIndices[tapped] + 1) % NUM_GAUGES;
                gaugeValues[tapped] = GAUGES[gaugeIndices[tapped]].minVal;
                Serial.printf("[TOUCH] Slot %d -> %s\n", tapped, GAUGES[gaugeIndices[tapped]].name);
                display.drawSingleGauge(tapped, gaugeIndices[tapped], gaugeValues[tapped], true);
                display.drawDTCButton();
            }
        }

        // Poll one gauge per cycle (round-robin, ~2.5 Hz per gauge)
        uint32_t now = millis();
        if (now - lastPollTime >= POLL_INTERVAL_MS) {
            lastPollTime = now;

            const GaugeConfig& g = GAUGES[gaugeIndices[pollSlot]];
            uint8_t a = 0, b = 0;

            if (obd2.requestPID(g.mode, g.pid, &a, &b, 50)) {
                gaugeValues[pollSlot] = decodeOBD2(g, a, b);
                lastCanDataTime = now;

                if (sleeping) {
                    sleeping = false;
                    display.setBrightness(70);
                    needsFullRedraw = true;
                    Serial.println("[SLEEP] Waking up (CAN data)");
                }
            }

            if (!sleeping) {
                if (needsFullRedraw) {
                    display.clearScreen();
                    display.drawAllGauges(gaugeIndices, gaugeValues, true);
                    display.drawDTCButton();
                    needsFullRedraw = false;
                } else {
                    display.drawSingleGauge(pollSlot, gaugeIndices[pollSlot],
                                            gaugeValues[pollSlot], false);
                    display.drawDTCButton();
                }

                if (now - lastCanDataTime > SLEEP_TIMEOUT_MS && lastCanDataTime > 0) {
                    sleeping = true;
                    display.setBrightness(0);
                    Serial.println("[SLEEP] No CAN data - going to sleep");
                }
            }

            pollSlot = (pollSlot + 1) % 4;
        }
        break;
    }

    case T5_DTC_MENU: {
        int item = display.dtcMenuTapped();
        if (item == 0) {
            Serial.println("[DTC] Scanning for trouble codes...");
            display.drawDTCScanningTab5();
            tab5State = T5_DTC_SCAN;
        } else if (item == 1) {
            Serial.println("[DTC] Clearing trouble codes...");
            display.drawDTCClearingTab5();
            tab5State = T5_DTC_CLEAR;
        } else if (item == 2) {
            Serial.println("[DTC] Returning to gauges");
            tab5State = T5_GAUGE;
            needsFullRedraw = true;
        }
        break;
    }

    case T5_DTC_SCAN: {
        dtcCount = obd2.scanDTCs(dtcList, MAX_DTCS);
        if (dtcCount < 0) dtcCount = 0;

        Serial.printf("[DTC] Found %d codes\n", dtcCount);
        for (int i = 0; i < dtcCount; i++) {
            Serial.printf("[DTC]   %s\n", dtcList[i].code);
        }

        dtcScrollOffset = 0;
        display.drawDTCResultsTab5(dtcList, dtcCount, dtcScrollOffset);
        tab5State = T5_DTC_RESULTS;
        break;
    }

    case T5_DTC_RESULTS: {
        int action = display.dtcResultsScrollOrBack();
        if (action == -2) {
            // Back to menu
            tab5State = T5_DTC_MENU;
            display.clearScreen();
            display.drawDTCMenuTab5(-1);
        } else if (action == -1 && dtcScrollOffset > 0) {
            dtcScrollOffset--;
            display.drawDTCResultsTab5(dtcList, dtcCount, dtcScrollOffset);
        } else if (action == 1 && dtcCount > 8 && dtcScrollOffset < dtcCount - 8) {
            dtcScrollOffset++;
            display.drawDTCResultsTab5(dtcList, dtcCount, dtcScrollOffset);
        }
        break;
    }

    case T5_DTC_CLEAR: {
        bool ok = obd2.clearDTCs();
        Serial.printf("[DTC] Clear %s\n", ok ? "OK" : "FAILED");
        display.drawDTCClearedTab5(ok);
        tab5State = T5_DTC_CLEARED;
        break;
    }

    case T5_DTC_CLEARED: {
        if (display.dtcBackTapped()) {
            tab5State = T5_DTC_MENU;
            display.clearScreen();
            display.drawDTCMenuTab5(-1);
        }
        break;
    }

    } // end switch
}


// #############################################################################
//  CROWPANEL — Single gauge with rotary encoder + DTC diagnostics
// #############################################################################

#else // CrowPanel

#include "encoder.h"

RotaryEncoder encoder;

int   currentGauge = 0;
float currentValue = 0.0f;
bool  needsFullRedraw = true;

// Menu state machine
enum AppState {
    STATE_GAUGE,
    STATE_DTC_MENU,
    STATE_DTC_SCAN,
    STATE_DTC_RESULTS,
    STATE_DTC_CLEAR,
    STATE_DTC_CLEARED,
};

AppState appState = STATE_GAUGE;
int menuSelection = 0;
DTC dtcList[MAX_DTCS];
int dtcCount = 0;
int dtcScrollOffset = 0;

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== ESP32 OBD2 Gauge ===");
    Serial.println("CrowPanel 1.28\" + M5Stack Mini CAN");

    pinMode(PIN_PWR_LIGHT, OUTPUT);
    digitalWrite(PIN_PWR_LIGHT, HIGH);

    Serial.println("[INIT] Display...");
    display.begin();

    Serial.println("[INIT] Encoder...");
    encoder.begin(PIN_ENC_A, PIN_ENC_B, PIN_ENC_SW);

    Serial.println("[INIT] CAN bus...");
    if (obd2.begin(PIN_CAN_TX, PIN_CAN_RX)) {
        Serial.println("[INIT] CAN bus OK");
    } else {
        Serial.println("[INIT] CAN bus FAILED - check wiring");
    }

    Serial.printf("[INIT] Starting with gauge: %s\n", GAUGES[currentGauge].name);
    currentValue = GAUGES[currentGauge].minVal;
    display.drawGauge(GAUGES[currentGauge], currentValue, true);

    Serial.println("[INIT] Ready! Turn knob to switch gauges, press for diagnostics.");
}

void loop() {
    int dir = encoder.getDirection();
    bool pressed = encoder.wasPressed();

    // Wake from sleep on any input
    if (sleeping && (dir != 0 || pressed)) {
        sleeping = false;
        display.setBrightness(70);
        needsFullRedraw = true;
        lastCanDataTime = millis();
        Serial.println("[SLEEP] Waking up (user input)");
        dir = 0;
        pressed = false;
    }

    switch (appState) {

    case STATE_GAUGE: {
        if (dir != 0) {
            currentGauge += dir;
            if (currentGauge >= NUM_GAUGES) currentGauge = 0;
            if (currentGauge < 0) currentGauge = NUM_GAUGES - 1;

            Serial.printf("[ENC] Switched to: %s (PID 0x%02X)\n",
                          GAUGES[currentGauge].name, GAUGES[currentGauge].pid);
            currentValue = GAUGES[currentGauge].minVal;
            needsFullRedraw = true;
        }

        if (pressed) {
            Serial.println("[MENU] Opening diagnostics menu");
            appState = STATE_DTC_MENU;
            menuSelection = 0;
            display.drawDTCMenu(menuSelection);
            break;
        }

        uint32_t now = millis();
        if (now - lastPollTime >= POLL_INTERVAL_MS) {
            lastPollTime = now;

            const GaugeConfig& gauge = GAUGES[currentGauge];
            uint8_t dataA = 0, dataB = 0;

            if (obd2.requestPID(gauge.mode, gauge.pid, &dataA, &dataB, 50)) {
                currentValue = decodeOBD2(gauge, dataA, dataB);
                lastCanDataTime = now;

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

                if (now - lastCanDataTime > SLEEP_TIMEOUT_MS && lastCanDataTime > 0) {
                    sleeping = true;
                    display.setBrightness(0);
                    Serial.println("[SLEEP] No CAN data - going to sleep");
                }
            }
        }
        break;
    }

    case STATE_DTC_MENU: {
        if (dir != 0) {
            menuSelection += dir;
            if (menuSelection < 0) menuSelection = 2;
            if (menuSelection > 2) menuSelection = 0;
            display.drawDTCMenu(menuSelection);
        }

        if (pressed) {
            if (menuSelection == 0) {
                Serial.println("[DTC] Scanning for trouble codes...");
                display.drawDTCScanning();
                appState = STATE_DTC_SCAN;
            } else if (menuSelection == 1) {
                Serial.println("[DTC] Clearing trouble codes...");
                display.drawDTCClearing();
                appState = STATE_DTC_CLEAR;
            } else {
                Serial.println("[MENU] Returning to gauge");
                appState = STATE_GAUGE;
                needsFullRedraw = true;
            }
        }
        break;
    }

    case STATE_DTC_SCAN: {
        dtcCount = obd2.scanDTCs(dtcList, MAX_DTCS);
        if (dtcCount < 0) dtcCount = 0;

        Serial.printf("[DTC] Found %d codes\n", dtcCount);
        for (int i = 0; i < dtcCount; i++) {
            Serial.printf("[DTC]   %s\n", dtcList[i].code);
        }

        dtcScrollOffset = 0;
        display.drawDTCResults(dtcList, dtcCount, dtcScrollOffset);
        appState = STATE_DTC_RESULTS;
        break;
    }

    case STATE_DTC_RESULTS: {
        if (dir != 0 && dtcCount > 5) {
            dtcScrollOffset += dir;
            if (dtcScrollOffset < 0) dtcScrollOffset = 0;
            if (dtcScrollOffset > dtcCount - 5) dtcScrollOffset = dtcCount - 5;
            display.drawDTCResults(dtcList, dtcCount, dtcScrollOffset);
        }

        if (pressed) {
            appState = STATE_DTC_MENU;
            menuSelection = 0;
            display.drawDTCMenu(menuSelection);
        }
        break;
    }

    case STATE_DTC_CLEAR: {
        bool ok = obd2.clearDTCs();
        Serial.printf("[DTC] Clear %s\n", ok ? "OK" : "FAILED");
        display.drawDTCCleared(ok);
        appState = STATE_DTC_CLEARED;
        break;
    }

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

#endif // CrowPanel
