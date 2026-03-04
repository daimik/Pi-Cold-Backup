#include "relay_controller.h"
#include "config.h"

static int _pin = DEFAULT_RELAY_PIN;
static bool _isOn = false;
static unsigned long _powerOnTime = 0;

void relayInit(int pin) {
    _pin = pin;
    pinMode(_pin, OUTPUT);
    digitalWrite(_pin, !RELAY_ON_LEVEL);  // Start with relay OFF
    _isOn = false;
    _powerOnTime = 0;
    Serial.printf("[RELAY] Initialized on GPIO%d (OFF)\n", _pin);
}

void relaySetPin(int pin) {
    if (pin == _pin) return;
    // Deactivate old pin
    digitalWrite(_pin, !RELAY_ON_LEVEL);
    // Set up new pin
    _pin = pin;
    pinMode(_pin, OUTPUT);
    if (_isOn) {
        digitalWrite(_pin, RELAY_ON_LEVEL);
    } else {
        digitalWrite(_pin, !RELAY_ON_LEVEL);
    }
    Serial.printf("[RELAY] Pin changed to GPIO%d\n", _pin);
}

void relayPowerOn() {
    digitalWrite(_pin, RELAY_ON_LEVEL);
    if (!_isOn) {
        _isOn = true;
        _powerOnTime = millis();
        Serial.println("[RELAY] Power ON");
    }
}

void relayPowerOff() {
    digitalWrite(_pin, !RELAY_ON_LEVEL);
    if (_isOn) {
        Serial.println("[RELAY] Power OFF");
    }
    _isOn = false;
    _powerOnTime = 0;
}

bool relayIsOn() {
    return _isOn;
}

unsigned long relayUptimeMs() {
    if (!_isOn || _powerOnTime == 0) return 0;
    return millis() - _powerOnTime;
}
