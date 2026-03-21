#pragma once
#include <cstdint>

// =============================================================================
// OBD2 Gauge Definitions
// =============================================================================

struct GaugeConfig {
    const char* name;       // Display name
    const char* units;      // Unit string
    uint8_t     pid;        // OBD2 PID (Mode 01)
    float       minVal;     // Minimum display value
    float       maxVal;     // Maximum display value
    float       warnVal;    // Warning threshold (yellow zone start)
    float       dangerVal;  // Danger threshold (red zone start)
    uint8_t     dataBytes;  // Number of data bytes in response (1 or 2)
    bool        isSigned;   // Whether the value can be negative
};

// Decode raw OBD2 response bytes into a physical value
inline float decodeOBD2(const GaugeConfig& gauge, uint8_t a, uint8_t b) {
    switch (gauge.pid) {
        case 0x04: // Engine Load: A * 100 / 255
            return a * 100.0f / 255.0f;

        case 0x05: // Coolant Temp: A - 40
            return (float)a - 40.0f;

        case 0x0B: // Intake MAP: A (kPa)
            return (float)a;

        case 0x0C: // RPM: (256*A + B) / 4
            return (256.0f * a + b) / 4.0f;

        case 0x0D: // Vehicle Speed: A (km/h)
            return (float)a;

        case 0x0F: // Intake Air Temp: A - 40
            return (float)a - 40.0f;

        case 0x11: // Throttle Position: A * 100 / 255
            return a * 100.0f / 255.0f;

        case 0x42: // Control Module Voltage: (256*A + B) / 1000
            return (256.0f * a + b) / 1000.0f;

        default:
            return (float)a;
    }
}

// All supported gauges
constexpr int NUM_GAUGES = 8;

const GaugeConfig GAUGES[NUM_GAUGES] = {
    // name              units    pid   min     max     warn    danger  bytes  signed
    { "RPM",            "rpm",   0x0C,  0,     8000,   5500,   6500,   2,     false },
    { "SPEED",          "km/h",  0x0D,  0,      255,    180,    220,   1,     false },
    { "COOLANT",        "\xB0""C",0x05, -40,    215,    100,    115,   1,     false },
    { "BOOST",          "kPa",   0x0B,  0,      255,    180,    220,   1,     false },
    { "THROTTLE",       "%",     0x11,  0,      100,     80,     95,   1,     false },
    { "LOAD",           "%",     0x04,  0,      100,     80,     95,   1,     false },
    { "INTAKE",         "\xB0""C",0x0F, -40,    215,     60,     80,   1,     false },
    { "VOLTAGE",        "V",     0x42,  0,       20,     15,     16,   2,     false },
};
