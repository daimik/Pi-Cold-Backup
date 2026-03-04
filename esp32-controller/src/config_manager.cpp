#include "config_manager.h"
#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static AppConfig _cfg;

void configInit() {
    if (!LittleFS.begin(true)) {
        Serial.println("[CONFIG] LittleFS mount failed");
        return;
    }
    Serial.println("[CONFIG] LittleFS mounted");
    configLoad();
}

void configLoad() {
    // Set defaults
    _cfg.relayPin = DEFAULT_RELAY_PIN;
    _cfg.piIp = DEFAULT_PI_IP;
    _cfg.piPort = DEFAULT_PI_PORT;
    _cfg.scheduleIntervalMin = DEFAULT_SCHEDULE_INTERVAL_MIN;

    File f = LittleFS.open(CONFIG_FILE, "r");
    if (!f) {
        Serial.println("[CONFIG] No config file — using defaults");
        configSave();
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("[CONFIG] JSON parse error: %s\n", err.c_str());
        return;
    }

    _cfg.relayPin = doc["relay_pin"] | DEFAULT_RELAY_PIN;
    _cfg.piIp = doc["pi_ip"] | DEFAULT_PI_IP;
    _cfg.piPort = doc["pi_port"] | DEFAULT_PI_PORT;
    _cfg.scheduleIntervalMin = doc["schedule_interval_min"] | (unsigned long)DEFAULT_SCHEDULE_INTERVAL_MIN;

    Serial.printf("[CONFIG] Loaded: relay=%d, pi=%s:%d, interval=%lu min\n",
                  _cfg.relayPin, _cfg.piIp.c_str(), _cfg.piPort, _cfg.scheduleIntervalMin);
}

void configSave() {
    JsonDocument doc;
    doc["relay_pin"] = _cfg.relayPin;
    doc["pi_ip"] = _cfg.piIp;
    doc["pi_port"] = _cfg.piPort;
    doc["schedule_interval_min"] = _cfg.scheduleIntervalMin;

    File f = LittleFS.open(CONFIG_FILE, "w");
    if (!f) {
        Serial.println("[CONFIG] Failed to open config for writing");
        return;
    }
    serializeJson(doc, f);
    f.close();
    Serial.println("[CONFIG] Saved");
}

AppConfig& configGet() {
    return _cfg;
}
