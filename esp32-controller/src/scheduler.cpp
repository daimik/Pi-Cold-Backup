#include "scheduler.h"

static unsigned long _intervalMs = 0;
static unsigned long _lastRunMs = 0;
static bool _initialized = false;

void schedulerInit(unsigned long intervalMinutes) {
    _intervalMs = intervalMinutes * 60UL * 1000UL;
    _lastRunMs = millis();
    _initialized = true;
    Serial.printf("[SCHED] Initialized: every %lu minutes\n", intervalMinutes);
}

void schedulerSetInterval(unsigned long intervalMinutes) {
    _intervalMs = intervalMinutes * 60UL * 1000UL;
    Serial.printf("[SCHED] Interval changed to %lu minutes\n", intervalMinutes);
}

bool schedulerCheck() {
    if (!_initialized || _intervalMs == 0) return false;
    // Unsigned subtraction handles millis() wrap (~49 days)
    return (millis() - _lastRunMs) >= _intervalMs;
}

void schedulerReset() {
    _lastRunMs = millis();
}

unsigned long schedulerNextRunMs() {
    if (!_initialized || _intervalMs == 0) return 0;
    unsigned long elapsed = millis() - _lastRunMs;
    if (elapsed >= _intervalMs) return 0;
    return _intervalMs - elapsed;
}

unsigned long schedulerIntervalMin() {
    return _intervalMs / 60000UL;
}
