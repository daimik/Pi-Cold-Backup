#include "ethernet_manager.h"
#include <ETH.h>
#include <Network.h>

static bool _connected = false;
static String _ip = "";

// WT32-ETH01 pin assignments for LAN8720
#define ETH_PHY_TYPE  ETH_PHY_LAN8720
#define ETH_PHY_ADDR  1
#define ETH_PHY_MDC   23
#define ETH_PHY_MDIO  18
#define ETH_PHY_POWER 16
#define ETH_CLK_MODE  ETH_CLOCK_GPIO0_IN

static void onEthEvent(arduino_event_id_t event) {
    switch (event) {
        case ARDUINO_EVENT_ETH_START:
            Serial.println("[ETH] Started");
            ETH.setHostname("cold-backup-esp32");
            break;
        case ARDUINO_EVENT_ETH_CONNECTED:
            Serial.println("[ETH] Link up");
            break;
        case ARDUINO_EVENT_ETH_GOT_IP:
            _connected = true;
            _ip = ETH.localIP().toString();
            Serial.printf("[ETH] IP: %s, Speed: %dMbps, %s\n",
                          _ip.c_str(), ETH.linkSpeed(),
                          ETH.fullDuplex() ? "Full Duplex" : "Half Duplex");
            break;
        case ARDUINO_EVENT_ETH_DISCONNECTED:
            _connected = false;
            _ip = "";
            Serial.println("[ETH] Link down");
            break;
        default:
            break;
    }
}

void ethernetInit() {
    Network.onEvent(onEthEvent);
    ETH.begin(ETH_PHY_TYPE, ETH_PHY_ADDR, ETH_PHY_MDC, ETH_PHY_MDIO,
              ETH_PHY_POWER, ETH_CLK_MODE);
    Serial.println("[ETH] Initializing...");
}

bool ethernetIsConnected() {
    return _connected;
}

String ethernetGetIP() {
    return _ip;
}
