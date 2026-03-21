#include "display.h"
#include <Arduino.h>
#include <cmath>

// =============================================================================
// Constants for gauge geometry
// =============================================================================
static constexpr int SCREEN_W        = 240;
static constexpr int SCREEN_H        = 240;
static constexpr int CENTER_X        = 120;
static constexpr int CENTER_Y        = 120;
static constexpr int GAUGE_RADIUS    = 110;
static constexpr int ARC_THICKNESS   = 12;
static constexpr int NEEDLE_LENGTH   = 85;
static constexpr int TICK_OUTER      = 98;
static constexpr int TICK_INNER_MAJ  = 82;
static constexpr int TICK_INNER_MIN  = 88;

// Gauge arc sweep: 270 degrees, starting from 135° (bottom-left) to 45° (bottom-right)
static constexpr float ARC_START_DEG = 135.0f;
static constexpr float ARC_SWEEP_DEG = 270.0f;

// Colors
static constexpr uint16_t COL_BG        = 0x0000; // Black
static constexpr uint16_t COL_DIAL_BG   = 0x18E3; // Dark gray
static constexpr uint16_t COL_GREEN     = 0x07E0;
static constexpr uint16_t COL_YELLOW    = 0xFFE0;
static constexpr uint16_t COL_RED       = 0xF800;
static constexpr uint16_t COL_WHITE     = 0xFFFF;
static constexpr uint16_t COL_NEEDLE    = 0xF800; // Red needle
static constexpr uint16_t COL_NEEDLE_HUB= 0xC618; // Light gray hub
static constexpr uint16_t COL_TEXT      = 0xFFFF;
static constexpr uint16_t COL_UNITS     = 0xB596; // Medium gray
static constexpr uint16_t COL_DISCONN   = 0xFDA0; // Orange

static inline float degToRad(float d) { return d * M_PI / 180.0f; }

// =============================================================================
// Public Methods
// =============================================================================

void GaugeDisplay::begin() {
    // Enable LCD power
    pinMode(PIN_LCD_PWR_EN1, OUTPUT);
    pinMode(PIN_LCD_PWR_EN2, OUTPUT);
    digitalWrite(PIN_LCD_PWR_EN1, HIGH);
    digitalWrite(PIN_LCD_PWR_EN2, HIGH);

    _tft.init();
    _tft.setRotation(0);

    // Set up backlight PWM
    ledcSetup(0, 5000, 8);
    ledcAttachPin(PIN_TFT_BL, 0);
    setBrightness(70);

    // Create full-screen sprite in PSRAM for flicker-free rendering
    _sprite.setColorDepth(16);
    _sprite.setPsram(true);
    _sprite.createSprite(SCREEN_W, SCREEN_H);

    // Initial clear
    _tft.fillScreen(COL_BG);
    _prevGaugeIdx = -1;
    _prevValue = -99999.0f;
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(0, (percent * 255) / 100);
}

