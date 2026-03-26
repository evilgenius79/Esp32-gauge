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
#include <cstring>
#include "datalog.h"

int  gaugeIndices[4] = {0, 1, 3, 4};  // RPM, Speed, Boost, Throttle
float gaugeValues[4] = {0, 0, 0, 0};
bool needsFullRedraw = true;
int  pollSlot = 0;  // Round-robin: poll one gauge per cycle

// Peak/min hold tracking
float peakValues[4]  = {0, 0, 0, 0};    // Max recorded per slot
float minValues[4]   = {0, 0, 0, 0};    // Min recorded per slot
bool  peakHoldEnabled = false;           // Toggle via DTC menu or editor

// Fullscreen single-gauge mode
int fullscreenSlot = -1;  // -1 = normal 2x2 mode, 0-3 = single slot expanded

// Data logger
DataLogger dataLogger;

// OBD polling — disabled by default, enable from tools menu
bool obdPollingEnabled = false;

// State machine
enum Tab5State {
    T5_GAUGE,
    T5_DTC_MENU,
    T5_DTC_SCAN,
    T5_DTC_RESULTS,
    T5_DTC_CLEAR,
    T5_DTC_CLEARED,
    T5_EDITOR,
    T5_KEYPAD,
    T5_FORMULA_PICKER,
    T5_FULLSCREEN,
    T5_PID_SCAN,
    T5_PID_RESULTS,
};

Tab5State tab5State = T5_GAUGE;
DTC dtcList[MAX_DTCS];
int dtcCount = 0;
int dtcScrollOffset = 0;

// PID discovery results
uint8_t supportedPIDs[32];  // Bitmap: PIDs 0x01-0xFF
int numSupportedPIDs = 0;

// Editor state
int editSlot = -1;         // Which display slot (0-3) is being edited
int editGaugeIdx = -1;     // Which gauge index is being edited
int editField = -1;        // Currently selected field
GaugeConfig editCopy;      // Working copy for editing
char keypadBuf[24];        // Keypad input buffer
const char* keypadTitle;   // Current keypad field title

// Double-tap detection for fullscreen
uint32_t lastTapTime = 0;
int lastTapSlot = -1;
constexpr uint32_t DOUBLE_TAP_MS = 400;

void setup() {
    auto cfg = M5.config();
    M5.begin(cfg);

    Serial.begin(115200);
    Serial.println("\n=== ESP32 OBD2 Multi-Gauge (Tab5) ===");

    loadGaugeConfigs();
    display.begin();

    // CAN bus NOT initialized here — user enables OBD polling from tools menu
    Serial.println("[INIT] CAN bus deferred — enable from tools menu");

    for (int i = 0; i < 4; i++) {
        gaugeValues[i] = GAUGES[gaugeIndices[i]].minVal;
        peakValues[i] = gaugeValues[i];
        minValues[i] = gaugeValues[i];
    }

    // Initialize SD card data logger
    dataLogger.begin();

    display.drawAllGauges(gaugeIndices, gaugeValues, true);
    display.drawDTCButton();
    Serial.println("[INIT] Ready! Tap=cycle, long-press=edit, DTC=diagnostics");
}

// Helper: apply keypad result to the editCopy field
static void applyKeypadToField(int field, const char* buf, GaugeConfig& gc) {
    switch (field) {
        case 0:  strncpy(gc.name, buf, sizeof(gc.name) - 1); gc.name[sizeof(gc.name)-1] = '\0'; break;
        case 1:  strncpy(gc.units, buf, sizeof(gc.units) - 1); gc.units[sizeof(gc.units)-1] = '\0'; break;
        case 2:  strncpy(gc.scaleLabel, buf, sizeof(gc.scaleLabel) - 1); gc.scaleLabel[sizeof(gc.scaleLabel)-1] = '\0'; break;
        case 3:  gc.mode = (uint8_t)strtol(buf, nullptr, 16); break;
        case 4:  gc.pid  = (uint16_t)strtol(buf, nullptr, 16); break;
        case 5:  gc.minVal        = atof(buf); break;
        case 6:  gc.maxVal        = atof(buf); break;
        case 7:  gc.warnVal       = atof(buf); break;
        case 8:  gc.dangerVal     = atof(buf); break;
        case 9:  gc.scaleDivisor  = atof(buf); break;
        case 10: gc.majorDivisions = atoi(buf); break;
        case 11: gc.dataBytes     = (uint8_t)atoi(buf); break;
        case 12: gc.decimals      = (uint8_t)atoi(buf); break;
        // case 13 (formula) is handled by formula picker, not keypad
    }
}

