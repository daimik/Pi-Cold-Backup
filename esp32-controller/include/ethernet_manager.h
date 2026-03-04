#pragma once

#include <Arduino.h>

void ethernetInit();
bool ethernetIsConnected();
String ethernetGetIP();
