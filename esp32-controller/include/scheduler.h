#pragma once

#include <Arduino.h>

void schedulerInit(unsigned long intervalMinutes);
void schedulerSetInterval(unsigned long intervalMinutes);
bool schedulerCheck();
void schedulerReset();
unsigned long schedulerNextRunMs();
unsigned long schedulerIntervalMin();
