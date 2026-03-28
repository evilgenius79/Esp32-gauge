#include "display.h"
#include <Arduino.h>
#include <cmath>

// Colors used by menu screens (shared palette)
static constexpr uint16_t C_BLACK   = 0x0000;
static constexpr uint16_t C_DKGRAY  = 0x2104;
static constexpr uint16_t C_GRAY    = 0x4A49;
static constexpr uint16_t C_WHITE   = 0xFFFF;
static constexpr uint16_t C_RED     = 0xF800;
static constexpr uint16_t C_ORANGE  = 0xFB20;

// #############################################################################
//  TAB5 — Multi-gauge 2x2 dashboard on 1280x720 MIPI-DSI
// #############################################################################

#ifdef TARGET_TAB5

void GaugeDisplay::begin() {
    // Force landscape: Tab5 panel is 720x1280 natively, rotation 1 = 1280x720
    M5.Display.setRotation(1);

    _layout = GaugeLayout::fromSize(GAUGE_SIZE);

    for (int i = 0; i < 4; i++) {
        _sprites[i].setColorDepth(16);
        _sprites[i].setPsram(true);
        _sprites[i].createSprite(GAUGE_SIZE, GAUGE_SIZE);
    }

    M5.Display.fillScreen(C_BLACK);

    // Draw grid separator lines
    M5.Display.drawFastVLine(SCREEN_W / 2, 0, SCREEN_H, C_DKGRAY);
    M5.Display.drawFastHLine(0, SCREEN_H / 2, SCREEN_W, C_DKGRAY);

    setBrightness(70);
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    M5.Display.setBrightness((percent * 255) / 100);
}

void GaugeDisplay::clearScreen() {
    M5.Display.fillScreen(C_BLACK);
}

void GaugeDisplay::drawSingleGauge(int slot, int gaugeIdx, float value, bool forceRedraw) {
    if (slot < 0 || slot > 3) return;
    const GaugeConfig& gauge = GAUGES[gaugeIdx];
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    // Skip redraw if value hasn't changed enough
    if (!forceRedraw && _prevGaugeIdx[slot] == gaugeIdx &&
        fabsf(clamped - _prevValues[slot]) < (gauge.maxVal - gauge.minVal) * 0.003f) {
        return;
    }
    _prevValues[slot] = clamped;

    // Full redraw if gauge type changed
    if (_prevGaugeIdx[slot] != gaugeIdx) {
        forceRedraw = true;
        _prevGaugeIdx[slot] = gaugeIdx;
    }

    // Render gauge into slot sprite
    _sprites[slot].fillSprite(C_BLACK);
    renderGauge(_sprites[slot], _layout, gauge, clamped);

    // Push to screen at the correct position
    _sprites[slot].pushSprite(&M5.Display, slotX(slot), slotY(slot));
}

void GaugeDisplay::drawAllGauges(const int indices[4], const float values[4], bool forceRedraw) {
    for (int i = 0; i < 4; i++) {
        drawSingleGauge(i, indices[i], values[i], forceRedraw);
    }
}

// =============================================================================
// Unified touch handler — reads touch once, detects tap vs long-press
// =============================================================================

