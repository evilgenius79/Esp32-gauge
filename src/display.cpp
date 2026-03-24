#include "display.h"
#include <Arduino.h>
#include <cmath>

// =============================================================================
// Layout constants (240x240, scaled from 466x466 reference)
// =============================================================================
static constexpr int CX = 120;
static constexpr int CY = 120;

// LovyanGFX angles: 0 = top (12 o'clock), clockwise
// Gauge: 0 value at ~7:30, max value at ~4:30, 270 deg sweep
static constexpr float ARC_START = 225.0f;
static constexpr float ARC_SWEEP = 270.0f;

// Radii
static constexpr int R_OUTER     = 117;   // Outermost visible edge
static constexpr int R_TICK_OUT  = 113;   // Tick marks outer edge
static constexpr int R_TICK_MAJ  = 100;   // Major tick inner end (13px long)
static constexpr int R_TICK_MIN  = 106;   // Minor tick inner end (7px long)
static constexpr int R_LABEL     = 85;    // Number labels
static constexpr int R_NEEDLE    = 97;    // Needle tip
static constexpr int R_TAIL      = 15;    // Needle tail
static constexpr int R_RING_OUT  = 30;    // Center ring outer
static constexpr int R_RING_IN   = 20;    // Center ring inner
static constexpr int R_SWEEP_OUT = 116;   // Sweep arc outer
static constexpr int R_SWEEP_IN  = 108;   // Sweep arc inner

// Colors (RGB565)
static constexpr uint16_t C_BLACK    = 0x0000;
static constexpr uint16_t C_DKGRAY   = 0x2104;   // Very dark gray for face
static constexpr uint16_t C_GRAY     = 0x4A49;   // Tick minor color
static constexpr uint16_t C_WHITE    = 0xFFFF;
static constexpr uint16_t C_RED      = 0xF800;
static constexpr uint16_t C_ORANGE   = 0xFB20;   // Orange for needle/ring center
static constexpr uint16_t C_YELLOW   = 0xFFE0;   // Yellow for center ring highlight

// Glow palette (brighter than before so they're visible on hardware)
static constexpr uint16_t C_GLOW_RED1 = 0x3000;  // Outermost red glow (faint)
static constexpr uint16_t C_GLOW_RED2 = 0x7800;  // Mid red glow
static constexpr uint16_t C_GLOW_RED3 = 0xB000;  // Inner red glow (bright)
static constexpr uint16_t C_GLOW_ORG1 = 0x4100;  // Faint orange glow
static constexpr uint16_t C_GLOW_ORG2 = 0xC340;  // Brighter orange glow

// =============================================================================
// Helpers
// =============================================================================

// Make a dimmed version of a color (simple bit shift approach)
static uint16_t dimColor565(uint16_t c, int shift) {
    uint16_t r = (c >> 11) & 0x1F;
    uint16_t g = (c >> 5)  & 0x3F;
    uint16_t b = c & 0x1F;
    r >>= shift;
    g >>= shift;
    b >>= shift;
    return (r << 11) | (g << 5) | b;
}

// =============================================================================
// Init
// =============================================================================

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
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(0, (percent * 255) / 100);
}

// =============================================================================
// Angle mapping
// =============================================================================

