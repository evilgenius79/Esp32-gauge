#include "obd2.h"
#include <ESP32-TWAI-CAN.hpp>

bool OBD2::begin(int txPin, int rxPin) {
    if (_initialized) return true;

    // 500 kbps is the standard OBD2 CAN bus speed
    if (!ESP32Can.begin(ESP32Can.convertSpeed(500), txPin, rxPin, 10, 10)) {
        Serial.println("[OBD2] CAN bus init failed");
        return false;
    }

    Serial.println("[OBD2] CAN bus initialized (500 kbps)");
    _initialized = true;
    _connected = false;
    _errorCount = 0;
    return true;
}

void OBD2::end() {
    if (_initialized) {
        ESP32Can.end();
        _initialized = false;
        _connected = false;
    }
}

void OBD2::sendRequest(uint8_t pid) {
    CanFrame frame = {};
    frame.identifier = OBD2_REQUEST_ID;
    frame.extd = 0;
    frame.data_length_code = 8;
    frame.data[0] = 0x02;  // Number of additional bytes
    frame.data[1] = 0x01;  // Mode 01 (current data)
    frame.data[2] = pid;   // PID
    frame.data[3] = 0xAA;  // Padding (avoid bit-stuffing)
    frame.data[4] = 0xAA;
    frame.data[5] = 0xAA;
    frame.data[6] = 0xAA;
    frame.data[7] = 0xAA;
    ESP32Can.writeFrame(frame);
}

bool OBD2::requestPID(uint8_t pid, uint8_t* dataA, uint8_t* dataB, uint32_t timeoutMs) {
    if (!_initialized) return false;

    // Flush any pending frames first
    CanFrame flush;
    while (ESP32Can.readFrame(flush, 0)) { /* discard */ }

    // Send the request
    sendRequest(pid);

    // Wait for matching response
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        CanFrame rxFrame;
        if (ESP32Can.readFrame(rxFrame, 10)) {
            // Accept responses from 0x7E8-0x7EF
            if (rxFrame.identifier >= OBD2_RESPONSE_ID &&
                rxFrame.identifier <= 0x7EF) {

                // Verify this is a Mode 01 response (0x41) for our PID
                if (rxFrame.data[1] == 0x41 && rxFrame.data[2] == pid) {
                    *dataA = rxFrame.data[3];
                    *dataB = rxFrame.data[4];
                    _connected = true;
                    _errorCount = 0;
                    return true;
                }
            }
        }
    }

    // Timeout - no response
    _errorCount++;
    if (_errorCount > 10) {
        _connected = false;
    }
    return false;
}