// Helper: get current field value as string for keypad
static void fieldToString(int field, const GaugeConfig& gc, char* buf, int bufLen) {
    switch (field) {
        case 0:  snprintf(buf, bufLen, "%s", gc.name); break;
        case 1:  snprintf(buf, bufLen, "%s", gc.units); break;
        case 2:  snprintf(buf, bufLen, "%s", gc.scaleLabel); break;
        case 3:  snprintf(buf, bufLen, "%02X", gc.mode); break;
        case 4:  snprintf(buf, bufLen, "%04X", gc.pid); break;
        case 5:  snprintf(buf, bufLen, "%.1f", gc.minVal); break;
        case 6:  snprintf(buf, bufLen, "%.1f", gc.maxVal); break;
        case 7:  snprintf(buf, bufLen, "%.1f", gc.warnVal); break;
        case 8:  snprintf(buf, bufLen, "%.1f", gc.dangerVal); break;
        case 9:  snprintf(buf, bufLen, "%.1f", gc.scaleDivisor); break;
        case 10: snprintf(buf, bufLen, "%d", gc.majorDivisions); break;
        case 11: snprintf(buf, bufLen, "%d", gc.dataBytes); break;
        case 12: snprintf(buf, bufLen, "%d", gc.decimals); break;
    }
}

static const char* FIELD_NAMES[] = {
    "NAME", "UNITS", "LABEL", "MODE (hex)", "PID (hex)",
    "MIN VALUE", "MAX VALUE", "WARNING", "DANGER",
    "SCALE DIVISOR", "MAJOR DIVS", "DATA BYTES", "DECIMALS", "FORMULA"
};

