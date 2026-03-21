#include "display.h"
#include <Arduino.h>
#include <cmath>

// =============================================================================
// Geometry constants
// =============================================================================
static constexpr int CX = 120;            // Center X
static constexpr int CY = 120;            // Center Y

// Arc geometry (LovyanGFX angles: 0°=top, clockwise)
static constexpr float ARC_START  = 225.0f;  // 7:30 position
static constexpr float ARC_END    = 135.0f;  // 4:30 position
static constexpr float ARC_SWEEP  = 270.0f;  // Total degrees

static constexpr int R_ARC_OUTER  = 115;     // Outer edge of colored arc
static constexpr int R_ARC_INNER  = 104;     // Inner edge of colored arc (11px band)
static constexpr int R_TICK_OUTER = 103;     // Tick marks start here
static constexpr int R_TICK_MAJOR = 90;      // Major tick inner end
static constexpr int R_TICK_MINOR = 96;      // Minor tick inner end
static constexpr int R_LABEL      = 76;      // Number label radius
static constexpr int R_NEEDLE     = 92;      // Needle tip radius
static constexpr int R_NEEDLE_HUB = 7;       // Center hub radius
static constexpr int R_NEEDLE_TAIL= 14;      // Needle tail length

// Colors (RGB565)
static constexpr uint16_t COL_BG       = 0x0011;  // Very dark blue
static constexpr uint16_t COL_DIAL_BG  = 0x0019;  // Slightly lighter dark blue
static constexpr uint16_t COL_GREEN    = 0x07E0;
static constexpr uint16_t COL_YELLOW   = 0xFFE0;
static constexpr uint16_t COL_RED      = 0xF800;
static constexpr uint16_t COL_ORANGE   = 0xFD20;
static constexpr uint16_t COL_WHITE    = 0xFFFF;
static constexpr uint16_t COL_LGRAY    = 0xC618;
static constexpr uint16_t COL_DGRAY    = 0x4208;
static constexpr uint16_t COL_NEEDLE   = 0xF800;  // Red
static constexpr uint16_t COL_DISCONN  = 0xFDA0;  // Orange

// =============================================================================
// Initialization
// =============================================================================

void GaugeDisplay::begin() {
    // Enable LCD power
    pinMode(PIN_LCD_PWR_EN1, OUTPUT);
    pinMode(PIN_LCD_PWR_EN2, OUTPUT);
    digitalWrite(PIN_LCD_PWR_EN1, HIGH);
    digitalWrite(PIN_LCD_PWR_EN2, HIGH);

    _tft.init();
    _tft.setRotation(0);

    // Backlight PWM
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    setBrightness(70);

    // Full-screen sprite in PSRAM
    _sprite.setColorDepth(16);
    _sprite.setPsram(true);
    _sprite.createSprite(240, 240);

    _tft.fillScreen(COL_BG);
    _prevValue = -99999.0f;
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(0, (percent * 255) / 100);
}

// =============================================================================
// Convert value to angle (LovyanGFX: 0°=top, CW)
// =============================================================================

float GaugeDisplay::valueToAngle(const GaugeConfig& gauge, float value) {
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);
    // Start at 225°, sweep 270° clockwise. Handle wrap past 360°.
    float angle = ARC_START + ARC_SWEEP * frac;
    if (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

uint16_t GaugeDisplay::valueToColor(const GaugeConfig& gauge, float value) {
    if (value >= gauge.dangerVal) return COL_RED;
    if (value >= gauge.warnVal)   return COL_YELLOW;
    return COL_WHITE;
}

// =============================================================================
// Main draw routine
// =============================================================================

void GaugeDisplay::drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw) {
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    // Skip if value barely changed
    if (!forceRedraw && fabsf(clamped - _prevValue) < (gauge.maxVal - gauge.minVal) * 0.003f) {
        return;
    }
    _prevValue = clamped;

    // --- Clear sprite ---
    _sprite.fillSprite(COL_BG);

    // --- Dial background circle ---
    _sprite.fillCircle(CX, CY, R_ARC_OUTER + 2, COL_DIAL_BG);
    _sprite.fillCircle(CX, CY, R_ARC_INNER - 1, COL_BG);

    // --- Colored arc segments using fillArc (no gaps!) ---
    float range = gauge.maxVal - gauge.minVal;
    float warnFrac   = (gauge.warnVal - gauge.minVal) / range;
    float dangerFrac = (gauge.dangerVal - gauge.minVal) / range;

    // Calculate arc segment angles (LovyanGFX convention)
    float greenEnd  = ARC_START + ARC_SWEEP * warnFrac;
    float yellowEnd = ARC_START + ARC_SWEEP * dangerFrac;
    float redEnd    = ARC_START + ARC_SWEEP;

    // Green zone
    _sprite.fillArc(CX, CY, R_ARC_OUTER, R_ARC_INNER,
                    fmodf(ARC_START, 360.0f), fmodf(greenEnd, 360.0f), COL_GREEN);

    // Yellow zone
    _sprite.fillArc(CX, CY, R_ARC_OUTER, R_ARC_INNER,
                    fmodf(greenEnd, 360.0f), fmodf(yellowEnd, 360.0f), COL_YELLOW);

    // Red zone
    _sprite.fillArc(CX, CY, R_ARC_OUTER, R_ARC_INNER,
                    fmodf(yellowEnd, 360.0f), fmodf(redEnd, 360.0f), COL_RED);

    // --- Tick marks and number labels ---
    drawTicksAndLabels(gauge);

    // --- Needle ---
    float needleAngle = valueToAngle(gauge, clamped);
    drawNeedle(needleAngle);

    // --- Center hub ---
    _sprite.fillCircle(CX, CY, R_NEEDLE_HUB, COL_DGRAY);
    _sprite.fillCircle(CX, CY, R_NEEDLE_HUB - 2, COL_ORANGE);

    // --- Gauge name (top area, inside the arc) ---
    _sprite.setTextColor(COL_WHITE);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setFont(&fonts::FreeSansBold9pt7b);
    _sprite.drawString(gauge.name, CX, 40);

    // --- Units label (just above center) ---
    _sprite.setFont(&fonts::Font2);
    _sprite.setTextColor(COL_LGRAY);
    _sprite.setTextDatum(BC_DATUM);
    _sprite.drawString(gauge.units, CX, CY - 18);

    // --- Numeric value (below center) ---
    char valBuf[16];
    if (gauge.maxVal >= 1000) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", clamped);
    } else if (gauge.pid == 0x42) {
        snprintf(valBuf, sizeof(valBuf), "%.1f", clamped);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.0f", clamped);
    }
    uint16_t valColor = valueToColor(gauge, clamped);
    _sprite.setTextColor(valColor);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setFont(&fonts::FreeSansBold18pt7b);
    _sprite.drawString(valBuf, CX, CY + 12);

    // --- Push to display ---
    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawConnectionStatus(bool connected) {
    if (connected == _prevConnected) return;
    _prevConnected = connected;
    // Status is drawn as part of the gauge redraw cycle;
    // force a full redraw on next frame
    if (!connected) _prevValue = -99999.0f;
}

