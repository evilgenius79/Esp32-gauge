#pragma once

#ifdef TARGET_TAB5
#include <M5GFX.h>
#else
#include <LovyanGFX.hpp>
#endif
#include "gauges.h"

// =============================================================================
// Resolution-Independent Gauge Renderer
//
// All dimensions are derived from a single radius value, scaled from the
// 240x240 reference design (radius 120). Works at any size — 240px CrowPanel,
// 320px Tab5 cells, or anything else.
// =============================================================================

struct GaugeLayout {
    int cx, cy;         // Center of gauge in sprite coordinates
    float scale;        // 1.0 = 240px reference
    int bloomWidth;     // Glow bloom spread in pixels

    // Radii (all computed from scale)
    int rOuter, rTickOut, rTickMaj, rTickMin;
    int rLabel, rNeedle, rTail;
    int rRingOut, rRingIn;
    int rSweepOut, rSweepIn;

    // Create layout centered in a square of given size
    static GaugeLayout fromSize(int size);

    // Create layout with explicit center and radius
    static GaugeLayout fromRadius(int cx, int cy, int radius);
};

// Render a complete gauge into a sprite (must be pre-created and cleared)
void renderGauge(LGFX_Sprite& sprite, const GaugeLayout& L,
                 const GaugeConfig& gauge, float value);

// Individual rendering passes (for advanced use)
void renderFace(LGFX_Sprite& sprite, const GaugeLayout& L, const GaugeConfig& gauge);
void renderSweepArc(LGFX_Sprite& sprite, const GaugeLayout& L,
                    const GaugeConfig& gauge, float value);
void renderNeedle(LGFX_Sprite& sprite, const GaugeLayout& L, float angleDeg);
void renderCenterRing(LGFX_Sprite& sprite, const GaugeLayout& L);
void renderLabels(LGFX_Sprite& sprite, const GaugeLayout& L,
                  const GaugeConfig& gauge, float value);

// Angle mapping (shared utility)
float gaugeValueToAngle(const GaugeConfig& gauge, float value);
