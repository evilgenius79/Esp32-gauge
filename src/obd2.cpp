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

void OBD2::sendRequest(uint8_t mode, uint16_t pid) {
    CanFrame frame = {};
    frame.identifier = OBD2_REQUEST_ID;
    frame.extd = 0;
    frame.data_length_code = 8;

    if (mode == 0x22) {
        // Mode 22 (Enhanced): 3 additional bytes (mode + 2-byte PID)
        frame.data[0] = 0x03;
        frame.data[1] = 0x22;
        frame.data[2] = (pid >> 8) & 0xFF;  // PID high byte
        frame.data[3] = pid & 0xFF;          // PID low byte
        frame.data[4] = 0xAA;
        frame.data[5] = 0xAA;
        frame.data[6] = 0xAA;
        frame.data[7] = 0xAA;
    } else {
        // Mode 01 (Standard): 2 additional bytes (mode + 1-byte PID)
        frame.data[0] = 0x02;
        frame.data[1] = 0x01;
        frame.data[2] = pid & 0xFF;
        frame.data[3] = 0xAA;
        frame.data[4] = 0xAA;
        frame.data[5] = 0xAA;
        frame.data[6] = 0xAA;
        frame.data[7] = 0xAA;
    }

    ESP32Can.writeFrame(frame);
}

bool OBD2::requestPID(uint8_t mode, uint16_t pid, uint8_t* dataA, uint8_t* dataB, uint32_t timeoutMs) {
    if (!_initialized) return false;

    // Flush any pending frames first
    CanFrame flush;
    while (ESP32Can.readFrame(flush, 0)) { /* discard */ }

    // Send the request
    sendRequest(mode, pid);

    // Wait for matching response
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        CanFrame rxFrame;
        if (ESP32Can.readFrame(rxFrame, 10)) {
            // Accept responses from 0x7E8-0x7EF
            if (rxFrame.identifier >= OBD2_RESPONSE_ID &&
                rxFrame.identifier <= 0x7EF) {

                if (mode == 0x22) {
                    // Mode 22 response: 0x62, PID_H, PID_L, A, B, ...
                    if (rxFrame.data[1] == 0x62 &&
                        rxFrame.data[2] == ((pid >> 8) & 0xFF) &&
                        rxFrame.data[3] == (pid & 0xFF)) {
                        *dataA = rxFrame.data[4];
                        *dataB = rxFrame.data[5];
                        _connected = true;
                        _errorCount = 0;
                        return true;
                    }
                } else {
                    // Mode 01 response: 0x41, PID, A, B, ...
                    if (rxFrame.data[1] == 0x41 && rxFrame.data[2] == (pid & 0xFF)) {
                        *dataA = rxFrame.data[3];
                        *dataB = rxFrame.data[4];
                        _connected = true;
                        _errorCount = 0;
                        return true;
                    }
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