// =============================================================================
// Tick marks and number labels
// =============================================================================

void GaugeDisplay::drawTicksAndLabels(const GaugeConfig& gauge) {
    float range = gauge.maxVal - gauge.minVal;

    // Calculate nice major tick spacing
    float majorStep;
    if      (range >= 5000) majorStep = 1000;
    else if (range >= 2000) majorStep = 500;
    else if (range >= 500)  majorStep = 100;
    else if (range >= 200)  majorStep = 50;
    else if (range >= 100)  majorStep = 20;
    else if (range >= 50)   majorStep = 10;
    else if (range >= 20)   majorStep = 5;
    else                    majorStep = 2;

    float minorStep = majorStep / 5.0f;

    // Draw minor ticks first (underneath major ticks)
    for (float v = gauge.minVal; v <= gauge.maxVal + 0.001f; v += minorStep) {
        float frac = (v - gauge.minVal) / range;
        float deg = ARC_START + ARC_SWEEP * frac;

        float ox = arcX(CX, R_TICK_OUTER, deg);
        float oy = arcY(CY, R_TICK_OUTER, deg);
        float ix = arcX(CX, R_TICK_MINOR, deg);
        float iy = arcY(CY, R_TICK_MINOR, deg);
        _sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, COL_DGRAY);
    }

    // Draw major ticks with labels
    for (float v = gauge.minVal; v <= gauge.maxVal + 0.001f; v += majorStep) {
        float frac = (v - gauge.minVal) / range;
        float deg = ARC_START + ARC_SWEEP * frac;

        // Major tick line (thicker: draw 2 adjacent lines)
        float ox = arcX(CX, R_TICK_OUTER, deg);
        float oy = arcY(CY, R_TICK_OUTER, deg);
        float ix = arcX(CX, R_TICK_MAJOR, deg);
        float iy = arcY(CY, R_TICK_MAJOR, deg);
        _sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, COL_WHITE);
        // Slight offset for thickness
        _sprite.drawLine((int)ox + 1, (int)oy, (int)ix + 1, (int)iy, COL_WHITE);

        // Number label
        float lx = arcX(CX, R_LABEL, deg);
        float ly = arcY(CY, R_LABEL, deg);

        char label[8];
        // For large ranges, show abbreviated (e.g., "1" instead of "1000")
        if (majorStep >= 1000) {
            snprintf(label, sizeof(label), "%.0f", v / 1000.0f);
        } else if (majorStep >= 100 && range >= 1000) {
            snprintf(label, sizeof(label), "%.0f", v / 1000.0f);
        } else {
            snprintf(label, sizeof(label), "%.0f", v);
        }

        _sprite.setFont(&fonts::FreeSansBold9pt7b);
        _sprite.setTextColor(COL_WHITE);
        _sprite.setTextDatum(MC_DATUM);
        _sprite.drawString(label, (int)lx, (int)ly);
    }
}

// =============================================================================
// Needle (solid triangle)
// =============================================================================

void GaugeDisplay::drawNeedle(float angleDeg) {
    // Needle tip
    float tipX = arcX(CX, R_NEEDLE, angleDeg);
    float tipY = arcY(CY, R_NEEDLE, angleDeg);

    // Needle tail (opposite direction)
    float tailX = arcX(CX, R_NEEDLE_TAIL, angleDeg + 180.0f);
    float tailY = arcY(CY, R_NEEDLE_TAIL, angleDeg + 180.0f);

    // Base points (perpendicular to needle direction, at center)
    float perpDeg = angleDeg + 90.0f;
    float baseHalfWidth = 3.5f;
    float b1x = arcX(CX, (int)baseHalfWidth, perpDeg);
    float b1y = arcY(CY, (int)baseHalfWidth, perpDeg);
    float b2x = arcX(CX, (int)baseHalfWidth, perpDeg + 180.0f);
    float b2y = arcY(CY, (int)baseHalfWidth, perpDeg + 180.0f);

    // Draw needle as two triangles (tip half + tail half)
    _sprite.fillTriangle((int)tipX, (int)tipY, (int)b1x, (int)b1y, (int)b2x, (int)b2y, COL_NEEDLE);
    _sprite.fillTriangle((int)tailX, (int)tailY, (int)b1x, (int)b1y, (int)b2x, (int)b2y, COL_ORANGE);
}
