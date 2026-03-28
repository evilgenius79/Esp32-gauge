#pragma once
#ifdef TARGET_TAB5

#include <SD.h>
#include <FS.h>

class DataLogger {
public:
    bool begin();
    void startSession(const char* gaugeNames[4]);
    void logRow(const float values[4], uint32_t timestamp);
    void stop();
    bool isLogging() const { return _logging; }
    bool isAvailable() const { return _sdAvailable; }

private:
    File _file;
    bool _sdAvailable = false;
    bool _logging = false;
    uint32_t _lastFlush = 0;
    char _filename[40];
};

#endif
