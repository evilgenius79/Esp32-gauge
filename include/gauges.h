#pragma once
#include <cstdint>

// =============================================================================
// OBD2 Gauge Definitions
// =============================================================================

struct GaugeConfig {
    const char* name;           // Display name
    const char* units;          // Unit string for numeric readout
    const char* scaleLabel;     // Label near scale (e.g., "x1000r/min")
    uint8_t     pid;            // OBD2 PID (Mode 01)
    float       minVal;         // Minimum display value
    float       maxVal;         // Maximum display value
    float       warnVal;        // Warning threshold (yellow zone start)
    float       dangerVal;      // Danger threshold (red zone start)
    float       scaleDivisor;   // Divide value by this for dial numbers
    int         majorDivisions; // Number of major scale divisions
    uint8_t     dataBytes;      // Number of data bytes in response (1 or 2)
    uint8_t     decimals;       // Decimal places for numeric readout
};

// Decode raw OBD2 response bytes into a physical value
inline float decodeOBD2(const GaugeConfig& gauge, uint8_t a, uint8_t b) {
    switch (gauge.pid) {
        case 0x04: return a * 100.0f / 255.0f;           // Engine Load %
        case 0x05: return (float)a - 40.0f;               // Coolant Temp C
        case 0x0B: return (float)a;                        // MAP kPa
        case 0x0C: return (256.0f * a + b) / 4.0f;        // RPM
        case 0x0D: return (float)a;                        // Speed km/h
        case 0x0F: return (float)a - 40.0f;               // Intake Temp C
        case 0x11: return a * 100.0f / 255.0f;            // Throttle %
        case 0x42: return (256.0f * a + b) / 1000.0f;     // Voltage V
        default:   return (float)a;
    }
}

constexpr int NUM_GAUGES = 8;

const GaugeConfig GAUGES[NUM_GAUGES] = {
    //  name        units       scaleLabel       pid   min  max   warn  danger divisor divs bytes dec
    { "RPM",       "rpm",      "x1000r/min",    0x0C,  0,  8000, 5500, 6500, 1000, 8,  2,  0 },
    { "SPEED",     "km/h",     "km/h",          0x0D,  0,   260,  180,  220,   20, 13, 1,  0 },
    { "COOLANT",   "\xB0""C",  "\xB0""C",       0x05, 40,   130,  100,  115,   10,  9, 1,  0 },
    { "BOOST",     "kPa",      "kPa",           0x0B,  0,   255,  180,  220,   50,  5, 1,  0 },
    { "THROTTLE",  "%",        "%",             0x11,  0,   100,   80,   95,   10, 10, 1,  0 },
    { "LOAD",      "%",        "%",             0x04,  0,   100,   80,   95,   10, 10, 1,  0 },
    { "INTAKE",    "\xB0""C",  "\xB0""C",       0x0F,-40,    80,   60,   75,   20,  6, 1,  0 },
    { "VOLTAGE",   "V",        "Volts",         0x42,  8,    16,   15,   16,    1,  8, 2,  1 },
};
