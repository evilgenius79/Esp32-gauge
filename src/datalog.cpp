#ifdef TARGET_TAB5

#include "datalog.h"
#include <SD.h>
#include <FS.h>
#include <M5Unified.h>

// Tab5 SD card uses the built-in SD slot via M5Unified
static File logFile;

bool DataLogger::begin() {
    // M5Unified initializes SD automatically; just check if it's mounted
    _sdAvailable = SD.begin();
    if (_sdAvailable) {
        // Create log directory if it doesn't exist
        if (!SD.exists("/obd2_log")) {
            SD.mkdir("/obd2_log");
        }
        Serial.println("[LOG] SD card ready");
    } else {
        Serial.println("[LOG] No SD card detected");
    }
    return _sdAvailable;
}

void DataLogger::startSession() {
    if (!_sdAvailable) return;

    // Generate filename from uptime (no RTC, so use millis as session ID)
    uint32_t ms = millis();
    uint32_t sec = ms / 1000;
    uint32_t min = sec / 60;
    uint32_t hr  = min / 60;
    snprintf(_filename, sizeof(_filename), "/obd2_log/S%02lu_%02lu_%02lu.csv",
             hr % 100, min % 60, sec % 60);

    logFile = SD.open(_filename, FILE_WRITE);
    if (!logFile) {
        Serial.printf("[LOG] Failed to create %s\n", _filename);
        _logging = false;
        return;
    }

    // Write CSV header
    logFile.println("timestamp_ms,slot0_name,slot0_value,slot1_name,slot1_value,"
                    "slot2_name,slot2_value,slot3_name,slot3_value");
    logFile.flush();
    _logging = true;
    Serial.printf("[LOG] Started logging to %s\n", _filename);
}

void DataLogger::logRow(const int indices[4], const float values[4], uint32_t timestamp) {
    if (!_logging || !logFile) return;

    char line[256];
    int pos = snprintf(line, sizeof(line), "%lu", timestamp);

    for (int i = 0; i < 4; i++) {
        const GaugeConfig& g = GAUGES[indices[i]];
        pos += snprintf(line + pos, sizeof(line) - pos, ",%s,%.2f", g.name, values[i]);
    }

    logFile.println(line);

    // Flush every 10 seconds to avoid data loss
    static uint32_t lastFlush = 0;
    if (timestamp - lastFlush > 10000) {
        logFile.flush();
        lastFlush = timestamp;
    }
}

void DataLogger::stopSession() {
    if (_logging && logFile) {
        logFile.flush();
        logFile.close();
        Serial.printf("[LOG] Stopped logging: %s\n", _filename);
    }
    _logging = false;
}

#endif // TARGET_TAB5