GaugeDisplay::TouchAction GaugeDisplay::pollTouch() {
    auto touch = M5.Touch.getDetail();
    uint32_t now = millis();
    touchSlot = -1;

    // Track touch-down for long-press detection
    if (touch.isPressed()) {
        if (!_touching) {
            // Touch just started
            _touching = true;
            _touchStartTime = now;
            _longPressTriggered = false;

            int col = (touch.x < SCREEN_W / 2) ? 0 : 1;
            int row = (touch.y < SCREEN_H / 2) ? 0 : 1;
            _touchQuadrant = row * 2 + col;

            // Check if touching DTC button area
            if (touch.x >= DTC_BTN_X && touch.x <= DTC_BTN_X + DTC_BTN_W &&
                touch.y >= DTC_BTN_Y && touch.y <= DTC_BTN_Y + DTC_BTN_H) {
                _touchQuadrant = -1;  // DTC button
            }
            // Check if touching LOG button area
            else if (touch.x >= LOG_BTN_X && touch.x <= LOG_BTN_X + LOG_BTN_W &&
                     touch.y >= LOG_BTN_Y && touch.y <= LOG_BTN_Y + LOG_BTN_H) {
                _touchQuadrant = -2;  // LOG button
            }
        }

        // Check for long-press (1 second hold on a gauge)
        if (!_longPressTriggered && _touchQuadrant >= 0 &&
            (now - _touchStartTime) >= LONG_PRESS_MS) {
            _longPressTriggered = true;
            touchSlot = _touchQuadrant;
            _lastTouchTime = now;
            return TOUCH_GAUGE_LONGPRESS;
        }
        return TOUCH_NONE;
    }

    // Touch released
    if (_touching) {
        _touching = false;
        uint32_t duration = now - _touchStartTime;

        // Debounce
        if (now - _lastTouchTime < 300) return TOUCH_NONE;
        _lastTouchTime = now;

        // Ignore if long-press already handled
        if (_longPressTriggered) return TOUCH_NONE;

        // Short tap (< 1 second)
        if (duration < LONG_PRESS_MS) {
            if (_touchQuadrant == -1) {
                return TOUCH_DTC_BUTTON;
            } else if (_touchQuadrant == -2) {
                return TOUCH_LOG_BUTTON;
            } else {
                touchSlot = _touchQuadrant;
                return TOUCH_GAUGE_TAP;
            }
        }
    }

    return TOUCH_NONE;
}

// =============================================================================
// DTC Button — sits at center of the 2x2 grid intersection
// =============================================================================

static constexpr uint16_t C_GREEN  = 0x07E0;

void GaugeDisplay::drawDTCButton(bool logging) {
    // DTC button (above center line)
    M5.Display.fillRoundRect(DTC_BTN_X, DTC_BTN_Y, DTC_BTN_W, DTC_BTN_H, 8, C_DKGRAY);
    M5.Display.drawRoundRect(DTC_BTN_X, DTC_BTN_Y, DTC_BTN_W, DTC_BTN_H, 8, C_ORANGE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.drawString("DTC", DTC_BTN_X + DTC_BTN_W / 2, DTC_BTN_Y + DTC_BTN_H / 2);

    // LOG button (below center line) — green when logging, red when stopped
    uint16_t logColor = logging ? C_GREEN : C_RED;
    M5.Display.fillRoundRect(LOG_BTN_X, LOG_BTN_Y, LOG_BTN_W, LOG_BTN_H, 8, C_DKGRAY);
    M5.Display.drawRoundRect(LOG_BTN_X, LOG_BTN_Y, LOG_BTN_W, LOG_BTN_H, 8, logColor);
    M5.Display.setTextColor(logColor);
    const char* label = logging ? "STOP" : "LOG";
    M5.Display.drawString(label, LOG_BTN_X + LOG_BTN_W / 2, LOG_BTN_Y + LOG_BTN_H / 2);
}

// =============================================================================
// DTC Fullscreen Menus for Tab5 (touch-driven)
// =============================================================================

static constexpr int TCX = 640;  // Tab5 center X
static constexpr int TCY = 360;  // Tab5 center Y

static const char* TAB5_DTC_MENU[] = { "SCAN CODES", "CLEAR CODES", "BACK" };
static constexpr int TAB5_MENU_BTN_W = 400;
static constexpr int TAB5_MENU_BTN_H = 70;
static constexpr int TAB5_MENU_SPACING = 90;
static constexpr int TAB5_MENU_START_Y = 220;

void GaugeDisplay::drawDTCMenuTab5(int selectedItem) {
    M5.Display.fillScreen(C_BLACK);

    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("DIAGNOSTICS", TCX, 60);

    M5.Display.drawFastHLine(TCX - 200, 120, 400, C_DKGRAY);

    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    for (int i = 0; i < 3; i++) {
        int y = TAB5_MENU_START_Y + i * TAB5_MENU_SPACING;
        int x = TCX - TAB5_MENU_BTN_W / 2;

        if (i == selectedItem) {
            M5.Display.fillRoundRect(x, y, TAB5_MENU_BTN_W, TAB5_MENU_BTN_H, 12, C_RED);
            M5.Display.setTextColor(C_WHITE);
        } else {
            M5.Display.drawRoundRect(x, y, TAB5_MENU_BTN_W, TAB5_MENU_BTN_H, 12, C_GRAY);
            M5.Display.setTextColor(C_GRAY);
        }
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.drawString(TAB5_DTC_MENU[i], TCX, y + TAB5_MENU_BTN_H / 2);
    }
}

int GaugeDisplay::dtcMenuTapped() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return -1;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return -1;
    _lastTouchTime = now;

    int tx = touch.x;
    int ty = touch.y;
    int btnX = TCX - TAB5_MENU_BTN_W / 2;

    for (int i = 0; i < 3; i++) {
        int btnY = TAB5_MENU_START_Y + i * TAB5_MENU_SPACING;
        if (tx >= btnX && tx <= btnX + TAB5_MENU_BTN_W &&
            ty >= btnY && ty <= btnY + TAB5_MENU_BTN_H) {
            return i;
        }
    }
    return -1;
}

