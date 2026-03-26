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

int GaugeDisplay::touchedGauge() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return -1;

    // Debounce
    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return -1;
    _lastTouchTime = now;

    int tx = touch.x;
    int ty = touch.y;

    // Ignore taps on the DTC button area (handled separately)
    if (tx >= DTC_BTN_X && tx <= DTC_BTN_X + DTC_BTN_W &&
        ty >= DTC_BTN_Y && ty <= DTC_BTN_Y + DTC_BTN_H) {
        return -1;
    }

    // Determine which quadrant was tapped
    int col = (tx < SCREEN_W / 2) ? 0 : 1;
    int row = (ty < SCREEN_H / 2) ? 0 : 1;
    return row * 2 + col;
}

// =============================================================================
// DTC Button — sits at center of the 2x2 grid intersection
// =============================================================================

void GaugeDisplay::drawDTCButton() {
    M5.Display.fillRoundRect(DTC_BTN_X, DTC_BTN_Y, DTC_BTN_W, DTC_BTN_H, 10, C_DKGRAY);
    M5.Display.drawRoundRect(DTC_BTN_X, DTC_BTN_Y, DTC_BTN_W, DTC_BTN_H, 10, C_ORANGE);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(C_ORANGE);
    M5.Display.setFont(&fonts::FreeSansBold9pt7b);
    M5.Display.drawString("DTC", SCREEN_W / 2, SCREEN_H / 2);
}

bool GaugeDisplay::dtcButtonTapped() {
    auto touch = M5.Touch.getDetail();
    if (!touch.wasClicked()) return false;

    uint32_t now = millis();
    if (now - _lastTouchTime < 300) return false;
    _lastTouchTime = now;

    return (touch.x >= DTC_BTN_X && touch.x <= DTC_BTN_X + DTC_BTN_W &&
            touch.y >= DTC_BTN_Y && touch.y <= DTC_BTN_Y + DTC_BTN_H);
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
