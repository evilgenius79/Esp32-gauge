#include "display.h"
#include <Arduino.h>
#include <cmath>

// =============================================================================
// Screen & gauge geometry
// =============================================================================
static constexpr int SW = 240;
static constexpr int SH = 240;
static constexpr int CX = 120;
static constexpr int CY = 120;

// Arc geometry: 270° sweep from bottom-left (135°) to bottom-right (45°)
static constexpr float ARC_START = 135.0f;
static constexpr float ARC_SWEEP = 270.0f;

// Radii
static constexpr int R_OUTER      = 118;  // Outer edge of tick area
static constexpr int R_TICK_MAJ   = 116;  // Major tick outer
static constexpr int R_TICK_MAJ_I = 102;  // Major tick inner
static constexpr int R_TICK_MIN   = 116;  // Minor tick outer
static constexpr int R_TICK_MIN_I = 108;  // Minor tick inner
static constexpr int R_NUMBERS    = 89;   // Number label radius
static constexpr int R_ARC_OUTER  = 118;  // Colored arc outer
static constexpr int R_ARC_INNER  = 112;  // Colored arc inner
static constexpr int R_NEEDLE     = 98;   // Needle tip radius
static constexpr int R_HUB_OUTER  = 28;   // Center hub outer ring
static constexpr int R_HUB_INNER  = 24;   // Center hub inner

// =============================================================================
// Colors — neon racing tachometer palette
// =============================================================================
// RGB565 helper
static constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static constexpr uint16_t COL_BG         = rgb565(2, 2, 12);     // Very dark blue-black
static constexpr uint16_t COL_DIAL_BG    = rgb565(8, 8, 35);     // Dark blue background
static constexpr uint16_t COL_TICK       = rgb565(220, 100, 40);  // Orange ticks
static constexpr uint16_t COL_TICK_DIM   = rgb565(120, 50, 20);   // Dim orange minor ticks
static constexpr uint16_t COL_NUM        = rgb565(255, 255, 255); // White numbers
static constexpr uint16_t COL_RED        = rgb565(255, 20, 20);   // Bright red
static constexpr uint16_t COL_RED_DIM    = rgb565(120, 5, 5);     // Dim red (inactive arc)
static constexpr uint16_t COL_RED_GLOW   = rgb565(200, 30, 10);   // Red glow
static constexpr uint16_t COL_NEEDLE     = rgb565(255, 40, 20);   // Bright red needle
static constexpr uint16_t COL_NEEDLE_DIM = rgb565(180, 25, 10);   // Needle edge
static constexpr uint16_t COL_HUB_RING   = rgb565(255, 60, 20);   // Hub ring (bright red-orange)
static constexpr uint16_t COL_HUB_GLOW   = rgb565(180, 20, 5);    // Hub glow
static constexpr uint16_t COL_HUB_CENTER = rgb565(255, 80, 30);   // Hub center bright
static constexpr uint16_t COL_VALUE      = rgb565(255, 255, 255); // Value text white
static constexpr uint16_t COL_UNITS_TXT  = rgb565(200, 200, 200); // Units text gray
static constexpr uint16_t COL_LABEL      = rgb565(255, 80, 30);   // Scale label (orange-red)
static constexpr uint16_t COL_WARN       = rgb565(255, 200, 0);   // Yellow warning
static constexpr uint16_t COL_NOCAN      = rgb565(255, 160, 0);   // Orange disconnected

static inline float degToRad(float d) { return d * M_PI / 180.0f; }

// =============================================================================
// Dim a 565 color by a factor (0-255, 255 = full brightness)
// =============================================================================
uint16_t GaugeDisplay::dimColor(uint16_t color, uint8_t factor) {
    uint8_t r = ((color >> 11) & 0x1F) * factor / 255;
    uint8_t g = ((color >> 5) & 0x3F) * factor / 255;
    uint8_t b = (color & 0x1F) * factor / 255;
    return (r << 11) | (g << 5) | b;
}

// =============================================================================
// Public methods
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
    _sprite.createSprite(SW, SH);

    _tft.fillScreen(COL_BG);
    _prevValue = -99999.0f;
}

void GaugeDisplay::setBrightness(uint8_t percent) {
    if (percent > 100) percent = 100;
    ledcWrite(0, (percent * 255) / 100);
}

float GaugeDisplay::valueToAngle(const GaugeConfig& gauge, float value) {
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    return ARC_START + ARC_SWEEP * frac;
}

