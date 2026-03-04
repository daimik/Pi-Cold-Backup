#include <Arduino.h>
#include "config.h"
#include "config_manager.h"
#include "ethernet_manager.h"
#include "relay_controller.h"
#include "scheduler.h"
#include "pi_client.h"
#include "orchestrator.h"
#include "web_server.h"

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("============================================");
    Serial.println("  Cold Backup ESP32 Controller");
    Serial.println("============================================");

    // 1. Load config from LittleFS
    configInit();
    auto& cfg = configGet();

    // 2. Initialize relay FIRST — pin must be driven LOW (off) before
    //    anything else runs, to prevent relay triggering on boot
    relayInit(cfg.relayPin);

    // 3. Initialize Ethernet
    ethernetInit();

    // 4. Initialize Pi HTTP client
    piClientInit(cfg.piIp, cfg.piPort);

    // 5. Initialize scheduler
    schedulerInit(cfg.scheduleIntervalMin);

    // 6. Initialize orchestrator state machine
    orchestratorInit();

    // 7. Wait for Ethernet before starting web server
    Serial.println("[MAIN] Waiting for Ethernet...");
    unsigned long ethWait = millis();
    while (!ethernetIsConnected() && (millis() - ethWait) < 10000) {
        delay(100);
    }

    if (ethernetIsConnected()) {
        Serial.printf("[MAIN] Ethernet OK: %s\n", ethernetGetIP().c_str());
    } else {
        Serial.println("[MAIN] Ethernet not connected — continuing anyway");
    }

    // 8. Start web server
    webServerInit();

    Serial.println("[MAIN] Ready");
}

void loop() {
    // Check if scheduler fires
    if (schedulerCheck()) {
        Serial.println("[MAIN] Scheduler triggered — starting backup scenario");
        schedulerReset();
        orchestratorStart();
    }

    // Advance orchestrator state machine
    orchestratorTick();

    delay(100);
}