float GaugeDisplay::valueToAngle(const GaugeConfig& gauge, float value) {
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);
    float angle = ARC_START + ARC_SWEEP * frac;
    if (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

// =============================================================================
// Main draw
// =============================================================================

void GaugeDisplay::drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw) {
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    if (!forceRedraw && fabsf(clamped - _prevValue) < (gauge.maxVal - gauge.minVal) * 0.003f) {
        return;
    }
    _prevValue = clamped;

    _sprite.fillSprite(C_BLACK);

    drawFace(gauge);
    drawSweepArc(gauge, clamped);
    drawNeedle(valueToAngle(gauge, clamped));
    drawCenterRing();
    drawLabels(gauge, clamped);

    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawNoCanStatus() {
    // Force redraw on next call
    _prevValue = -99999.0f;
}

// =============================================================================
// Gauge face: outer ring, tick marks, numbers
// =============================================================================

void GaugeDisplay::drawFace(const GaugeConfig& gauge) {
    // Subtle dark outer ring
    _sprite.fillArc(CX, CY, R_OUTER, R_OUTER - 2, 0, 360, C_DKGRAY);

    float range = gauge.maxVal - gauge.minVal;
    int numMajor = gauge.majorDivisions;
    float majorStep = range / numMajor;
    int numMinor = numMajor * 5;  // 5 minor ticks per major division
    float minorStep = range / numMinor;

    // Danger zone fraction
    float dangerFrac = (gauge.dangerVal - gauge.minVal) / range;

    // --- Minor ticks ---
    for (int i = 0; i <= numMinor; i++) {
        float v = gauge.minVal + i * minorStep;
        float frac = (float)i / numMinor;
        float deg = ARC_START + ARC_SWEEP * frac;

        float ox = px(CX, R_TICK_OUT, deg);
        float oy = py(CY, R_TICK_OUT, deg);
        float ix = px(CX, R_TICK_MIN, deg);
        float iy = py(CY, R_TICK_MIN, deg);

        uint16_t tickColor = (frac >= dangerFrac) ? C_RED : C_GRAY;
        _sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, tickColor);
    }

    // --- Major ticks + numbers ---
    for (int i = 0; i <= numMajor; i++) {
        float frac = (float)i / numMajor;
        float deg = ARC_START + ARC_SWEEP * frac;

        // Major tick (draw 2px wide)
        float ox = px(CX, R_TICK_OUT, deg);
        float oy = py(CY, R_TICK_OUT, deg);
        float ix = px(CX, R_TICK_MAJ, deg);
        float iy = py(CY, R_TICK_MAJ, deg);

        uint16_t tickColor = (frac >= dangerFrac) ? C_RED : C_WHITE;
        _sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, tickColor);
        // Second line offset for thickness
        float perpDeg = deg + 90.0f;
        float dx = sinf(perpDeg * DEG_TO_RAD);
        float dy = -cosf(perpDeg * DEG_TO_RAD);
        _sprite.drawLine((int)(ox+dx), (int)(oy+dy), (int)(ix+dx), (int)(iy+dy), tickColor);

        // Number label
        float lx = px(CX, R_LABEL, deg);
        float ly = py(CY, R_LABEL, deg);

        float val = gauge.minVal + i * majorStep;
        float displayVal = val / gauge.scaleDivisor;

        char label[8];
        // Show integer if the display value is a whole number
        if (fabsf(displayVal - roundf(displayVal)) < 0.01f) {
            snprintf(label, sizeof(label), "%.0f", displayVal);
        } else {
            snprintf(label, sizeof(label), "%.1f", displayVal);
        }

        _sprite.setFont(&fonts::FreeSansBold9pt7b);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.setTextColor(tickColor);
        _sprite.drawString(label, (int)lx, (int)ly);
    }
}

// =============================================================================
// Red sweep arc: extends from start to current value position
// =============================================================================

void GaugeDisplay::drawSweepArc(const GaugeConfig& gauge, float value) {
    if (value <= gauge.minVal) return;

    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);

    // Convert from our angle convention (0°=top, CW) to LovyanGFX (0°=right, CW) by subtracting 90°
    float startDeg = ARC_START - 90.0f;              // 135°
    float endDeg   = startDeg + ARC_SWEEP * frac;

    // Helper: draw all 4 glow/arc layers for a given angular span
    auto drawLayers = [&](float s, float e) {
        _sprite.fillArc(CX, CY, R_SWEEP_OUT + 4, R_SWEEP_IN - 4, s, e, C_GLOW_RED1);
        _sprite.fillArc(CX, CY, R_SWEEP_OUT + 2, R_SWEEP_IN - 2, s, e, C_GLOW_RED2);
        _sprite.fillArc(CX, CY, R_SWEEP_OUT, R_SWEEP_IN, s, e, C_RED);
        _sprite.fillArc(CX, CY, R_SWEEP_OUT - 2, R_SWEEP_IN + 2, s, e, C_ORANGE);
    };

    if (endDeg > 360.0f) {
        // Arc crosses 360°: split into two segments
        drawLayers(startDeg, 360.0f);
        drawLayers(0.0f, endDeg - 360.0f);
    } else {
        drawLayers(startDeg, endDeg);
    }
}

// =============================================================================
// Needle: triangle with glow effect
// =============================================================================

void GaugeDisplay::drawNeedle(float angleDeg) {
    float tipX = px(CX, R_NEEDLE, angleDeg);
    float tipY = py(CY, R_NEEDLE, angleDeg);
    float tailX = px(CX, R_TAIL, angleDeg + 180.0f);
    float tailY = py(CY, R_TAIL, angleDeg + 180.0f);

    float perpDeg = angleDeg + 90.0f;
    float sinP = sinf(perpDeg * DEG_TO_RAD);
    float cosP = cosf(perpDeg * DEG_TO_RAD);

    // Outer glow (wide, faint red)
    float gw = 7.0f;
    float g1x = CX + gw * sinP, g1y = CY - gw * cosP;
    float g2x = CX - gw * sinP, g2y = CY + gw * cosP;
    _sprite.fillTriangle((int)tipX, (int)tipY, (int)g1x, (int)g1y, (int)g2x, (int)g2y, C_GLOW_RED1);
    _sprite.fillTriangle((int)tailX, (int)tailY, (int)g1x, (int)g1y, (int)g2x, (int)g2y, C_GLOW_RED1);

    // Inner glow (brighter red)
    gw = 5.0f;
    g1x = CX + gw * sinP; g1y = CY - gw * cosP;
    g2x = CX - gw * sinP; g2y = CY + gw * cosP;
    _sprite.fillTriangle((int)tipX, (int)tipY, (int)g1x, (int)g1y, (int)g2x, (int)g2y, C_GLOW_RED2);
    _sprite.fillTriangle((int)tailX, (int)tailY, (int)g1x, (int)g1y, (int)g2x, (int)g2y, C_GLOW_RED2);

    // Main needle (bright orange)
    float nw = 2.5f;
    float n1x = CX + nw * sinP, n1y = CY - nw * cosP;
    float n2x = CX - nw * sinP, n2y = CY + nw * cosP;
    _sprite.fillTriangle((int)tipX, (int)tipY, (int)n1x, (int)n1y, (int)n2x, (int)n2y, C_ORANGE);
    _sprite.fillTriangle((int)tailX, (int)tailY, (int)n1x, (int)n1y, (int)n2x, (int)n2y, C_ORANGE);

    // Hot center line (yellow core for neon pop)
    _sprite.drawLine(CX, CY, (int)tipX, (int)tipY, C_YELLOW);
}

