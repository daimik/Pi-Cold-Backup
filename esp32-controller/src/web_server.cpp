#include "web_server.h"
#include "config.h"
#include "config_manager.h"
#include "relay_controller.h"
#include "scheduler.h"
#include "orchestrator.h"
#include "ethernet_manager.h"

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

static AsyncWebServer server(80);

// Pins reserved for WT32-ETH01 Ethernet (LAN8720 RMII)
static bool isEthPin(int pin) {
    return pin == 0 || pin == 16 || pin == 18 || pin == 19 ||
           pin == 21 || pin == 22 || pin == 23 || pin == 25 ||
           pin == 26 || pin == 27;
}

void webServerInit() {
    // ── REST API ──────────────────────────────────────────────────────

    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *req) {
        JsonDocument doc;
        doc["pi_power"] = relayIsOn();
        doc["state"] = orchestratorStateStr();
        doc["uptime_ms"] = millis();
        doc["pi_uptime_ms"] = relayUptimeMs();
        doc["next_run_ms"] = schedulerNextRunMs();
        doc["schedule_interval_min"] = schedulerIntervalMin();
        doc["eth_connected"] = ethernetIsConnected();
        doc["eth_ip"] = ethernetGetIP();
        doc["free_heap"] = ESP.getFreeHeap();

        auto& result = orchestratorGetResult();
        doc["last_backup_ago_ms"] = result.lastBackupMs ? (millis() - result.lastBackupMs) : 0;
        doc["last_result"] = result.lastResult;
        doc["last_error"] = result.lastError;
        doc["last_duration_ms"] = result.durationMs;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    server.on("/api/power/on", HTTP_POST, [](AsyncWebServerRequest *req) {
        relayPowerOn();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/power/off", HTTP_POST, [](AsyncWebServerRequest *req) {
        relayPowerOff();
        req->send(200, "application/json", "{\"ok\":true}");
    });

    server.on("/api/backup/trigger", HTTP_POST, [](AsyncWebServerRequest *req) {
        OrchestratorState st = orchestratorGetState();
        if (st != STATE_IDLE && st != STATE_COMPLETE && st != STATE_ERROR) {
            req->send(409, "application/json", "{\"ok\":false,\"error\":\"Already running\"}");
            return;
        }
        orchestratorStart();
        req->send(200, "application/json", "{\"ok\":true,\"message\":\"Backup scenario started\"}");
    });

    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *req) {
        auto& cfg = configGet();
        JsonDocument doc;
        doc["relay_pin"] = cfg.relayPin;
        doc["pi_ip"] = cfg.piIp;
        doc["pi_port"] = cfg.piPort;
        doc["schedule_interval_min"] = cfg.scheduleIntervalMin;

        String json;
        serializeJson(doc, json);
        req->send(200, "application/json", json);
    });

    // Config POST — receives JSON body
    server.on("/api/config", HTTP_POST,
        // Request handler (called after body is received)
        [](AsyncWebServerRequest *req) {},
        // Upload handler (not used)
        nullptr,
        // Body handler
        [](AsyncWebServerRequest *req, uint8_t *data, size_t len, size_t index, size_t total) {
            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, data, len);
            if (err) {
                req->send(400, "application/json", "{\"ok\":false,\"error\":\"Invalid JSON\"}");
                return;
            }

            // ── Validate before applying ─────────────────────────
            if (doc.containsKey("relay_pin")) {
                int pin = doc["relay_pin"];
                if (pin < 0 || pin > 33) {
                    req->send(400, "application/json", "{\"ok\":false,\"error\":\"relay_pin must be 0-33\"}");
                    return;
                }
                if (isEthPin(pin)) {
                    req->send(400, "application/json", "{\"ok\":false,\"error\":\"Pin reserved for Ethernet\"}");
                    return;
                }
            }
            if (doc.containsKey("pi_port")) {
                int port = doc["pi_port"];
                if (port < 1 || port > 65535) {
                    req->send(400, "application/json", "{\"ok\":false,\"error\":\"pi_port must be 1-65535\"}");
                    return;
                }
            }
            if (doc.containsKey("schedule_interval_min")) {
                int interval = doc["schedule_interval_min"];
                if (interval < 1) {
                    req->send(400, "application/json", "{\"ok\":false,\"error\":\"schedule_interval_min must be >= 1\"}");
                    return;
                }
            }

            // ── Apply validated config ───────────────────────────
            auto& cfg = configGet();
            bool changed = false;

            if (doc.containsKey("relay_pin")) {
                int newPin = doc["relay_pin"];
                if (newPin != cfg.relayPin) {
                    cfg.relayPin = newPin;
                    relaySetPin(newPin);
                    changed = true;
                }
            }
            if (doc.containsKey("pi_ip")) {
                cfg.piIp = doc["pi_ip"].as<String>();
                changed = true;
            }
            if (doc.containsKey("pi_port")) {
                cfg.piPort = doc["pi_port"];
                changed = true;
            }
            if (doc.containsKey("schedule_interval_min")) {
                cfg.scheduleIntervalMin = doc["schedule_interval_min"];
                schedulerSetInterval(cfg.scheduleIntervalMin);
                changed = true;
            }

            if (changed) {
                configSave();
                // Update pi_client target
                extern void piClientSetTarget(const String& ip, int port);
                piClientSetTarget(cfg.piIp, cfg.piPort);
            }

            req->send(200, "application/json", "{\"ok\":true}");
        }
    );

    server.on("/api/reboot", HTTP_POST, [](AsyncWebServerRequest *req) {
        req->send(200, "application/json", "{\"ok\":true,\"message\":\"Rebooting...\"}");
        delay(500);
        ESP.restart();
    });

    // ── Static files from LittleFS ───────────────────────────────────

    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
    Serial.println("[WEB] Server started on port 80");
}
