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

void OBD2::decodeDTC(uint8_t byteA, uint8_t byteB, DTC& dtc) {
    // First 2 bits = prefix (P/C/B/U), next 2 bits = first digit,
    // remaining 12 bits = last 3 hex digits
    char prefix = DTC_PREFIX[(byteA >> 6) & 0x03];
    uint8_t d1 = (byteA >> 4) & 0x03;
    uint8_t d2 = byteA & 0x0F;
    uint8_t d3 = (byteB >> 4) & 0x0F;
    uint8_t d4 = byteB & 0x0F;
    snprintf(dtc.code, sizeof(dtc.code), "%c%d%X%X%X", prefix, d1, d2, d3, d4);
}

int OBD2::scanDTCs(DTC* dtcArray, int maxDTCs, uint32_t timeoutMs) {
    if (!_initialized) return -1;

    // Flush pending frames
    CanFrame flush;
    while (ESP32Can.readFrame(flush, 0)) { /* discard */ }

    // Send Mode 03 request (read stored DTCs)
    CanFrame frame = {};
    frame.identifier = OBD2_REQUEST_ID;
    frame.extd = 0;
    frame.data_length_code = 8;
    frame.data[0] = 0x01;  // 1 byte follows
    frame.data[1] = 0x03;  // Mode 03: request stored DTCs
    for (int i = 2; i < 8; i++) frame.data[i] = 0xAA;
    ESP32Can.writeFrame(frame);

    // Collect responses — each frame can hold up to 3 DTCs
    int count = 0;
    uint32_t start = millis();
    while (millis() - start < timeoutMs && count < maxDTCs) {
        CanFrame rxFrame;
        if (ESP32Can.readFrame(rxFrame, 50)) {
            if (rxFrame.identifier >= OBD2_RESPONSE_ID &&
                rxFrame.identifier <= 0x7EF &&
                rxFrame.data[1] == 0x43) {
                // Mode 03 response: 0x43, numDTCs, DTC1_H, DTC1_L, DTC2_H, DTC2_L, ...
                // Parse DTCs from bytes 2-7 (up to 3 DTCs per frame)
                for (int i = 2; i < 7 && count < maxDTCs; i += 2) {
                    uint8_t a = rxFrame.data[i];
                    uint8_t b = rxFrame.data[i + 1];
                    if (a == 0 && b == 0) continue;  // Skip empty slots
                    decodeDTC(a, b, dtcArray[count]);
                    count++;
                }
                _connected = true;
                _errorCount = 0;
            }
        }
    }

    return count;
}

bool OBD2::clearDTCs(uint32_t timeoutMs) {
    if (!_initialized) return false;

    // Flush pending frames
    CanFrame flush;
    while (ESP32Can.readFrame(flush, 0)) { /* discard */ }

    // Send Mode 04 request (clear DTCs and MIL)
    CanFrame frame = {};
    frame.identifier = OBD2_REQUEST_ID;
    frame.extd = 0;
    frame.data_length_code = 8;
    frame.data[0] = 0x01;
    frame.data[1] = 0x04;  // Mode 04: clear DTCs
    for (int i = 2; i < 8; i++) frame.data[i] = 0xAA;
    ESP32Can.writeFrame(frame);

    // Wait for positive response (0x44)
    uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        CanFrame rxFrame;
        if (ESP32Can.readFrame(rxFrame, 50)) {
            if (rxFrame.identifier >= OBD2_RESPONSE_ID &&
                rxFrame.identifier <= 0x7EF &&
                rxFrame.data[1] == 0x44) {
                _connected = true;
                _errorCount = 0;
                return true;
            }
        }
    }
    return false;
}