void GaugeDisplay::drawGauge(const GaugeConfig& gauge, float value, bool forceRedraw) {
    float clamped = value;
    if (clamped < gauge.minVal) clamped = gauge.minVal;
    if (clamped > gauge.maxVal) clamped = gauge.maxVal;

    if (!forceRedraw && fabsf(clamped - _prevValue) < (gauge.maxVal - gauge.minVal) * 0.003f) {
        return;
    }
    _prevValue = clamped;

    // --- Clear sprite with dark blue-black background ---
    _sprite.fillSprite(COL_BG);

    // --- Circular dial background ---
    _sprite.fillCircle(CX, CY, R_OUTER, COL_DIAL_BG);

    // --- Draw the colored outer arc ---
    drawOuterRing(gauge);

    // --- Draw ticks and numbers ---
    drawTicksAndNumbers(gauge);

    // --- Scale label (e.g., "x1000r/min") ---
    _sprite.setTextColor(COL_LABEL);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.setFont(&fonts::Font0);
    _sprite.drawString(gauge.scaleLabel, CX + 18, CY - 28);

    // --- Draw needle ---
    float needleAngle = valueToAngle(gauge, clamped);
    drawNeedle(needleAngle, COL_NEEDLE);

    // --- Draw center hub with glow ring ---
    drawCenterHub(gauge, clamped);

    // --- Numeric value readout (lower right area) ---
    char valBuf[16];
    if (gauge.decimals == 0) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", clamped);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.*f", gauge.decimals, clamped);
    }

    // Value color: white normally, yellow at warn, red at danger
    uint16_t valCol = COL_VALUE;
    if (clamped >= gauge.dangerVal) valCol = COL_RED;
    else if (clamped >= gauge.warnVal) valCol = COL_WARN;

    _sprite.setTextColor(valCol);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.setFont(&fonts::Font7);
    _sprite.drawString(valBuf, CX + 15, CY + 38);

    // Units label below value
    _sprite.setTextColor(COL_UNITS_TXT);
    _sprite.setFont(&fonts::Font2);
    _sprite.setTextDatum(MC_DATUM);
    _sprite.drawString(gauge.units, CX + 15, CY + 60);

    // --- Connection status overlay ---
    if (!_prevConnected) {
        _sprite.setTextColor(COL_NOCAN);
        _sprite.setFont(&fonts::Font2);
        _sprite.setTextDatum(BC_DATUM);
        _sprite.drawString("NO CAN", CX, SH - 8);
    }

    // --- Push to screen ---
    _sprite.pushSprite(&_tft, 0, 0);
}

void GaugeDisplay::drawConnectionStatus(bool connected) {
    if (connected == _prevConnected) return;
    _prevConnected = connected;
    // Will be drawn on next gauge redraw via forceRedraw or naturally
}

// =============================================================================
// Draw the colored outer arc with dim/bright zones
// =============================================================================
void GaugeDisplay::drawOuterRing(const GaugeConfig& gauge) {
    float range = gauge.maxVal - gauge.minVal;
    float dangerFrac = (gauge.dangerVal - gauge.minVal) / range;
    float dangerAngle = ARC_START + ARC_SWEEP * dangerFrac;

    // Normal zone: dim orange-red
    drawGlowArc(CX, CY, R_ARC_OUTER, ARC_START, dangerAngle,
                COL_TICK_DIM, R_ARC_OUTER - R_ARC_INNER);

    // Danger zone: bright red
    drawGlowArc(CX, CY, R_ARC_OUTER, dangerAngle, ARC_START + ARC_SWEEP,
                COL_RED, R_ARC_OUTER - R_ARC_INNER);
}

// =============================================================================
// Draw tick marks and numbers around the dial
// =============================================================================
void GaugeDisplay::drawTicksAndNumbers(const GaugeConfig& gauge) {
    float range = gauge.maxVal - gauge.minVal;
    int numMajor = gauge.majorDivisions;
    float majorStep = range / numMajor;
    float minorStep = majorStep / 5.0f;

    // --- Minor ticks ---
    for (int i = 0; i <= numMajor * 5; i++) {
        float v = gauge.minVal + i * minorStep;
        if (v > gauge.maxVal + 0.01f) break;

        // Skip positions that are major ticks
        if (i % 5 == 0) continue;

        float frac = (v - gauge.minVal) / range;
        float angle = ARC_START + ARC_SWEEP * frac;
        float rad = degToRad(angle);
        float c = cosf(rad), s = sinf(rad);

        int ox = CX + (int)(R_TICK_MIN * c);
        int oy = CY + (int)(R_TICK_MIN * s);
        int ix = CX + (int)(R_TICK_MIN_I * c);
        int iy = CY + (int)(R_TICK_MIN_I * s);

        // Color: bright in danger zone, dim otherwise
        uint16_t col = (v >= gauge.dangerVal) ? COL_RED : COL_TICK_DIM;
        _sprite.drawLine(ox, oy, ix, iy, col);
    }

    // --- Major ticks + numbers ---
    _sprite.setFont(&fonts::DejaVu18);
    _sprite.setTextDatum(MC_DATUM);

    for (int i = 0; i <= numMajor; i++) {
        float v = gauge.minVal + i * majorStep;
        float frac = (float)i / numMajor;
        float angle = ARC_START + ARC_SWEEP * frac;
        float rad = degToRad(angle);
        float c = cosf(rad), s = sinf(rad);

        // Major tick line (thicker — draw 2 parallel lines)
        int ox = CX + (int)(R_TICK_MAJ * c);
        int oy = CY + (int)(R_TICK_MAJ * s);
        int ix = CX + (int)(R_TICK_MAJ_I * c);
        int iy = CY + (int)(R_TICK_MAJ_I * s);

        uint16_t tickCol = (v >= gauge.dangerVal) ? COL_RED : COL_TICK;
        _sprite.drawLine(ox, oy, ix, iy, tickCol);

        // Slightly offset parallel for thickness
        float perpR = rad + M_PI / 2.0f;
        float pc = cosf(perpR), ps = sinf(perpR);
        _sprite.drawLine(ox + (int)pc, oy + (int)ps, ix + (int)pc, iy + (int)ps, tickCol);

        // Number label
        int nx = CX + (int)(R_NUMBERS * c);
        int ny = CY + (int)(R_NUMBERS * s);

        char label[8];
        float displayVal = v / gauge.scaleDivisor;
        if (fabsf(displayVal - roundf(displayVal)) < 0.01f) {
            snprintf(label, sizeof(label), "%.0f", displayVal);
        } else {
            snprintf(label, sizeof(label), "%.1f", displayVal);
        }

        uint16_t numCol = (v >= gauge.dangerVal) ? COL_RED : COL_NUM;
        _sprite.setTextColor(numCol);
        _sprite.drawString(label, nx, ny);
    }
}

