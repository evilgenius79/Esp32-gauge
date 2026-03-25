#include "gauge_render.h"
#include <cmath>

// =============================================================================
// Constants
// =============================================================================

static constexpr float ARC_START = 225.0f;   // 7:30 position (0°=top, CW)
static constexpr float ARC_SWEEP = 270.0f;   // Full sweep to 4:30

// =============================================================================
// Color Palette (RGB565)
// =============================================================================

static constexpr uint16_t C_BLACK   = 0x0000;
static constexpr uint16_t C_DKGRAY  = 0x2104;
static constexpr uint16_t C_GRAY    = 0x4A49;
static constexpr uint16_t C_WHITE   = 0xFFFF;
static constexpr uint16_t C_RED     = 0xF800;
static constexpr uint16_t C_ORANGE  = 0xFB20;
static constexpr uint16_t C_YELLOW  = 0xFFE0;

// =============================================================================
// Bloom glow palette — smooth gradient from near-black to full brightness
// 10 steps for soft neon bloom effect
// =============================================================================

static constexpr int BLOOM_STEPS = 10;

// Red bloom: outer (faintest) to inner (brightest)
static constexpr uint16_t BLOOM_RED[BLOOM_STEPS] = {
    0x0800, 0x1000, 0x2000, 0x3000, 0x4800,
    0x6000, 0x7800, 0x9800, 0xB800, 0xD800,
};

// Orange bloom for center ring
static constexpr uint16_t BLOOM_ORG[BLOOM_STEPS] = {
    0x1080, 0x2100, 0x3180, 0x4200, 0x5280,
    0x6B00, 0x8340, 0x9BA0, 0xBBC0, 0xDB40,
};

// =============================================================================
// Trig helpers — 0° = top (12 o'clock), clockwise positive
// =============================================================================

static inline float gpx(int cx, int r, float deg) {
    return cx + r * sinf(deg * DEG_TO_RAD);
}
static inline float gpy(int cy, int r, float deg) {
    return cy - r * cosf(deg * DEG_TO_RAD);
}

// =============================================================================
// RGB565 color interpolation
// =============================================================================

static uint16_t lerpColor565(uint16_t c1, uint16_t c2, float t) {
    if (t <= 0.0f) return c1;
    if (t >= 1.0f) return c2;
    int r1 = (c1 >> 11) & 0x1F, g1 = (c1 >> 5) & 0x3F, b1 = c1 & 0x1F;
    int r2 = (c2 >> 11) & 0x1F, g2 = (c2 >> 5) & 0x3F, b2 = c2 & 0x1F;
    int r = r1 + (int)((r2 - r1) * t);
    int g = g1 + (int)((g2 - g1) * t);
    int b = b1 + (int)((b2 - b1) * t);
    return (r << 11) | (g << 5) | b;
}

// =============================================================================
// GaugeLayout factory methods
// =============================================================================

GaugeLayout GaugeLayout::fromSize(int size) {
    return fromRadius(size / 2, size / 2, size / 2);
}

GaugeLayout GaugeLayout::fromRadius(int cx, int cy, int radius) {
    GaugeLayout L;
    L.cx = cx;
    L.cy = cy;
    float s = radius / 120.0f;  // 120 = half of 240px reference
    L.scale = s;

    L.rOuter    = (int)(117 * s);
    L.rTickOut  = (int)(113 * s);
    L.rTickMaj  = (int)(100 * s);
    L.rTickMin  = (int)(106 * s);
    L.rLabel    = (int)(85 * s);
    L.rNeedle   = (int)(97 * s);
    L.rTail     = (int)(15 * s);
    L.rRingOut  = (int)(30 * s);
    L.rRingIn   = (int)(20 * s);
    L.rSweepOut = (int)(116 * s);
    L.rSweepIn  = (int)(108 * s);

    // Wider bloom on bigger gauges (min 6px, max ~13px at 320px)
    L.bloomWidth = max(6, (int)(10 * s));

    return L;
}

// =============================================================================
// Angle mapping
// =============================================================================

float gaugeValueToAngle(const GaugeConfig& gauge, float value) {
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);
    float angle = ARC_START + ARC_SWEEP * frac;
    if (angle >= 360.0f) angle -= 360.0f;
    return angle;
}

// =============================================================================
// Gauge Face — outer ring, tick marks, numbers
// =============================================================================

