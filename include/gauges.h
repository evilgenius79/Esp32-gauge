#pragma once
#include <cstdint>

// =============================================================================
// OBD2 Gauge Definitions
// =============================================================================

// Selectable decode formulas for converting raw OBD2 bytes to display values
enum DecodeFormula : uint8_t {
    DECODE_RAW_A = 0,          // A                    (raw byte)
    DECODE_RAW_AB,             // 256*A + B            (raw 2-byte)
    DECODE_PERCENT,            // A * 100 / 255        (percent 0-100)
    DECODE_TEMP_C,             // A - 40               (celsius)
    DECODE_TEMP_F,             // (A - 40) * 9/5 + 32  (fahrenheit)
    DECODE_RPM,                // (256*A + B) / 4      (RPM)
    DECODE_SPEED_KMH,          // A                    (km/h)
    DECODE_SPEED_MPH,          // A * 0.621371         (mph from km/h)
    DECODE_VOLTAGE,            // (256*A + B) / 1000   (volts)
    DECODE_BOOST_PSI,          // (256*A+B)/128 * 0.145 - 14.696  (kPa to PSI gauge)
    DECODE_AB_DIV_128,         // (256*A + B) / 128    (generic)
    DECODE_AB_DIV_4,           // (256*A + B) / 4      (generic)
    DECODE_AB_DIV_100,         // (256*A + B) / 100    (generic)
    DECODE_AB_DIV_1000,        // (256*A + B) / 1000   (generic)
    NUM_DECODE_FORMULAS
};

// Human-readable names for the editor UI
static const char* const DECODE_NAMES[] = {
    "Raw A",           // 0
    "Raw 256A+B",      // 1
    "A*100/255 %",     // 2
    "A-40 (C)",        // 3
    "A-40 (F)",        // 4
    "(256A+B)/4 RPM",  // 5
    "A km/h",          // 6
    "A*0.621 mph",     // 7
    "(256A+B)/1000 V", // 8
    "kPa->PSI boost",  // 9
    "(256A+B)/128",    // 10
    "(256A+B)/4",      // 11
    "(256A+B)/100",    // 12
    "(256A+B)/1000",   // 13
};

struct GaugeConfig {
    char          name[12];       // Display name (editable)
    char          units[8];       // Unit string for numeric readout
    char          scaleLabel[16]; // Label near scale (e.g., "x1000r/min")
    uint8_t       mode;           // OBD2 mode (0x01 standard, 0x22 enhanced)
    uint16_t      pid;            // OBD2 PID (1 byte for Mode 01, 2 bytes for Mode 22)
    float         minVal;         // Minimum display value
    float         maxVal;         // Maximum display value
    float         warnVal;        // Warning threshold (yellow zone start)
    float         dangerVal;      // Danger threshold (red zone start)
    float         scaleDivisor;   // Divide value by this for dial numbers
    int           majorDivisions; // Number of major scale divisions
    uint8_t       dataBytes;      // Number of data bytes in response (1 or 2)
    uint8_t       decimals;       // Decimal places for numeric readout
    DecodeFormula formula;        // How to decode raw OBD2 bytes
};

// Decode raw OBD2 response bytes using the gauge's selected formula
inline float decodeOBD2(const GaugeConfig& gauge, uint8_t a, uint8_t b) {
    float ab = 256.0f * a + b;
    switch (gauge.formula) {
        case DECODE_RAW_A:       return (float)a;
        case DECODE_RAW_AB:      return ab;
        case DECODE_PERCENT:     return a * 100.0f / 255.0f;
        case DECODE_TEMP_C:      return (float)a - 40.0f;
        case DECODE_TEMP_F:      return ((float)a - 40.0f) * 9.0f / 5.0f + 32.0f;
        case DECODE_RPM:         return ab / 4.0f;
        case DECODE_SPEED_KMH:   return (float)a;
        case DECODE_SPEED_MPH:   return (float)a * 0.621371f;
        case DECODE_VOLTAGE:     return ab / 1000.0f;
        case DECODE_BOOST_PSI:   return (ab / 128.0f) * 0.14504f - 14.696f;
        case DECODE_AB_DIV_128:  return ab / 128.0f;
        case DECODE_AB_DIV_4:    return ab / 4.0f;
        case DECODE_AB_DIV_100:  return ab / 100.0f;
        case DECODE_AB_DIV_1000: return ab / 1000.0f;
        default:                 return (float)a;
    }
}

constexpr int NUM_GAUGES = 9;

// Default gauge configurations (copied into mutable array at startup)
//                                                                                                    formula
static const GaugeConfig DEFAULT_GAUGES[NUM_GAUGES] = {
    //  name        units    scaleLabel      mode   pid     min   max   warn  danger div  divs  bytes dec  formula
    { "RPM",       "rpm",   "x1000r/min",  0x01, 0x0C,     0, 8000, 5500, 6500, 1000, 8,  2,  0, DECODE_RPM },
    { "SPEED",     "mph",   "SPEED",       0x01, 0x0D,     0,  160,  110,  140,    1, 8,  1,  0, DECODE_SPEED_MPH },
    { "COOLANT",   "\xB0""F","COOLANT",    0x01, 0x05,   100,  260,  210,  240,    1, 4,  1,  0, DECODE_TEMP_F },
    { "BOOST",     "psi",   "BOOST",       0x22, 0x033E, -15,   30,   25,   28,    1, 9,  2,  1, DECODE_BOOST_PSI },
    { "THROTTLE",  "%",     "THROTTLE",    0x01, 0x11,     0,  100,   80,   95,    1, 5,  1,  0, DECODE_PERCENT },
    { "LOAD",      "%",     "LOAD",        0x01, 0x04,     0,  100,   80,   95,    1, 5,  1,  0, DECODE_PERCENT },
    { "IAT",       "\xB0""F","PRE-TURBO",  0x01, 0x0F,     0,  200,  140,  170,    1, 4,  1,  0, DECODE_TEMP_F },
    { "CHG AIR",   "\xB0""F","POST-IC",    0x22, 0xF40F,   0,  300,  200,  250,    1, 6,  1,  0, DECODE_TEMP_F },
    { "VOLTAGE",   "V",     "VOLTAGE",     0x01, 0x42,     8,   16,   15,   16,    1, 8,  2,  1, DECODE_VOLTAGE },
};

// Mutable gauge array — editable at runtime, persisted to NVS
extern GaugeConfig GAUGES[NUM_GAUGES];

// CAN bus speed — configurable, persisted to NVS
extern uint32_t canBusSpeed;  // 250 or 500 (kbps)
void loadGaugeConfigs();           // Load from NVS (or defaults if no saved data)
void saveGaugeConfig(int index);   // Save single gauge config to NVS
void resetGaugeConfig(int index);  // Reset single gauge to factory default
void saveCanSpeed();               // Save CAN speed to NVS
