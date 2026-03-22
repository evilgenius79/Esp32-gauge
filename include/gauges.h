#pragma once
#include <cstdint>

// =============================================================================
// OBD2 Gauge Definitions
// =============================================================================

struct GaugeConfig {
    const char* name;           // Display name
    const char* units;          // Unit string for numeric readout
    const char* scaleLabel;     // Label near scale (e.g., "x1000r/min")
    uint8_t     mode;           // OBD2 mode (0x01 standard, 0x22 enhanced)
    uint16_t    pid;            // OBD2 PID (1 byte for Mode 01, 2 bytes for Mode 22)
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
        case 0x05: return ((float)a - 40.0f) * 9.0f/5.0f + 32.0f;  // Coolant Temp °F
        case 0x0C: return (256.0f * a + b) / 4.0f;                 // RPM
        case 0x0D: return (float)a * 0.621371f;                     // Speed mph
        case 0x0F: return ((float)a - 40.0f) * 9.0f/5.0f + 32.0f;  // Intake Temp °F
        case 0x11: return a * 100.0f / 255.0f;            // Throttle %
        case 0x42: return (256.0f * a + b) / 1000.0f;     // Voltage V
        // Ford Enhanced PIDs (Mode 22)
        case 0x033E: {  // TIP (Throttle Inlet Pressure) - boost gauge pressure PSI
            float kpa = (256.0f * a + b) / 128.0f;
            return kpa * 0.14504f - 14.696f;
        }
        case 0x0451: {  // Charge Air Temp (IAT2) - post-intercooler, from MAP combo sensor
            float tempC = (float)((int16_t)(a << 8 | b)) / 64.0f;
            return tempC * 1.8f + 32.0f;  // Convert to °F
        }
        default:   return (float)a;
    }
}

constexpr int NUM_GAUGES = 9;

const GaugeConfig GAUGES[NUM_GAUGES] = {
    //  name        units       scaleLabel      mode  pid     min  max   warn  danger divisor divs bytes dec
    { "RPM",       "rpm",      "x1000r/min",   0x01, 0x0C,    0, 8000, 5500, 6500, 1000, 8,  2,  0 },
    { "SPEED",     "mph",      "SPEED",        0x01, 0x0D,    0,  160,  110,  140,    1,  8, 1,  0 },
    { "COOLANT",   "\xB0""F",  "COOLANT",      0x01, 0x05,  100,  260,  210,  240,    1,  4, 1,  0 },
    { "BOOST",     "psi",      "BOOST",        0x22, 0x033E, -15,   30,   25,   28,    1,  9, 2,  1 },
    { "THROTTLE",  "%",        "THROTTLE",     0x01, 0x11,    0,  100,   80,   95,    1,  5, 1,  0 },
    { "LOAD",      "%",        "LOAD",         0x01, 0x04,    0,  100,   80,   95,    1,  5, 1,  0 },
    { "IAT",       "\xB0""F",  "PRE-TURBO",    0x01, 0x0F,    0,  200,  140,  170,    1,  4, 1,  0 },
    { "CHG AIR",   "\xB0""F",  "POST-IC",      0x22, 0x0451,  0,  300,  200,  250,    1,  6, 2,  0 },
    { "VOLTAGE",   "V",        "VOLTAGE",      0x01, 0x42,    8,   16,   15,   16,    1,  8, 2,  1 },
};
