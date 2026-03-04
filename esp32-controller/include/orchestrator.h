#pragma once

#include <Arduino.h>

enum OrchestratorState {
    STATE_IDLE,
    STATE_POWERING_ON,
    STATE_WAITING_FOR_HEALTH,
    STATE_TRIGGERING_BACKUP,
    STATE_POLLING_STATUS,
    STATE_REQUESTING_SHUTDOWN,
    STATE_WAITING_FOR_SHUTDOWN,
    STATE_POWERING_OFF,
    STATE_COMPLETE,
    STATE_ERROR
};

struct OrchestratorResult {
    unsigned long lastBackupMs; // millis() when backup finished (0 = never)
    String lastResult;          // SUCCESS | PARTIAL | FAILED | ERROR
    String lastError;
    unsigned long durationMs;
};

void orchestratorInit();
void orchestratorTick();
void orchestratorStart();
OrchestratorState orchestratorGetState();
const char* orchestratorStateStr();
OrchestratorResult& orchestratorGetResult();