void renderFace(LGFX_Sprite& sprite, const GaugeLayout& L, const GaugeConfig& gauge) {
    int cx = L.cx, cy = L.cy;

    // Subtle dark outer ring
    sprite.fillArc(cx, cy, L.rOuter, L.rOuter - max(1, (int)(2 * L.scale)), 0, 360, C_DKGRAY);

    float range = gauge.maxVal - gauge.minVal;
    int numMajor = gauge.majorDivisions;
    int numMinor = numMajor * 5;
    float dangerFrac = (gauge.dangerVal - gauge.minVal) / range;

    // Tick thickness (1px at 240, 2px at 320+)
    int tickThick = max(1, (int)(L.scale));

    // --- Minor ticks ---
    for (int i = 0; i <= numMinor; i++) {
        float frac = (float)i / numMinor;
        float deg = ARC_START + ARC_SWEEP * frac;

        float ox = gpx(cx, L.rTickOut, deg);
        float oy = gpy(cy, L.rTickOut, deg);
        float ix = gpx(cx, L.rTickMin, deg);
        float iy = gpy(cy, L.rTickMin, deg);

        uint16_t color = (frac >= dangerFrac) ? C_RED : C_GRAY;
        sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, color);
    }

    // --- Major ticks + numbers ---
    float majorStep = range / numMajor;
    for (int i = 0; i <= numMajor; i++) {
        float frac = (float)i / numMajor;
        float deg = ARC_START + ARC_SWEEP * frac;

        float ox = gpx(cx, L.rTickOut, deg);
        float oy = gpy(cy, L.rTickOut, deg);
        float ix = gpx(cx, L.rTickMaj, deg);
        float iy = gpy(cy, L.rTickMaj, deg);

        uint16_t tickColor = (frac >= dangerFrac) ? C_RED : C_WHITE;
        sprite.drawLine((int)ox, (int)oy, (int)ix, (int)iy, tickColor);

        // Extra lines for thickness
        float perpDeg = deg + 90.0f;
        float dx = sinf(perpDeg * DEG_TO_RAD);
        float dy = -cosf(perpDeg * DEG_TO_RAD);
        for (int t = 1; t < tickThick; t++) {
            sprite.drawLine((int)(ox + dx*t), (int)(oy + dy*t),
                            (int)(ix + dx*t), (int)(iy + dy*t), tickColor);
        }

        // Number label
        float lx = gpx(cx, L.rLabel, deg);
        float ly = gpy(cy, L.rLabel, deg);

        float val = gauge.minVal + i * majorStep;
        float displayVal = val / gauge.scaleDivisor;

        char label[8];
        if (fabsf(displayVal - roundf(displayVal)) < 0.01f) {
            snprintf(label, sizeof(label), "%.0f", displayVal);
        } else {
            snprintf(label, sizeof(label), "%.1f", displayVal);
        }

        sprite.setFont(&fonts::FreeSansBold9pt7b);
        sprite.setTextSize(L.scale);
        sprite.setTextDatum(MC_DATUM);
        sprite.setTextColor(tickColor);
        sprite.drawString(label, (int)lx, (int)ly);
    }

    sprite.setTextSize(1.0f);  // Reset
}

// =============================================================================
// Sweep Arc — soft bloom neon glow from start to current value
// =============================================================================

void renderSweepArc(LGFX_Sprite& sprite, const GaugeLayout& L,
                    const GaugeConfig& gauge, float value) {
    if (value <= gauge.minVal) return;

    int cx = L.cx, cy = L.cy;
    float frac = (value - gauge.minVal) / (gauge.maxVal - gauge.minVal);
    frac = constrain(frac, 0.0f, 1.0f);

    // Convert to LovyanGFX angles (0°=right, CW) by subtracting 90°
    float startDeg = ARC_START - 90.0f;
    float endDeg   = startDeg + ARC_SWEEP * frac;

    // Helper: draw arc layers for a given angular span
    auto drawBloom = [&](float s, float e) {
        int bw = L.bloomWidth;

        // Bloom layers — outer to inner, faintest to brightest
        for (int i = 0; i < BLOOM_STEPS; i++) {
            float t = (float)i / (BLOOM_STEPS - 1);           // 0..1
            int offset = bw - (int)(t * bw);                   // bw..0
            int rOut = L.rSweepOut + offset;
            int rIn  = L.rSweepIn  - offset;
            if (rIn < 1) rIn = 1;
            sprite.fillArc(cx, cy, rOut, rIn, s, e, BLOOM_RED[i]);
        }

        // Hot core layers
        sprite.fillArc(cx, cy, L.rSweepOut - 1, L.rSweepIn + 1, s, e, C_ORANGE);
        int coreInset = max(2, (int)(3 * L.scale));
        sprite.fillArc(cx, cy, L.rSweepOut - coreInset, L.rSweepIn + coreInset, s, e, C_YELLOW);
    };

    if (endDeg > 360.0f) {
        drawBloom(startDeg, 360.0f);
        drawBloom(0.0f, endDeg - 360.0f);
    } else {
        drawBloom(startDeg, endDeg);
    }
}

// =============================================================================
// Needle — multi-layer triangle with soft bloom glow
// =============================================================================

