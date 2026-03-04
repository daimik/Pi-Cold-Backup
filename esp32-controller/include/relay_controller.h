#pragma once

#include <Arduino.h>

void relayInit(int pin);
void relaySetPin(int pin);
void relayPowerOn();
void relayPowerOff();
bool relayIsOn();
unsigned long relayUptimeMs();