void GaugeDisplay::drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw) {
    // Clamp value to gauge range
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    // Skip redraw if value hasn't changed much (reduces flicker, saves CPU)
    if (!forceRedraw && fabsf(clamped - _prevValue) < (gauge.maxVal - gauge.minVal) * 0.002f) {
        return;
    }
    _prevValue = clamped;

    // --- Draw everything to sprite ---
    _sprite.fillSprite(COL_BG);

    // Draw circular background
    _sprite.fillCircle(CENTER_X, CENTER_Y, GAUGE_RADIUS + 2, COL_DIAL_BG);
    _sprite.fillCircle(CENTER_X, CENTER_Y, GAUGE_RADIUS - ARC_THICKNESS - 4, COL_BG);

    // Draw colored arc segments
    float range = gauge.maxVal - gauge.minVal;
    float warnFrac   = (gauge.warnVal - gauge.minVal) / range;
    float dangerFrac = (gauge.dangerVal - gauge.minVal) / range;

    // Green zone: 0 to warn
    float greenEnd = ARC_START_DEG + ARC_SWEEP_DEG * warnFrac;
    drawArc(CENTER_X, CENTER_Y, GAUGE_RADIUS, GAUGE_RADIUS - ARC_THICKNESS,
            ARC_START_DEG, greenEnd, COL_GREEN);

    // Yellow zone: warn to danger
    float yellowEnd = ARC_START_DEG + ARC_SWEEP_DEG * dangerFrac;
    drawArc(CENTER_X, CENTER_Y, GAUGE_RADIUS, GAUGE_RADIUS - ARC_THICKNESS,
            greenEnd, yellowEnd, COL_YELLOW);

    // Red zone: danger to max
    float redEnd = ARC_START_DEG + ARC_SWEEP_DEG;
    drawArc(CENTER_X, CENTER_Y, GAUGE_RADIUS, GAUGE_RADIUS - ARC_THICKNESS,
            yellowEnd, redEnd, COL_RED);

    // Draw scale tick marks and labels
    drawScaleMarks(CENTER_X, CENTER_Y, TICK_OUTER, gauge.minVal, gauge.maxVal,
                   ARC_START_DEG, ARC_SWEEP_DEG);

    // Draw needle
    float needleAngle = valueToAngle(gauge, clamped);
    drawNeedle(CENTER_X, CENTER_Y, needleAngle, NEEDLE_LENGTH, COL_NEEDLE);

    // Draw center hub
    _sprite.fillCircle(CENTER_X, CENTER_Y, 8, COL_NEEDLE_HUB);
    _sprite.fillCircle(CENTER_X, CENTER_Y, 5, COL_NEEDLE);

    // Draw gauge name (top)
    _sprite.setTextColor(COL_TEXT);
    _sprite.setTextDatum(TC_DATUM);
    _sprite.setFont(&fonts::Font4);
    _sprite.drawString(gauge.name, CENTER_X, 38);

    // Draw numeric value (center)
    char valBuf[16];
    if (gauge.maxVal >= 1000) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", clamped);
    } else if (gauge.maxVal >= 100) {
        snprintf(valBuf, sizeof(valBuf), "%.1f", clamped);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.1f", clamped);
    }
    _sprite.setFont(&fonts::Font7);
    _sprite.setTextDatum(MC_DATUM);
    uint16_t valColor = valueToColor(gauge, clamped);
    _sprite.setTextColor(valColor);
    _sprite.drawString(valBuf, CENTER_X, CENTER_Y + 10);

    // Draw units (below value)
    _sprite.setFont(&fonts::Font2);
    _sprite.setTextColor(COL_UNITS);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString(gauge.units, CENTER_X, CENTER_Y + 45);

    // Push sprite to display
    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawConnectionStatus(bool connected) {
    if (connected == _prevConnected) return;
    _prevConnected = connected;

    if (!connected) {
        // Draw small "NO CAN" indicator at bottom
        _sprite.setFont(&fonts::Font2);
        _sprite.setTextColor(COL_DISCONN);
        _sprite.setTextDatum(BC_DATUM);
        _sprite.drawString("NO CAN", CENTER_X, SCREEN_H - 10);
        _sprite.pushSprite(&_tft, 0, 0);
    }
}

// =============================================================================
// Private Drawing Helpers
// =============================================================================

float GaugeDisplay::valueToAngle(const GaugeConfig& gauge, float value) {
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);
    return ARC_START_DEG + ARC_SWEEP_DEG * frac;
}

uint16_t GaugeDisplay::valueToColor(const GaugeConfig& gauge, float value) {
    if (value >= gauge.dangerVal) return COL_RED;
    if (value >= gauge.warnVal)   return COL_YELLOW;
    return COL_WHITE;
}

void GaugeDisplay::drawArc(int cx, int cy, int r_outer, int r_inner,
                            float startAngle, float endAngle, uint16_t color) {
    // Draw arc by filling pixels between inner and outer radius
    for (float angle = startAngle; angle <= endAngle; angle += 0.8f) {
        float rad = degToRad(angle);
        float cosA = cosf(rad);
        float sinA = sinf(rad);

        int x1 = cx + (int)(r_inner * cosA);
        int y1 = cy + (int)(r_inner * sinA);
        int x2 = cx + (int)(r_outer * cosA);
        int y2 = cy + (int)(r_outer * sinA);

        _sprite.drawLine(x1, y1, x2, y2, color);
    }
}

