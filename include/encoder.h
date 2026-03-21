#pragma once

#include <Arduino.h>

// =============================================================================
// Rotary Encoder Handler for CrowPanel 1.28"
// Proper quadrature decoding with robust debouncing
// =============================================================================

class RotaryEncoder {
public:
    void begin(int pinA, int pinB, int pinSW);
    int getDirection();
    bool wasPressed();

private:
    int _pinA;
    int _pinB;
    int _pinSW;

    // Quadrature state tracking
    volatile int8_t  _encState     = 0;
    volatile int     _direction    = 0;
    volatile uint8_t _lastAB       = 0;

    // Button state
    volatile bool     _buttonPressed = false;
    volatile uint32_t _lastBtnTime   = 0;

    static RotaryEncoder* _instance;
    static void IRAM_ATTR isrEncoderAB();
    static void IRAM_ATTR isrButton();
};