void renderNeedle(LGFX_Sprite& sprite, const GaugeLayout& L, float angleDeg) {
    int cx = L.cx, cy = L.cy;

    float tipX  = gpx(cx, L.rNeedle, angleDeg);
    float tipY  = gpy(cy, L.rNeedle, angleDeg);
    float tailX = gpx(cx, L.rTail, angleDeg + 180.0f);
    float tailY = gpy(cy, L.rTail, angleDeg + 180.0f);

    float perpDeg = angleDeg + 90.0f;
    float sinP = sinf(perpDeg * DEG_TO_RAD);
    float cosP = cosf(perpDeg * DEG_TO_RAD);

    // Bloom layers on needle (5 layers, widest = faintest)
    struct { float width; uint16_t color; } layers[] = {
        { 9.0f * L.scale,  BLOOM_RED[1] },   // Outermost faint glow
        { 7.0f * L.scale,  BLOOM_RED[3] },   // Mid-outer glow
        { 5.0f * L.scale,  BLOOM_RED[5] },   // Mid glow
        { 3.5f * L.scale,  BLOOM_RED[7] },   // Inner glow
        { 2.5f * L.scale,  C_ORANGE },        // Main needle body
    };

    for (auto& l : layers) {
        float w = l.width;
        float g1x = cx + w * sinP, g1y = cy - w * cosP;
        float g2x = cx - w * sinP, g2y = cy + w * cosP;
        sprite.fillTriangle((int)tipX, (int)tipY,
                            (int)g1x, (int)g1y, (int)g2x, (int)g2y, l.color);
        sprite.fillTriangle((int)tailX, (int)tailY,
                            (int)g1x, (int)g1y, (int)g2x, (int)g2y, l.color);
    }

    // Hot center line (yellow core for neon pop)
    sprite.drawLine(cx, cy, (int)tipX, (int)tipY, C_YELLOW);
    if (L.scale >= 1.2f) {
        // Thicker core line on larger gauges
        sprite.drawLine(cx+1, cy, (int)tipX+1, (int)tipY, C_YELLOW);
    }
}

// =============================================================================
// Center Ring — concentric bloom rings
// =============================================================================

void renderCenterRing(LGFX_Sprite& sprite, const GaugeLayout& L) {
    int cx = L.cx, cy = L.cy;
    int bw = max(4, (int)(8 * L.scale));  // bloom width for ring

    // Bloom layers — outer to inner
    for (int i = 0; i < BLOOM_STEPS; i++) {
        float t = (float)i / (BLOOM_STEPS - 1);
        int offset = bw - (int)(t * bw);
        int rOut = L.rRingOut + offset;
        int rIn  = L.rRingIn  - offset;
        if (rIn < 1) rIn = 1;
        sprite.fillArc(cx, cy, rOut, rIn, 0, 360, BLOOM_ORG[i]);
    }

    // Main ring (bright orange)
    sprite.fillArc(cx, cy, L.rRingOut, L.rRingIn, 0, 360, C_ORANGE);

    // Yellow highlight on inner edge
    int highlightInset = max(1, (int)(2 * L.scale));
    sprite.fillArc(cx, cy, L.rRingOut - highlightInset,
                   L.rRingIn + highlightInset, 0, 360, C_YELLOW);

    // Inner dark fill
    sprite.fillCircle(cx, cy, L.rRingIn - 1, C_BLACK);

    // Small center dot
    int dotR = max(3, (int)(4 * L.scale));
    sprite.fillCircle(cx, cy, dotR, C_ORANGE);
}

// =============================================================================
// Labels — scale label, numeric value, units
// =============================================================================

void renderLabels(LGFX_Sprite& sprite, const GaugeLayout& L,
                  const GaugeConfig& gauge, float value) {
    int cx = L.cx, cy = L.cy;

    // Scale label (e.g. "x1000r/min") — above center ring
    sprite.setFont(&fonts::Font2);
    sprite.setTextSize(L.scale);
    sprite.setTextDatum(BC_DATUM);
    sprite.setTextColor(C_RED);
    sprite.drawString(gauge.scaleLabel, cx, cy - L.rRingOut - (int)(8 * L.scale));

    // Numeric value — below center ring, large white
    char valBuf[16];
    if (gauge.decimals == 0) {
        snprintf(valBuf, sizeof(valBuf), "%.0f", value);
    } else {
        snprintf(valBuf, sizeof(valBuf), "%.*f", gauge.decimals, value);
    }

    sprite.setFont(&fonts::FreeSansBold12pt7b);
    sprite.setTextSize(L.scale);
    sprite.setTextDatum(TC_DATUM);
    sprite.setTextColor(C_WHITE);
    sprite.drawString(valBuf, cx, cy + L.rRingOut + (int)(10 * L.scale));

    // Units label — below numeric value
    sprite.setFont(&fonts::Font2);
    sprite.setTextSize(L.scale);
    sprite.setTextDatum(TC_DATUM);
    sprite.setTextColor(C_RED);
    sprite.drawString(gauge.units, cx, cy + L.rRingOut + (int)(38 * L.scale));

    sprite.setTextSize(1.0f);  // Reset
}

// =============================================================================
// Full gauge render — composites all layers
// =============================================================================

void renderGauge(LGFX_Sprite& sprite, const GaugeLayout& L,
                 const GaugeConfig& gauge, float value) {
    float clamped = constrain(value, gauge.minVal, gauge.maxVal);

    renderFace(sprite, L, gauge);
    renderSweepArc(sprite, L, gauge, clamped);
    renderNeedle(sprite, L, gaugeValueToAngle(gauge, clamped));
    renderCenterRing(sprite, L);
    renderLabels(sprite, L, gauge, clamped);
}
