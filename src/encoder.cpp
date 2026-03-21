#include "encoder.h"

RotaryEncoder* RotaryEncoder::_instance = nullptr;

// Quadrature lookup table: maps (prevAB << 2 | currAB) to direction
// Valid transitions give +1 (CW) or -1 (CCW), invalid give 0
static const int8_t ENC_TABLE[16] = {
     0, -1,  1,  0,
     1,  0,  0, -1,
    -1,  0,  0,  1,
     0,  1, -1,  0
};

void RotaryEncoder::begin(int pinA, int pinB, int pinSW) {
    _pinA  = pinA;
    _pinB  = pinB;
    _pinSW = pinSW;
    _instance = this;

    pinMode(_pinA,  INPUT_PULLUP);
    pinMode(_pinB,  INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);

    // Read initial state
    _lastAB = (digitalRead(_pinA) << 1) | digitalRead(_pinB);
    _encState = 0;

    // Attach interrupts on BOTH edges of BOTH pins for full quadrature decode
    attachInterrupt(digitalPinToInterrupt(_pinA), isrEncoderAB, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinB), isrEncoderAB, CHANGE);
    attachInterrupt(digitalPinToInterrupt(_pinSW), isrButton, FALLING);
}

int RotaryEncoder::getDirection() {
    int d = _direction;
    _direction = 0;
    return d;
}

bool RotaryEncoder::wasPressed() {
    bool p = _buttonPressed;
    _buttonPressed = false;
    return p;
}

void IRAM_ATTR RotaryEncoder::isrEncoderAB() {
    if (!_instance) return;

    uint8_t curAB = (digitalRead(_instance->_pinA) << 1) | digitalRead(_instance->_pinB);
    uint8_t idx = (_instance->_lastAB << 2) | curAB;
    _instance->_lastAB = curAB;

    int8_t step = ENC_TABLE[idx & 0x0F];
    if (step == 0) return; // Invalid or no transition

    _instance->_encState += step;

    // Only register a direction change after a full detent (4 valid transitions)
    // This eliminates bouncing — partial/noisy transitions cancel out
    if (_instance->_encState >= 4) {
        _instance->_direction = 1;  // CW
        _instance->_encState = 0;
    } else if (_instance->_encState <= -4) {
        _instance->_direction = -1; // CCW
        _instance->_encState = 0;
    }
}

void IRAM_ATTR RotaryEncoder::isrButton() {
    if (!_instance) return;

    uint32_t now = millis();
    if (now - _instance->_lastBtnTime < 250) return; // 250ms debounce
    _instance->_lastBtnTime = now;

    // Confirm button is actually pressed (not noise)
    if (digitalRead(_instance->_pinSW) == LOW) {
        _instance->_buttonPressed = true;
    }
}