void GaugeDisplay::drawDTCScanningTab5() {
    M5.Display.fillScreen(C_BLACK);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("SCANNING...", TCX, TCY);
}

void GaugeDisplay::drawDTCResultsTab5(const DTC* dtcs, int count, int scrollOffset) {
    M5.Display.fillScreen(C_BLACK);

    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("TROUBLE CODES", TCX, 40);

    M5.Display.drawFastHLine(TCX - 200, 90, 400, C_DKGRAY);

    if (count == 0) {
        M5.Display.setFont(&fonts::FreeSansBold18pt7b);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.setTextColor(0x07E0);  // Green
        M5.Display.drawString("NO CODES", TCX, TCY - 20);

        M5.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5.Display.setTextColor(C_GRAY);
        M5.Display.drawString("ALL CLEAR", TCX, TCY + 30);
    } else {
        constexpr int VISIBLE = 8;
        constexpr int CODE_START_Y = 120;
        constexpr int CODE_SPACING = 55;

        int show = min(count - scrollOffset, VISIBLE);

        M5.Display.setFont(&fonts::FreeSansBold12pt7b);
        M5.Display.setTextDatum(MC_DATUM);

        for (int i = 0; i < show; i++) {
            int y = CODE_START_Y + i * CODE_SPACING;
            M5.Display.setTextColor(C_RED);
            M5.Display.drawString(dtcs[scrollOffset + i].code, TCX, y);
        }

        M5.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5.Display.setTextColor(C_GRAY);
        if (scrollOffset > 0) {
            M5.Display.setTextDatum(TC_DATUM);
            M5.Display.drawString("^ SCROLL UP ^", TCX, 100);
        }
        if (scrollOffset + VISIBLE < count) {
            M5.Display.setTextDatum(BC_DATUM);
            M5.Display.drawString("v SCROLL DOWN v", TCX, 680);
        }
    }

    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.setTextDatum(BC_DATUM);
    M5.Display.setTextColor(C_GRAY);
    M5.Display.drawString("tap to return", TCX, 710);
}

int GaugeDisplay::dtcResultsScrollOrBack() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return 0;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return 0;
    _lastTouchTime = now;

    int ty = touch.y;
    // Top quarter = scroll up, bottom quarter = scroll down, middle = back
    if (ty < 180) return -1;       // scroll up
    if (ty > 540) return 1;        // scroll down
    return -2;                      // back
}

void GaugeDisplay::drawDTCClearingTab5() {
    M5.Display.fillScreen(C_BLACK);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("CLEARING...", TCX, TCY);
}

void GaugeDisplay::drawDTCClearedTab5(bool success) {
    M5.Display.fillScreen(C_BLACK);

    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(MC_DATUM);

    if (success) {
        M5.Display.setTextColor(0x07E0);
        M5.Display.drawString("CLEARED", TCX, TCY - 30);

        M5.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5.Display.setTextColor(C_GRAY);
        M5.Display.drawString("codes & MIL reset", TCX, TCY + 30);
    } else {
        M5.Display.setTextColor(C_RED);
        M5.Display.drawString("FAILED", TCX, TCY - 30);

        M5.Display.setFont(&fonts::FreeSansBold9pt7b);
        M5.Display.setTextColor(C_GRAY);
        M5.Display.drawString("no response from ECU", TCX, TCY + 30);
    }

    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.setTextDatum(BC_DATUM);
    M5.Display.setTextColor(C_GRAY);
    M5.Display.drawString("tap to return", TCX, 710);
}

