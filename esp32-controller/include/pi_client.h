#pragma once

#include <Arduino.h>

struct PiStatus {
    String state;    // idle | running | completed | failed
    String jobId;
    String error;
    String reportStatus;  // SUCCESS | PARTIAL | FAILED
    bool valid;
};

void piClientInit(const String& ip, int port);
void piClientSetTarget(const String& ip, int port);

bool piCheckHealth();
String piTriggerBackup();   // returns job_id or empty on failure
PiStatus piPollStatus();
bool piRequestShutdown();