// =============================================================================
// Center decorative ring (glowing orange/red)
// =============================================================================

void GaugeDisplay::drawCenterRing() {
    // Layered neon glow (outer to inner)
    _sprite.fillArc(CX, CY, R_RING_OUT + 6, R_RING_IN - 6, 0, 360, C_GLOW_RED1);
    _sprite.fillArc(CX, CY, R_RING_OUT + 4, R_RING_IN - 4, 0, 360, C_GLOW_ORG1);
    _sprite.fillArc(CX, CY, R_RING_OUT + 2, R_RING_IN - 2, 0, 360, C_GLOW_ORG2);
    // Main ring (bright orange)
    _sprite.fillArc(CX, CY, R_RING_OUT, R_RING_IN, 0, 360, C_ORANGE);
    // Yellow highlight on inner edge
    _sprite.fillArc(CX, CY, R_RING_OUT - 2, R_RING_IN + 2, 0, 360, C_YELLOW);
    // Inner dark fill
    _sprite.fillCircle(CX, CY, R_RING_IN - 1, C_BLACK);
    // Small center dot
    _sprite.fillCircle(CX, CY, 4, C_ORANGE);
}

// =============================================================================
// Text labels: scale label, numeric value, units, NO CAN status
// =============================================================================

void GaugeDisplay::drawLabels(const GaugeConfig& gauge, float value) {
    // Scale label (e.g. "x1000r/min") — above center, in red
    _sprite.setFont(&fonts::Font2);
    _sprite.setTextDatum(BC_DATUM);
    _sprite.setTextColor(C_RED);
    _sprite.drawString(gauge.scaleLabel, CX, CY - R_RING_OUT - 8);

    // Numeric value — below center, large, white
    char valBuf[16];
    if (gauge.decimals == 0) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", value);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.*f", gauge.decimals, value);
    }

    _sprite.setFont(&fonts::FreeSansBold12pt7b);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_WHITE);
    _sprite.drawString(valBuf, CX, CY + R_RING_OUT + 10);

    // Units label (e.g. "rpm") — below numeric value, in red
    _sprite.setFont(&fonts::Font2);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_RED);
    _sprite.drawString(gauge.units, CX, CY + R_RING_OUT + 38);
}

// =============================================================================
// DTC Menu Screens
// =============================================================================

static const char* DTC_MENU_ITEMS[] = { "SCAN CODES", "CLEAR CODES", "BACK" };
static constexpr int DTC_MENU_COUNT = 3;

void GaugeDisplay::drawDTCMenu(int selectedItem) {
    _sprite.fillSprite(C_BLACK);

    // Title
    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("DIAGNOSTICS", CX, 30);

    // Decorative line
    _sprite.drawFastHLine(40, 52, 160, C_DKGRAY);

    // Menu items
    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    for (int i = 0; i < DTC_MENU_COUNT; i++) {
        int y = 80 + i * 45;
        if (i == selectedItem) {
            // Highlight bar
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
    _prevValue = -99999.0f;  // Force gauge redraw when returning

    // Title — same Y as other menus so it doesn't clip on the round display
    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setTextColor(C_ORANGE);
    _sprite.drawString("TROUBLE CODES", CX, 30);

    _sprite.drawFastHLine(40, 52, 160, C_DKGRAY);

    if (count == 0) {
        _sprite.setFont(&fonts::FreeSansBold12pt7b);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.setTextColor(0x07E0);  // Green
        _sprite.drawString("NO CODES", CX, CY);

        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(C_GRAY);
        _sprite.drawString("ALL CLEAR", CX, CY + 30);
    } else {
        // Scrollable list: show up to 5 codes starting from scrollOffset
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

        // Scroll indicators
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

    // Footer
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
        _sprite.setTextColor(0x07E0);  // Green
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