bool GaugeDisplay::dtcBackTapped() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return false;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return false;
    _lastTouchTime = now;

    return true;
}

// =============================================================================
// Gauge Editor — fullscreen touchscreen config editor
// All 13 fields + formula selector + CAN speed toggle
// =============================================================================

static const char* EDITOR_FIELDS[] = {
    "NAME", "UNITS", "LABEL", "MODE", "PID",
    "MIN", "MAX", "WARN", "DANGER",
    "SCALE DIV", "DIVISIONS", "DATA BYTES", "DECIMALS", "FORMULA",
};
static constexpr int NUM_FIELDS = 14;
static constexpr int ED_ROW_H = 42;
static constexpr int ED_START_Y = 70;
static constexpr int ED_LABEL_X = 100;
static constexpr int ED_BTN_W = 550;

void GaugeDisplay::drawGaugeEditor(int slot, const GaugeConfig& gauge, int selectedField) {
    M5.Display.fillScreen(C_BLACK);

    // Title
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    char title[32];
    snprintf(title, sizeof(title), "EDIT GAUGE %d: %s", slot + 1, gauge.name);
    M5.Display.drawString(title, TCX, 12);
    M5.Display.drawFastHLine(TCX - 300, 48, 600, C_DKGRAY);

    // Fields
    M5.Display.setFont(&fonts::Font2);
    char valBuf[24];

    for (int i = 0; i < NUM_FIELDS; i++) {
        int y = ED_START_Y + i * ED_ROW_H;
        int rowX = TCX - ED_BTN_W / 2;

        if (i == selectedField) {
            M5.Display.fillRoundRect(rowX, y - 3, ED_BTN_W, 34, 6, 0x1082);
            M5.Display.drawRoundRect(rowX, y - 3, ED_BTN_W, 34, 6, C_ORANGE);
        }

        // Label
        M5.Display.setTextDatum(ML_DATUM);
        M5.Display.setTextColor(C_GRAY);
        M5.Display.drawString(EDITOR_FIELDS[i], ED_LABEL_X, y + 13);

        // Value
        M5.Display.setTextDatum(MR_DATUM);
        M5.Display.setTextColor(C_WHITE);

        switch (i) {
            case 0:  snprintf(valBuf, sizeof(valBuf), "%s", gauge.name); break;
            case 1:  snprintf(valBuf, sizeof(valBuf), "%s", gauge.units); break;
            case 2:  snprintf(valBuf, sizeof(valBuf), "%s", gauge.scaleLabel); break;
            case 3:  snprintf(valBuf, sizeof(valBuf), "0x%02X", gauge.mode); break;
            case 4:  snprintf(valBuf, sizeof(valBuf), "0x%04X", gauge.pid); break;
            case 5:  snprintf(valBuf, sizeof(valBuf), "%.1f", gauge.minVal); break;
            case 6:  snprintf(valBuf, sizeof(valBuf), "%.1f", gauge.maxVal); break;
            case 7:  snprintf(valBuf, sizeof(valBuf), "%.1f", gauge.warnVal); break;
            case 8:  snprintf(valBuf, sizeof(valBuf), "%.1f", gauge.dangerVal); break;
            case 9:  snprintf(valBuf, sizeof(valBuf), "%.1f", gauge.scaleDivisor); break;
            case 10: snprintf(valBuf, sizeof(valBuf), "%d", gauge.majorDivisions); break;
            case 11: snprintf(valBuf, sizeof(valBuf), "%d", gauge.dataBytes); break;
            case 12: snprintf(valBuf, sizeof(valBuf), "%d", gauge.decimals); break;
            case 13: {
                int f = gauge.formula;
                if (f >= 0 && f < NUM_DECODE_FORMULAS)
                    snprintf(valBuf, sizeof(valBuf), "%s", DECODE_NAMES[f]);
                else
                    snprintf(valBuf, sizeof(valBuf), "?");
                break;
            }
        }
        M5.Display.drawString(valBuf, SCREEN_W - ED_LABEL_X, y + 13);
    }

    // Bottom row: SAVE / RESET / CANCEL + CAN speed
    int btnY = ED_START_Y + NUM_FIELDS * ED_ROW_H + 6;
    int btnW = 150;
    int btnH = 44;
    int gap = 20;
    int startX = TCX - (4 * btnW + 3 * gap) / 2;

    // SAVE
    M5.Display.fillRoundRect(startX, btnY, btnW, btnH, 10, 0x0400);
    M5.Display.drawRoundRect(startX, btnY, btnW, btnH, 10, 0x07E0);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(0x07E0);
    M5.Display.drawString("SAVE", startX + btnW / 2, btnY + btnH / 2);

    // RESET
    int x2 = startX + btnW + gap;
    M5.Display.fillRoundRect(x2, btnY, btnW, btnH, 10, 0x4000);
    M5.Display.drawRoundRect(x2, btnY, btnW, btnH, 10, C_ORANGE);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("RESET", x2 + btnW / 2, btnY + btnH / 2);

    // CANCEL
    int x3 = startX + 2 * (btnW + gap);
    M5.Display.fillRoundRect(x3, btnY, btnW, btnH, 10, 0x4000);
    M5.Display.drawRoundRect(x3, btnY, btnW, btnH, 10, C_RED);
    M5.Display.setTextColor(C_RED);
    M5.Display.drawString("CANCEL", x3 + btnW / 2, btnY + btnH / 2);

    // CAN SPEED toggle
    int x4 = startX + 3 * (btnW + gap);
    char canLabel[16];
    snprintf(canLabel, sizeof(canLabel), "CAN %luk", canBusSpeed);
    M5.Display.fillRoundRect(x4, btnY, btnW, btnH, 10, 0x1082);
    M5.Display.drawRoundRect(x4, btnY, btnW, btnH, 10, C_WHITE);
    M5.Display.setTextColor(C_WHITE);
    M5.Display.drawString(canLabel, x4 + btnW / 2, btnY + btnH / 2);
}