// =============================================================================
// Draw the needle with glow effect
// =============================================================================
void GaugeDisplay::drawNeedle(float angleDeg, uint16_t color) {
    float rad = degToRad(angleDeg);
    float c = cosf(rad), s = sinf(rad);
    float perpR = rad + M_PI / 2.0f;
    float pc = cosf(perpR), ps = sinf(perpR);

    int tipX = CX + (int)(R_NEEDLE * c);
    int tipY = CY + (int)(R_NEEDLE * s);

    // Needle tail
    int tailX = CX - (int)(18 * c);
    int tailY = CY - (int)(18 * s);

    // Draw glow (wider, dimmer)
    uint16_t glowCol = dimColor(color, 80);
    for (float w = -3.5f; w <= 3.5f; w += 0.7f) {
        int gx = CX + (int)(w * pc);
        int gy = CY + (int)(w * ps);
        _sprite.drawLine(gx, gy, tipX, tipY, glowCol);
        _sprite.drawLine(gx, gy, tailX, tailY, glowCol);
    }

    // Draw core needle (bright, narrow)
    for (float w = -1.5f; w <= 1.5f; w += 0.5f) {
        int nx = CX + (int)(w * pc);
        int ny = CY + (int)(w * ps);
        _sprite.drawLine(nx, ny, tipX, tipY, color);
        _sprite.drawLine(nx, ny, tailX, tailY, color);
    }
}

// =============================================================================
// Draw center hub with glowing red ring
// =============================================================================
void GaugeDisplay::drawCenterHub(const GaugeConfig& gauge, float value) {
    // Outer glow ring (multiple circles for glow effect)
    for (int r = R_HUB_OUTER + 4; r >= R_HUB_OUTER; r--) {
        uint8_t brightness = 40 + (R_HUB_OUTER + 4 - r) * 10;
        _sprite.drawCircle(CX, CY, r, dimColor(COL_HUB_RING, brightness));
    }

    // Main ring
    for (int r = R_HUB_OUTER; r >= R_HUB_INNER; r--) {
        // Gradient from bright edge to darker inside
        uint8_t brightness = 100 + (R_HUB_OUTER - r) * 30;
        if (brightness > 255) brightness = 255;
        _sprite.drawCircle(CX, CY, r, dimColor(COL_HUB_RING, brightness));
    }

    // Dark center fill
    _sprite.fillCircle(CX, CY, R_HUB_INNER - 1, COL_DIAL_BG);

    // Bright center dot
    _sprite.fillCircle(CX, CY, 4, COL_HUB_CENTER);
    _sprite.fillCircle(CX, CY, 2, COL_RED);
}

// =============================================================================
// Draw an arc with configurable thickness
// =============================================================================
void GaugeDisplay::drawGlowArc(int cx, int cy, int radius,
                                float startDeg, float endDeg,
                                uint16_t color, int thickness) {
    float step = 0.6f;
    for (float a = startDeg; a <= endDeg; a += step) {
        float rad = degToRad(a);
        float c = cosf(rad), s = sinf(rad);
        int ox = cx + (int)(radius * c);
        int oy = cy + (int)(radius * s);
        int ix = cx + (int)((radius - thickness) * c);
        int iy = cy + (int)((radius - thickness) * s);
        _sprite.drawLine(ox, oy, ix, iy, color);
    }
}
