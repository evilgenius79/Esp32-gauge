#pragma once

#include <Arduino.h>

// =============================================================================
// Rotary Encoder Handler for CrowPanel 1.28"
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

    volatile int  _direction    = 0;
    volatile bool _buttonPressed = false;
    volatile uint32_t _lastEncTime = 0;
    volatile uint32_t _lastBtnTime = 0;

    static RotaryEncoder* _instance;
    static void IRAM_ATTR isrEncoder();
    static void IRAM_ATTR isrButton();
};