int GaugeDisplay::editorFieldTapped() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return -1;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return -1;
    _lastTouchTime = now;

    int tx = touch.x;
    int ty = touch.y;

    // Check field rows
    for (int i = 0; i < NUM_FIELDS; i++) {
        int rowY = ED_START_Y + i * ED_ROW_H - 3;
        int rowX = TCX - ED_BTN_W / 2;
        if (tx >= rowX && tx <= rowX + ED_BTN_W &&
            ty >= rowY && ty <= rowY + 34) {
            return i;
        }
    }

    // Check bottom buttons
    int btnY = ED_START_Y + NUM_FIELDS * ED_ROW_H + 6;
    int btnW = 150;
    int btnH = 44;
    int gap = 20;
    int startX = TCX - (4 * btnW + 3 * gap) / 2;

    if (ty >= btnY && ty <= btnY + btnH) {
        if (tx >= startX && tx <= startX + btnW) return 99;                                    // SAVE
        if (tx >= startX + btnW + gap && tx <= startX + 2 * btnW + gap) return 98;             // RESET
        if (tx >= startX + 2 * (btnW + gap) && tx <= startX + 3 * btnW + 2 * gap) return 97;  // CANCEL
        if (tx >= startX + 3 * (btnW + gap) && tx <= startX + 4 * btnW + 3 * gap) return 96;  // CAN SPEED
    }

    return -1;
}

// =============================================================================
// Formula picker — scrollable list of decode formulas
// =============================================================================

void GaugeDisplay::drawFormulaPicker(int currentFormula) {
    M5.Display.fillScreen(C_BLACK);

    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString("SELECT DECODE FORMULA", TCX, 20);
    M5.Display.drawFastHLine(TCX - 300, 55, 600, C_DKGRAY);

    int fBtnW = 550;
    int fBtnH = 42;
    int fGap = 4;
    int fStartY = 65;
    int fX = TCX - fBtnW / 2;

    M5.Display.setFont(&fonts::Font2);

    for (int i = 0; i < NUM_DECODE_FORMULAS; i++) {
        int y = fStartY + i * (fBtnH + fGap);
        if (y + fBtnH > SCREEN_H) break;

        if (i == currentFormula) {
            M5.Display.fillRoundRect(fX, y, fBtnW, fBtnH, 8, 0x0400);
            M5.Display.drawRoundRect(fX, y, fBtnW, fBtnH, 8, 0x07E0);
            M5.Display.setTextColor(0x07E0);
        } else {
            M5.Display.drawRoundRect(fX, y, fBtnW, fBtnH, 8, C_DKGRAY);
            M5.Display.setTextColor(C_GRAY);
        }

        M5.Display.setTextDatum(ML_DATUM);
        char label[32];
        snprintf(label, sizeof(label), "%d: %s", i, DECODE_NAMES[i]);
        M5.Display.drawString(label, fX + 15, y + fBtnH / 2);
    }
}

