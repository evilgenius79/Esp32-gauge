#include "datalog.h"
#ifdef TARGET_TAB5

#include <M5Unified.h>

bool DataLogger::begin() {
    // M5Unified initializes SD card automatically via M5.begin()
    // Check if SD is available by testing if we can open root
    _sdAvailable = SD.exists("/");
    if (_sdAvailable) {
        if (!SD.exists("/obd2_log")) {
            SD.mkdir("/obd2_log");
        }
        Serial.println("[LOG] SD card ready");
    } else {
        Serial.println("[LOG] SD card not found");
    }
    return _sdAvailable;
}

void DataLogger::startSession(const char* gaugeNames[4]) {
    if (_logging) return;

    _logging = true;

    if (!_sdAvailable) {
        Serial.println("[LOG] Started (no SD card — data not saved)");
        return;
    }

    // Generate filename from uptime
    uint32_t sec = millis() / 1000;
    int h = sec / 3600;
    int m = (sec % 3600) / 60;
    int s = sec % 60;
    snprintf(_filename, sizeof(_filename), "/obd2_log/S%02d_%02d_%02d.csv", h, m, s);

    _file = SD.open(_filename, FILE_WRITE);
    if (!_file) {
        Serial.printf("[LOG] Failed to open %s\n", _filename);
        return;
    }

    // Write CSV header
    _file.printf("timestamp_ms,%s,%s,%s,%s\n",
                 gaugeNames[0], gaugeNames[1], gaugeNames[2], gaugeNames[3]);
    _file.flush();
    _lastFlush = millis();
    Serial.printf("[LOG] Started: %s\n", _filename);
}

void DataLogger::logRow(const float values[4], uint32_t timestamp) {
    if (!_logging || !_file) return;

    _file.printf("%lu,%.2f,%.2f,%.2f,%.2f\n",
                 timestamp, values[0], values[1], values[2], values[3]);

    // Flush every 10 seconds to avoid data loss
    if (timestamp - _lastFlush >= 10000) {
        _file.flush();
        _lastFlush = timestamp;
    }
}

void DataLogger::stop() {
    if (!_logging) return;
    if (_file) {
        _file.flush();
        _file.close();
    }
    _logging = false;
    Serial.println("[LOG] Stopped");
}

#endif
