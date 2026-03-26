#pragma once

#include <cstdint>

// =============================================================================
// OBD2 CAN Bus Communication via ESP32 TWAI
// Supports Mode 01 (standard) and Mode 22 (Ford enhanced) PIDs
// =============================================================================

// DTC prefix types (first 2 bits of DTC)
static const char DTC_PREFIX[] = { 'P', 'C', 'B', 'U' };

struct DTC {
    char code[6];  // e.g. "P0133\0"
};

static constexpr int MAX_DTCS = 16;

class OBD2 {
public:
    // Initialize TWAI/CAN bus hardware
    bool begin(int txPin, int rxPin);

    // Stop CAN bus
    void end();

    // Request a PID and wait for response (blocking, with timeout)
    // mode: 0x01 for standard OBD2, 0x22 for Ford enhanced
    // pid: 1 byte for Mode 01, 2 bytes for Mode 22
    // Returns true if response received, false on timeout
    bool requestPID(uint8_t mode, uint16_t pid, uint8_t* dataA, uint8_t* dataB, uint32_t timeoutMs = 200);

    // Scan for stored DTCs (Mode 03). Returns number of DTCs found.
    int scanDTCs(DTC* dtcArray, int maxDTCs, uint32_t timeoutMs = 1000);

    // Clear stored DTCs and MIL (Mode 04). Returns true on success.
    bool clearDTCs(uint32_t timeoutMs = 2000);

    // Check if bus is connected / last request succeeded
    bool isConnected() const { return _connected; }

    // Get consecutive error count
    int getErrorCount() const { return _errorCount; }

private:
    bool _initialized = false;
    bool _connected   = false;
    int  _errorCount  = 0;
    int  _skipCount   = 0;   // Back-off counter when bus disconnected

    static constexpr uint32_t OBD2_REQUEST_ID  = 0x7DF;
    static constexpr uint32_t OBD2_RESPONSE_ID = 0x7E8;

    void sendRequest(uint8_t mode, uint16_t pid);
    void decodeDTC(uint8_t byteA, uint8_t byteB, DTC& dtc);
};
