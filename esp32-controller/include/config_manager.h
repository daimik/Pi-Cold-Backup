#pragma once

#include <Arduino.h>

struct AppConfig {
    int relayPin;
    String piIp;
    int piPort;
    unsigned long scheduleIntervalMin;
};

void configInit();
void configLoad();
void configSave();
AppConfig& configGet();
