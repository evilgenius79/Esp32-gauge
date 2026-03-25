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

    // Determine which quadrant was tapped
    int col = (tx < SCREEN_W / 2) ? 0 : 1;
    int row = (ty < SCREEN_H / 2) ? 0 : 1;
    return row * 2 + col;
}

// #############################################################################
//  CROWPANEL — Single gauge on 240x240 GC9A01 round display
// #############################################################################

#else // CrowPanel

static constexpr int CX = 120;
static constexpr int CY = 120;

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