int GaugeDisplay::formulaPickerTapped() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return -1;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return -1;
    _lastTouchTime = now;

    int fBtnW = 550;
    int fBtnH = 42;
    int fGap = 4;
    int fStartY = 65;
    int fX = TCX - fBtnW / 2;

    for (int i = 0; i < NUM_DECODE_FORMULAS; i++) {
        int y = fStartY + i * (fBtnH + fGap);
        if (touch.x >= fX && touch.x <= fX + fBtnW &&
            touch.y >= y && touch.y <= y + fBtnH) {
            return i;
        }
    }
    return -1;
}

// =============================================================================
// On-screen keypad — full alphabet + numbers + hex + symbols
// =============================================================================

static constexpr int KP_KEYS_PER_ROW = 10;
static constexpr int KP_KEY_W = 70;
static constexpr int KP_KEY_H = 58;
static constexpr int KP_GAP = 6;
static constexpr int KP_START_Y = 230;

// 5 rows of 10 keys
static const char* KP_LABELS[] = {
    "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
    "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
    "A", "S", "D", "F", "G", "H", "J", "K", "L", " ",
    "Z", "X", "C", "V", "B", "N", "M", ".", "-", "/",
    "DEL", " ", " ", "SPC", " ", " ", " ", " ", " ", "OK",
};
static constexpr int KP_NUM_KEYS = 50;

void GaugeDisplay::drawEditorKeypad(const char* fieldTitle, const char* currentValue) {
    M5.Display.fillScreen(C_BLACK);

    // Title
    M5.Display.setFont(&fonts::FreeSansBold12pt7b);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.drawString(fieldTitle, TCX, 20);

    // Current value display
    M5.Display.fillRoundRect(TCX - 300, 65, 600, 55, 10, 0x1082);
    M5.Display.drawRoundRect(TCX - 300, 65, 600, 55, 10, C_GRAY);
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(C_WHITE);
    M5.Display.drawString(currentValue, TCX, 92);

    // Hint
    M5.Display.setFont(&fonts::Font2);
    M5.Display.setTextColor(C_GRAY);
    M5.Display.setTextDatum(TC_DATUM);
    M5.Display.drawString("Hex PID: use 0-9 A-F  |  Text: full keyboard", TCX, 135);

    // Max length indicator
    char lenBuf[16];
    snprintf(lenBuf, sizeof(lenBuf), "%d chars", (int)strlen(currentValue));
    M5.Display.drawString(lenBuf, TCX, 155);

    // Draw keys
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    int totalW = KP_KEYS_PER_ROW * KP_KEY_W + (KP_KEYS_PER_ROW - 1) * KP_GAP;
    int kpStartX = TCX - totalW / 2;

    for (int i = 0; i < KP_NUM_KEYS; i++) {
        if (KP_LABELS[i][0] == ' ' && strlen(KP_LABELS[i]) == 1) continue;

        int row = i / KP_KEYS_PER_ROW;
        int col = i % KP_KEYS_PER_ROW;
        int x = kpStartX + col * (KP_KEY_W + KP_GAP);
        int y = KP_START_Y + row * (KP_KEY_H + KP_GAP);

        uint16_t bgColor = C_DKGRAY;
        uint16_t fgColor = C_WHITE;

        // Special keys
        if (strcmp(KP_LABELS[i], "OK") == 0) {
            bgColor = 0x0400; fgColor = 0x07E0;
        } else if (strcmp(KP_LABELS[i], "DEL") == 0) {
            bgColor = 0x4000; fgColor = C_RED;
        } else if (strcmp(KP_LABELS[i], "SPC") == 0) {
            fgColor = C_GRAY;
        } else if (i < 10) {
            fgColor = 0xFFE0;  // Yellow for numbers
        }

        // Special width for SPC and DEL/OK
        int keyW = KP_KEY_W;
        if (strcmp(KP_LABELS[i], "SPC") == 0) {
            keyW = KP_KEY_W * 4 + KP_GAP * 3;  // Wide space bar
        } else if (strcmp(KP_LABELS[i], "DEL") == 0 || strcmp(KP_LABELS[i], "OK") == 0) {
            keyW = KP_KEY_W;
        }

        M5.Display.fillRoundRect(x, y, keyW, KP_KEY_H, 6, bgColor);
        M5.Display.drawRoundRect(x, y, keyW, KP_KEY_H, 6, fgColor);
        M5.Display.setTextDatum(MC_DATUM);
        M5.Display.setTextColor(fgColor);
        M5.Display.drawString(KP_LABELS[i], x + keyW / 2, y + KP_KEY_H / 2);
    }
}