void GaugeDisplay::drawNeedle(int cx, int cy, float angle, int length, uint16_t color) {
    float rad = degToRad(angle);
    float cosA = cosf(rad);
    float sinA = sinf(rad);

    int tipX = cx + (int)(length * cosA);
    int tipY = cy + (int)(length * sinA);

    // Draw thick needle (3 lines for width)
    float perpRad = rad + M_PI / 2.0f;
    float pw = 2.0f; // half-width

    for (float w = -pw; w <= pw; w += 0.5f) {
        int sx = cx + (int)(w * cosf(perpRad));
        int sy = cy + (int)(w * sinf(perpRad));
        _sprite.drawLine(sx, sy, tipX, tipY, color);
    }

    // Draw needle tail (shorter, opposite direction)
    int tailLen = 15;
    int tailX = cx - (int)(tailLen * cosA);
    int tailY = cy - (int)(tailLen * sinA);
    for (float w = -pw; w <= pw; w += 0.5f) {
        int sx = cx + (int)(w * cosf(perpRad));
        int sy = cy + (int)(w * sinf(perpRad));
        _sprite.drawLine(sx, sy, tailX, tailY, color);
    }
}

void GaugeDisplay::drawScaleMarks(int cx, int cy, int radius,
                                   float minVal, float maxVal,
                                   float startAngle, float sweepAngle) {
    float range = maxVal - minVal;

    // Determine nice tick spacing based on range
    float majorStep;
    if (range >= 5000)      majorStep = 1000;
    else if (range >= 2000) majorStep = 500;
    else if (range >= 500)  majorStep = 100;
    else if (range >= 200)  majorStep = 50;
    else if (range >= 100)  majorStep = 20;
    else if (range >= 50)   majorStep = 10;
    else if (range >= 20)   majorStep = 5;
    else                    majorStep = 2;

    // Draw major ticks with labels
    for (float v = minVal; v <= maxVal + 0.01f; v += majorStep) {
        float frac = (v - minVal) / range;
        float angle = startAngle + sweepAngle * frac;
        float rad = degToRad(angle);
        float cosA = cosf(rad);
        float sinA = sinf(rad);

        // Major tick line
        int ox = cx + (int)(radius * cosA);
        int oy = cy + (int)(radius * sinA);
        int ix = cx + (int)(TICK_INNER_MAJ * cosA);
        int iy = cy + (int)(TICK_INNER_MAJ * sinA);
        _sprite.drawLine(ox, oy, ix, iy, COL_WHITE);

        // Label
        int lx = cx + (int)((TICK_INNER_MAJ - 12) * cosA);
        int ly = cy + (int)((TICK_INNER_MAJ - 12) * sinA);
        _sprite.setFont(&fonts::Font0);
        _sprite.setTextColor(COL_WHITE);
        _sprite.setTextDatum(MC_DATUM);

        char label[8];
        if (majorStep >= 1.0f) {
            snprintf(label, sizeof(label), "%.0f", v);
        } else {
            snprintf(label, sizeof(label), "%.1f", v);
        }
        _sprite.drawString(label, lx, ly);
    }

    // Draw minor ticks (5 per major division)
    float minorStep = majorStep / 5.0f;
    for (float v = minVal; v <= maxVal + 0.01f; v += minorStep) {
        float frac = (v - minVal) / range;
        float angle = startAngle + sweepAngle * frac;
        float rad = degToRad(angle);
        float c = cosf(rad);
        float s = sinf(rad);

        int ox = cx + (int)(radius * c);
        int oy = cy + (int)(radius * s);
        int ix = cx + (int)(TICK_INNER_MIN * c);
        int iy = cy + (int)(TICK_INNER_MIN * s);
        _sprite.drawLine(ox, oy, ix, iy, COL_UNITS);
    }
}