void loop() {
    M5.update();

    switch (tab5State) {

    case T5_GAUGE: {
        auto action = display.pollTouch();

        if (action == GaugeDisplay::TOUCH_DTC_BUTTON) {
            if (sleeping) {
                sleeping = false;
                display.setBrightness(70);
                needsFullRedraw = true;
                lastCanDataTime = millis();
            } else {
                Serial.println("[DTC] Opening diagnostics menu");
                tab5State = T5_DTC_MENU;
                display.clearScreen();
                display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
                break;
            }
        }

        if (action == GaugeDisplay::TOUCH_GAUGE_TAP && display.touchSlot >= 0) {
            int slot = display.touchSlot;
            if (sleeping) {
                sleeping = false;
                display.setBrightness(70);
                needsFullRedraw = true;
                lastCanDataTime = millis();
            } else {
                uint32_t now = millis();
                // Double-tap detection: same slot within DOUBLE_TAP_MS = fullscreen
                if (slot == lastTapSlot && (now - lastTapTime) < DOUBLE_TAP_MS) {
                    fullscreenSlot = slot;
                    tab5State = T5_FULLSCREEN;
                    display.clearScreen();
                    display.drawFullscreenGauge(gaugeIndices[slot],
                                                 gaugeValues[slot], -1, true);
                    lastTapSlot = -1;
                    Serial.printf("[FULL] Fullscreen slot %d\n", slot);
                    break;
                }
                lastTapSlot = slot;
                lastTapTime = now;
                // Single tap: cycle gauge
                gaugeIndices[slot] = (gaugeIndices[slot] + 1) % NUM_GAUGES;
                gaugeValues[slot] = GAUGES[gaugeIndices[slot]].minVal;
                peakValues[slot] = gaugeValues[slot];
                minValues[slot] = gaugeValues[slot];
                Serial.printf("[TOUCH] Slot %d -> %s\n", slot, GAUGES[gaugeIndices[slot]].name);
                display.drawSingleGauge(slot, gaugeIndices[slot], gaugeValues[slot], true);
                display.drawDTCButton();
            }
        }

        if (action == GaugeDisplay::TOUCH_GAUGE_LONGPRESS && display.touchSlot >= 0) {
            editSlot = display.touchSlot;
            editGaugeIdx = gaugeIndices[editSlot];
            memcpy(&editCopy, &GAUGES[editGaugeIdx], sizeof(GaugeConfig));
            editField = -1;
            Serial.printf("[EDIT] Long-press on slot %d, editing gauge %s\n",
                          editSlot, editCopy.name);
            tab5State = T5_EDITOR;
            display.clearScreen();
            display.drawGaugeEditor(editSlot, editCopy, editField);
            break;
        }

        // Redraw gauges and optionally poll OBD
        uint32_t now = millis();
        if (now - lastPollTime >= POLL_INTERVAL_MS) {
            lastPollTime = now;

            // Only poll CAN bus when OBD is enabled
            if (obdPollingEnabled) {
                const GaugeConfig& g = GAUGES[gaugeIndices[pollSlot]];
                uint8_t a = 0, b = 0;

                if (obd2.requestPID(g.mode, g.pid, &a, &b, 50)) {
                    float val = decodeOBD2(g, a, b);
                    gaugeValues[pollSlot] = val;
                    lastCanDataTime = now;

                    // Peak/min tracking
                    if (val > peakValues[pollSlot]) peakValues[pollSlot] = val;
                    if (val < minValues[pollSlot])  minValues[pollSlot] = val;

                    // CSV data logging
                    if (dataLogger.isLogging()) {
                        dataLogger.logRow(gaugeIndices, gaugeValues, now);
                    }

                    if (sleeping) {
                        sleeping = false;
                        display.setBrightness(70);
                        needsFullRedraw = true;
                    }
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

                if (obdPollingEnabled && now - lastCanDataTime > SLEEP_TIMEOUT_MS && lastCanDataTime > 0) {
                    sleeping = true;
                    display.setBrightness(0);
                }
            }

            pollSlot = (pollSlot + 1) % 4;
        }
        break;
    }

    case T5_EDITOR: {
        int field = display.editorFieldTapped();
        if (field >= 0 && field <= 12) {
            // Open keypad for fields 0-12
            editField = field;
            fieldToString(field, editCopy, keypadBuf, sizeof(keypadBuf));
            keypadTitle = FIELD_NAMES[field];
            tab5State = T5_KEYPAD;
            display.clearScreen();
            display.drawEditorKeypad(keypadTitle, keypadBuf);
        } else if (field == 13) {
            // Open formula picker for FORMULA field
            editField = 13;
            tab5State = T5_FORMULA_PICKER;
            display.clearScreen();
            display.drawFormulaPicker(editCopy.formula);
        } else if (field == 99) {
            // SAVE
            memcpy(&GAUGES[editGaugeIdx], &editCopy, sizeof(GaugeConfig));
            saveGaugeConfig(editGaugeIdx);
            Serial.printf("[EDIT] Saved gauge %d: %s\n", editGaugeIdx, editCopy.name);
            // Reset gauge value so display shows new min properly
            gaugeValues[editSlot] = editCopy.minVal;
            // Force display to treat this as a new gauge (invalidate cache)
            display.invalidateSlot(editSlot);
            tab5State = T5_GAUGE;
            needsFullRedraw = true;
        } else if (field == 98) {
            // RESET to default
            resetGaugeConfig(editGaugeIdx);
            memcpy(&editCopy, &GAUGES[editGaugeIdx], sizeof(GaugeConfig));
            display.drawGaugeEditor(editSlot, editCopy, -1);
        } else if (field == 97) {
            // CANCEL
            Serial.println("[EDIT] Cancelled");
            tab5State = T5_GAUGE;
            needsFullRedraw = true;
        } else if (field == 96) {
            // CAN SPEED toggle: 250 <-> 500
            canBusSpeed = (canBusSpeed == 500) ? 250 : 500;
            saveCanSpeed();
            Serial.printf("[EDIT] CAN speed toggled to %lu kbps\n", canBusSpeed);
            display.clearScreen();
            display.drawGaugeEditor(editSlot, editCopy, -1);
        }
        break;
    }

    case T5_KEYPAD: {
        int result = display.keypadTapped(keypadBuf, sizeof(keypadBuf));
        if (result == 0) {
            // Key pressed — redraw with updated value
            display.drawEditorKeypad(keypadTitle, keypadBuf);
        } else if (result == 1) {
            // OK — apply value and return to editor
            applyKeypadToField(editField, keypadBuf, editCopy);
            tab5State = T5_EDITOR;
            display.clearScreen();
            display.drawGaugeEditor(editSlot, editCopy, editField);
        }
        break;
    }

    case T5_FORMULA_PICKER: {
        int picked = display.formulaPickerTapped();
        if (picked >= 0) {
            editCopy.formula = (DecodeFormula)picked;
            Serial.printf("[EDIT] Formula set to %d: %s\n", picked, DECODE_NAMES[picked]);
            tab5State = T5_EDITOR;
            display.clearScreen();
            display.drawGaugeEditor(editSlot, editCopy, 13);
        }
        break;
    }

    case T5_DTC_MENU: {
        int item = display.dtcMenuTapped();
        if (item == 0) {
            // Toggle OBD connect/disconnect
            if (!obdPollingEnabled) {
                Serial.println("[OBD] Connecting CAN bus...");
                if (!obd2.isConnected()) {
                    obd2.begin(PIN_CAN_TX, PIN_CAN_RX);
                }
                obdPollingEnabled = true;
                Serial.println("[OBD] Polling enabled");
            } else {
                obdPollingEnabled = false;
                Serial.println("[OBD] Polling disabled");
            }
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
        } else if (item == 1) {
            // Ensure CAN is initialized for DTC scan
            if (!obdPollingEnabled) obd2.begin(PIN_CAN_TX, PIN_CAN_RX);
            display.drawDTCScanningTab5();
            tab5State = T5_DTC_SCAN;
        } else if (item == 2) {
            // Ensure CAN is initialized for DTC clear
            if (!obdPollingEnabled) obd2.begin(PIN_CAN_TX, PIN_CAN_RX);
            display.drawDTCClearingTab5();
            tab5State = T5_DTC_CLEAR;
        } else if (item == 3) {
            // Toggle CSV data logging
            if (dataLogger.isAvailable()) {
                if (dataLogger.isLogging()) {
                    dataLogger.stopSession();
                    Serial.println("[LOG] Logging stopped");
                } else {
                    dataLogger.startSession();
                    Serial.println("[LOG] Logging started");
                }
            } else {
                Serial.println("[LOG] No SD card");
            }
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
        } else if (item == 4) {
            // Toggle peak/min hold
            peakHoldEnabled = !peakHoldEnabled;
            if (!peakHoldEnabled) {
                for (int i = 0; i < 4; i++) {
                    peakValues[i] = gaugeValues[i];
                    minValues[i] = gaugeValues[i];
                }
            }
            Serial.printf("[PEAK] Peak hold %s\n", peakHoldEnabled ? "ON" : "OFF");
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
        } else if (item == 5) {
            // PID auto-discovery scan
            if (!obdPollingEnabled) obd2.begin(PIN_CAN_TX, PIN_CAN_RX);
            display.clearScreen();
            display.drawPIDScanningTab5();
            tab5State = T5_PID_SCAN;
        } else if (item == 6) {
            tab5State = T5_GAUGE;
            needsFullRedraw = true;
        }
        break;
    }

    case T5_DTC_SCAN: {
        dtcCount = obd2.scanDTCs(dtcList, MAX_DTCS);
        if (dtcCount < 0) dtcCount = 0;
        dtcScrollOffset = 0;
        display.drawDTCResultsTab5(dtcList, dtcCount, dtcScrollOffset);
        tab5State = T5_DTC_RESULTS;
        break;
    }

    case T5_DTC_RESULTS: {
        int action = display.dtcResultsScrollOrBack();
        if (action == -2) {
            tab5State = T5_DTC_MENU;
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
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
        display.drawDTCClearedTab5(ok);
        tab5State = T5_DTC_CLEARED;
        break;
    }

    case T5_DTC_CLEARED: {
        if (display.dtcBackTapped()) {
            tab5State = T5_DTC_MENU;
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
        }
        break;
    }

    case T5_PID_SCAN: {
        // Scan supported Mode 01 PIDs using PID 0x00, 0x20, 0x40, 0x60
        numSupportedPIDs = 0;
        memset(supportedPIDs, 0, sizeof(supportedPIDs));

        for (int group = 0; group < 4; group++) {
            uint16_t basePID = group * 0x20;
            uint8_t a = 0, b = 0, c = 0, d = 0;
            // Request PID support bitmap (4 bytes = 32 PIDs per group)
            uint8_t respA = 0, respB = 0;
            if (obd2.requestPID(0x01, basePID, &respA, &respB, 200)) {
                // Each bit represents a PID: MSB of A = basePID+1, etc.
                // We get 2 bytes from our simple request; store what we can
                for (int bit = 0; bit < 8; bit++) {
                    if (respA & (0x80 >> bit)) {
                        int pid = basePID + bit + 1;
                        if (pid < 256) {
                            supportedPIDs[pid / 8] |= (1 << (pid % 8));
                            numSupportedPIDs++;
                        }
                    }
                }
                for (int bit = 0; bit < 8; bit++) {
                    if (respB & (0x80 >> bit)) {
                        int pid = basePID + 8 + bit + 1;
                        if (pid < 256) {
                            supportedPIDs[pid / 8] |= (1 << (pid % 8));
                            numSupportedPIDs++;
                        }
                    }
                }
            }
        }
        Serial.printf("[PID] Found %d supported PIDs\n", numSupportedPIDs);
        display.clearScreen();
        display.drawPIDResultsTab5(supportedPIDs, numSupportedPIDs);
        tab5State = T5_PID_RESULTS;
        break;
    }

    case T5_PID_RESULTS: {
        if (display.dtcBackTapped()) {
            tab5State = T5_DTC_MENU;
            display.clearScreen();
            display.drawDTCMenuTab5(-1, obdPollingEnabled, dataLogger.isLogging(), peakHoldEnabled);
        }
        break;
    }

    case T5_FULLSCREEN: {
        auto action = display.pollTouch();
        // Any tap exits fullscreen
        if (action == GaugeDisplay::TOUCH_GAUGE_TAP ||
            action == GaugeDisplay::TOUCH_DTC_BUTTON ||
            action == GaugeDisplay::TOUCH_GAUGE_LONGPRESS) {
            fullscreenSlot = -1;
            tab5State = T5_GAUGE;
            needsFullRedraw = true;
            break;
        }

        // Poll and update the fullscreen gauge
        uint32_t now = millis();
        if (now - lastPollTime >= POLL_INTERVAL_MS) {
            lastPollTime = now;
            int gIdx = gaugeIndices[fullscreenSlot];

            if (obdPollingEnabled) {
                const GaugeConfig& g = GAUGES[gIdx];
                uint8_t a = 0, b = 0;
                if (obd2.requestPID(g.mode, g.pid, &a, &b, 50)) {
                    float val = decodeOBD2(g, a, b);
                    gaugeValues[fullscreenSlot] = val;
                    if (val > peakValues[fullscreenSlot]) peakValues[fullscreenSlot] = val;
                    if (val < minValues[fullscreenSlot]) minValues[fullscreenSlot] = val;
                    lastCanDataTime = now;
                    if (dataLogger.isLogging()) {
                        dataLogger.logRow(gaugeIndices, gaugeValues, now);
                    }
                }
            }

            display.drawFullscreenGauge(gIdx, gaugeValues[fullscreenSlot],
                                         peakHoldEnabled ? peakValues[fullscreenSlot] : -1,
                                         false);
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

    loadGaugeConfigs();

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
