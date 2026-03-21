#include "encoder.h"
#include <Arduino.h>

RotaryEncoder* RotaryEncoder::_instance = nullptr;

void RotaryEncoder::begin(int pinA, int pinB, int pinSW) {
    _pinA  = pinA;
    _pinB  = pinB;
    _pinSW = pinSW;
    _instance = this;

    pinMode(_pinA,  INPUT_PULLUP);
    pinMode(_pinB,  INPUT_PULLUP);
    pinMode(_pinSW, INPUT_PULLUP);

    attachInterrupt(digitalPinToInterrupt(_pinA), isrEncoder, FALLING);
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

void IRAM_ATTR RotaryEncoder::isrEncoder() {
    if (!_instance) return;

    uint32_t now = millis();
    if (now - _instance->_lastEncTime < 5) return; // debounce
    _instance->_lastEncTime = now;

    int bVal = digitalRead(_instance->_pinB);
    if (bVal == 0) {
        _instance->_direction = 1;  // CW
    } else {
        _instance->_direction = -1; // CCW
    }
}

void IRAM_ATTR RotaryEncoder::isrButton() {
    if (!_instance) return;

    uint32_t now = millis();
    if (now - _instance->_lastBtnTime < 200) return; // debounce
    _instance->_lastBtnTime = now;

    _instance->_buttonPressed = true;
}
