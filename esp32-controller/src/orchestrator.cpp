#include "orchestrator.h"
#include "config.h"
#include "relay_controller.h"
#include "pi_client.h"

static OrchestratorState _state = STATE_IDLE;
static OrchestratorResult _result = {0, "", "", 0};
static unsigned long _stateEnteredMs = 0;
static unsigned long _lastPollMs = 0;
static unsigned long _startMs = 0;
static String _jobId;

static void setState(OrchestratorState s) {
    _state = s;
    _stateEnteredMs = millis();
}

void orchestratorInit() {
    _state = STATE_IDLE;
}

void orchestratorStart() {
    if (_state != STATE_IDLE && _state != STATE_COMPLETE && _state != STATE_ERROR) {
        Serial.println("[ORCH] Already running — ignoring start");
        return;
    }
    Serial.println("[ORCH] Starting backup scenario");
    _startMs = millis();
    _jobId = "";
    setState(STATE_POWERING_ON);
}

void orchestratorTick() {
    unsigned long elapsed = millis() - _stateEnteredMs;

    switch (_state) {

    case STATE_IDLE:
    case STATE_COMPLETE:
    case STATE_ERROR:
        // Nothing to do
        break;

    case STATE_POWERING_ON:
        relayPowerOn();
        Serial.println("[ORCH] Pi power ON — waiting for health...");
        setState(STATE_WAITING_FOR_HEALTH);
        break;

    case STATE_WAITING_FOR_HEALTH:
        if (elapsed > PI_HEALTH_TIMEOUT_MS) {
            Serial.println("[ORCH] Health timeout — Pi did not respond");
            _result.lastResult = "ERROR";
            _result.lastError = "Pi did not respond to health check";
            _result.durationMs = millis() - _startMs;
            // Leave Pi powered on for debugging
            setState(STATE_ERROR);
            break;
        }
        if ((millis() - _lastPollMs) >= PI_HEALTH_POLL_INTERVAL_MS) {
            _lastPollMs = millis();
            if (piCheckHealth()) {
                Serial.println("[ORCH] Pi is healthy — triggering backup");
                setState(STATE_TRIGGERING_BACKUP);
            }
        }
        break;

    case STATE_TRIGGERING_BACKUP:
        _jobId = piTriggerBackup();
        if (_jobId.length() > 0) {
            Serial.printf("[ORCH] Backup triggered, job_id=%s\n", _jobId.c_str());
            _lastPollMs = 0;
            setState(STATE_POLLING_STATUS);
        } else {
            Serial.println("[ORCH] Failed to trigger backup");
            _result.lastResult = "ERROR";
            _result.lastError = "Failed to trigger backup on Pi";
            _result.durationMs = millis() - _startMs;
            setState(STATE_ERROR);
        }
        break;

    case STATE_POLLING_STATUS:
        if (elapsed > PI_BACKUP_TIMEOUT_MS) {
            Serial.println("[ORCH] Backup timeout exceeded");
            _result.lastResult = "ERROR";
            _result.lastError = "Backup exceeded maximum time";
            _result.durationMs = millis() - _startMs;
            setState(STATE_ERROR);
            break;
        }
        if ((millis() - _lastPollMs) >= PI_BACKUP_POLL_INTERVAL_MS) {
            _lastPollMs = millis();
            PiStatus st = piPollStatus();
            if (st.valid) {
                Serial.printf("[ORCH] Backup state: %s\n", st.state.c_str());
                if (st.state == "completed") {
                    _result.lastResult = st.reportStatus;
                    _result.lastError = "";
                    _result.durationMs = millis() - _startMs;
                    Serial.printf("[ORCH] Backup completed: %s\n", st.reportStatus.c_str());
                    setState(STATE_REQUESTING_SHUTDOWN);
                } else if (st.state == "failed") {
                    _result.lastResult = "FAILED";
                    _result.lastError = st.error;
                    _result.durationMs = millis() - _startMs;
                    Serial.printf("[ORCH] Backup failed — leaving Pi on for debug\n");
                    setState(STATE_ERROR);
                }
            }
        }
        break;

    case STATE_REQUESTING_SHUTDOWN:
        Serial.println("[ORCH] Requesting Pi shutdown...");
        piRequestShutdown();
        setState(STATE_WAITING_FOR_SHUTDOWN);
        break;

    case STATE_WAITING_FOR_SHUTDOWN:
        if (elapsed >= PI_SHUTDOWN_WAIT_MS) {
            Serial.println("[ORCH] Shutdown wait complete — cutting power");
            setState(STATE_POWERING_OFF);
        }
        break;

    case STATE_POWERING_OFF:
        relayPowerOff();
        _result.lastBackupMs = millis();
        Serial.println("[ORCH] Scenario complete");
        setState(STATE_COMPLETE);
        break;
    }
}

OrchestratorState orchestratorGetState() {
    return _state;
}

const char* orchestratorStateStr() {
    switch (_state) {
        case STATE_IDLE:                return "IDLE";
        case STATE_POWERING_ON:         return "POWERING_ON";
        case STATE_WAITING_FOR_HEALTH:  return "WAITING_FOR_HEALTH";
        case STATE_TRIGGERING_BACKUP:   return "TRIGGERING_BACKUP";
        case STATE_POLLING_STATUS:      return "POLLING_STATUS";
        case STATE_REQUESTING_SHUTDOWN: return "REQUESTING_SHUTDOWN";
        case STATE_WAITING_FOR_SHUTDOWN:return "WAITING_FOR_SHUTDOWN";
        case STATE_POWERING_OFF:        return "POWERING_OFF";
        case STATE_COMPLETE:            return "COMPLETE";
        case STATE_ERROR:               return "ERROR";
        default:                        return "UNKNOWN";
    }
}

OrchestratorResult& orchestratorGetResult() {
    return _result;
}
