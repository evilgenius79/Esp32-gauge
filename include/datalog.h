#pragma once

#ifdef TARGET_TAB5

#include <cstdint>
#include "gauges.h"

// =============================================================================
// CSV Data Logger — writes gauge readings to SD card
//
// File format: /obd2_log/YYYYMMDD_HHMMSS.csv
// Columns: timestamp_ms, gauge0_name, gauge0_value, gauge1_name, gauge1_value, ...
// =============================================================================

class DataLogger {
public:
    bool begin();              // Initialize SD card, returns true if SD present
    void startSession();       // Create new CSV file with header row
    void logRow(const int indices[4], const float values[4], uint32_t timestamp);
    void stopSession();        // Flush and close file
    bool isLogging() const { return _logging; }
    bool isAvailable() const { return _sdAvailable; }
    const char* currentFile() const { return _filename; }

private:
    bool _sdAvailable = false;
    bool _logging = false;
    char _filename[40];        // e.g., "/obd2_log/20260326_143000.csv"
};

#endif // TARGET_TAB5
