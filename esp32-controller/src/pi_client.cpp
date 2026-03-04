#include "pi_client.h"
#include <HTTPClient.h>
#include <ArduinoJson.h>

static String _piIp;
static int _piPort;

void piClientInit(const String& ip, int port) {
    _piIp = ip;
    _piPort = port;
}

void piClientSetTarget(const String& ip, int port) {
    _piIp = ip;
    _piPort = port;
}

static String buildUrl(const String& path) {
    return "http://" + _piIp + ":" + String(_piPort) + path;
}

bool piCheckHealth() {
    HTTPClient http;
    http.setTimeout(5000);
    http.begin(buildUrl("/api/health"));

    int code = http.GET();
    http.end();

    if (code != 200 && code != -1) {
        Serial.printf("[PI] Health check HTTP %d\n", code);
    }
    return code == 200;
}

String piTriggerBackup() {
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(buildUrl("/api/backup/start"));
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{}");
    String jobId = "";

    if (code == 202 || code == 200) {
        String body = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            jobId = doc["job_id"].as<String>();
        }
    } else {
        Serial.printf("[PI] Trigger backup HTTP %d\n", code);
    }

    http.end();
    return jobId;
}

PiStatus piPollStatus() {
    PiStatus status = {"", "", "", "", false};

    HTTPClient http;
    http.setTimeout(10000);
    http.begin(buildUrl("/api/backup/status"));

    int code = http.GET();
    if (code == 200) {
        String body = http.getString();
        JsonDocument doc;
        if (!deserializeJson(doc, body)) {
            status.state = doc["state"].as<String>();
            status.jobId = doc["job_id"].as<String>();
            status.error = doc["error"].as<String>();
            if (!doc["report"].isNull()) {
                status.reportStatus = doc["report"]["status"].as<String>();
            }
            status.valid = true;
        }
    } else {
        Serial.printf("[PI] Poll status HTTP %d\n", code);
    }

    http.end();
    return status;
}

bool piRequestShutdown() {
    HTTPClient http;
    http.setTimeout(10000);
    http.begin(buildUrl("/api/shutdown"));
    http.addHeader("Content-Type", "application/json");

    int code = http.POST("{}");
    http.end();

    if (code != 200) {
        Serial.printf("[PI] Shutdown request HTTP %d\n", code);
    }
    return code == 200;
}