int GaugeDisplay::keypadTapped(char* buffer, int bufLen) {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return -1;

    uint32_t now = millis();
    if (now - _lastTouchTime < 200) return -1;
    _lastTouchTime = now;

    int tx = touch.x;
    int ty = touch.y;

    int totalW = KP_KEYS_PER_ROW * KP_KEY_W + (KP_KEYS_PER_ROW - 1) * KP_GAP;
    int kpStartX = TCX - totalW / 2;

    for (int i = 0; i < KP_NUM_KEYS; i++) {
        if (KP_LABELS[i][0] == ' ' && strlen(KP_LABELS[i]) == 1) continue;

        int row = i / KP_KEYS_PER_ROW;
        int col = i % KP_KEYS_PER_ROW;
        int x = kpStartX + col * (KP_KEY_W + KP_GAP);
        int y = KP_START_Y + row * (KP_KEY_H + KP_GAP);

        int keyW = KP_KEY_W;
        if (strcmp(KP_LABELS[i], "SPC") == 0) {
            keyW = KP_KEY_W * 4 + KP_GAP * 3;
        }

        if (tx >= x && tx <= x + keyW && ty >= y && ty <= y + KP_KEY_H) {
            if (strcmp(KP_LABELS[i], "OK") == 0) return 1;   // Done
            if (strcmp(KP_LABELS[i], "DEL") == 0) {
                int len = strlen(buffer);
                if (len > 0) buffer[len - 1] = '\0';
                return 0;
            }
            if (strcmp(KP_LABELS[i], "SPC") == 0) {
                int len = strlen(buffer);
                if (len < bufLen - 1) { buffer[len] = ' '; buffer[len + 1] = '\0'; }
                return 0;
            }
            // Regular character key
            int len = strlen(buffer);
            if (len < bufLen - 1) {
                buffer[len] = KP_LABELS[i][0];
                buffer[len + 1] = '\0';
            }
            return 0;
        }
    }

    return -1;  // No key hit
}

// #############################################################################
//  CROWPANEL — Single gauge on 240x240 GC9A01 round display
// #############################################################################

#else // CrowPanel

static constexpr int CX = 120;
static constexpr int CY = 120;

void GaugeDisplay::clearScreen() {
    _tft.fillScreen(C_BLACK);
}

void GaugeDisplay::begin() {
    pinMode(PIN_LCD_PWR_EN1, OUTPUT);
    pinMode(PIN_LCD_PWR_EN2, OUTPUT);
    digitalWrite(PIN_LCD_PWR_EN1, HIGH);
    digitalWrite(PIN_LCD_PWR_EN2, HIGH);

    _tft.init();
    _tft.setRotation(0);

    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    setBrightness(70);

    _sprite.setColorDepth(16);
    _sprite.setPsram(true);
    _sprite.createSprite(240, 240);

    _tft.fillScreen(C_BLACK);

    _layout = GaugeLayout::fromSize(240);
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(0, (percent * 255) / 100);
}

// =============================================================================
// Main gauge draw — uses shared renderer
// =============================================================================

void GaugeDisplay::drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw) {
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    if (!forceRedraw && fabsf(clamped - _prevValue) < (gauge.maxVal - gauge.minVal) * 0.003f) {
        return;
    }
    _prevValue = clamped;

    _sprite.fillSprite(C_BLACK);
    renderGauge(_sprite, _layout, gauge, clamped);
    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawNoCanStatus() {
    _prevValue = -99999.0f;
}

// =============================================================================
// DTC Menu Screens (CrowPanel only — encoder-driven)
// =============================================================================

static const char* DTC_MENU_ITEMS[] = { "SCAN CODES", "CLEAR CODES", "BACK" };
static constexpr int DTC_MENU_COUNT = 3;

void GaugeDisplay::drawDTCMenu(int selectedItem) {
    _sprite.fillSprite(C_BLACK);

    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("DIAGNOSTICS", CX, 30);

    _sprite.drawFastHLine(40, 52, 160, C_DKGRAY);

    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    for (int i = 0; i < DTC_MENU_COUNT; i++) {
        int y = 80 + i * 45;
        if (i == selectedItem) {
            _sprite.fillRoundRect(30, y - 8, 180, 32, 6, C_RED);
            _sprite.setTextColor(C_WHITE);
        } else {
            _sprite.drawRoundRect(30, y - 8, 180, 32, 6, C_DKGRAY);
            _sprite.setTextColor(C_GRAY);
        }
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString(DTC_MENU_ITEMS[i], CX, y + 8);
    }

    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawDTCScanning() {
    _sprite.fillSprite(C_BLACK);

    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("SCANNING...", CX, CY);

    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawDTCResults(const DTC* dtcs, int count, int scrollOffset) {
    _sprite.fillSprite(C_BLACK);
    _prevValue = -99999.0f;

    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("TROUBLE CODES", CX, 30);

    _sprite.drawFastHLine(40, 52, 160, C_DKGRAY);

    if (count == 0) {
        _sprite.setFont(&fonts::FreeSansBold12pt7b);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.setTextColor(0x07E0);
        _sprite.drawString("NO CODES", CX, CY);

        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(C_GRAY);
        _sprite.drawString("ALL CLEAR", CX, CY + 30);
    } else {
        constexpr int VISIBLE_CODES = 5;
        constexpr int CODE_START_Y  = 65;
        constexpr int CODE_SPACING  = 28;

        int show = (count - scrollOffset > VISIBLE_CODES)
                     ? VISIBLE_CODES : (count - scrollOffset);

        _sprite.setFont(&fonts::FreeSansBold9pt7b);
        _sprite.setTextDatum(MC_DATUM);

        for (int i = 0; i < show; i++) {
            int y = CODE_START_Y + i * CODE_SPACING;
            _sprite.setTextColor(C_RED);
            _sprite.drawString(dtcs[scrollOffset + i].code, CX, y);
        }

        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(C_GRAY);
        if (scrollOffset > 0) {
            _sprite.setTextDatum(TC_DATUM);
            _sprite.drawString("^ more ^", CX, 54);
        }
        if (scrollOffset + VISIBLE_CODES < count) {
            _sprite.setTextDatum(BC_DATUM);
            _sprite.drawString("v more v", CX, 210);
        }
    }

    _sprite.setFont(&fonts::Font2);
    _sprite.setTextDatum(BC_DATUM);
    _sprite.setTextColor(C_GRAY);
    _sprite.drawString("press to return", CX, 232);

    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawDTCClearing() {
    _sprite.fillSprite(C_BLACK);

    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("CLEARING...", CX, CY);

    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawDTCCleared(bool success) {
    _sprite.fillSprite(C_BLACK);
    _prevValue = -99999.0f;

    _sprite.setFont(&fonts::FreeSansBold12pt7b);
    _sprite.setTextDatum(MC_DATUM);

    if (success) {
        _sprite.setTextColor(0x07E0);
        _sprite.drawString("CLEARED", CX, CY - 10);

        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(C_GRAY);
        _sprite.drawString("codes & MIL reset", CX, CY + 20);
    } else {
        _sprite.setTextColor(C_RED);
        _sprite.drawString("FAILED", CX, CY - 10);

        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(C_GRAY);
        _sprite.drawString("no response from ECU", CX, CY + 20);
    }

    _sprite.setFont(&fonts::Font2);
    _sprite.setTextDatum(BC_DATUM);
    _sprite.setTextColor(C_GRAY);
    _sprite.drawString("press to return", CX, 235);

    _sprite.pushSprite(&_tft, 0, 0);
}

#endif // CrowPanel
