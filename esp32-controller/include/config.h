#pragma once

// ── Compile-time defaults (overridable via web UI / LittleFS config) ──

#define DEFAULT_RELAY_PIN             4
#define DEFAULT_PI_IP                 "192.168.1.11"
#define DEFAULT_PI_PORT               5000
#define DEFAULT_SCHEDULE_INTERVAL_MIN 43200  // 30 days in minutes

// Relay active level: HIGH = relay energized = Pi powered (active-HIGH modules)
#define RELAY_ON_LEVEL                HIGH

// ── Timeouts ──

#define PI_HEALTH_POLL_INTERVAL_MS    5000     // 5s between health polls
#define PI_HEALTH_TIMEOUT_MS          180000   // 3 min to boot
#define PI_BACKUP_POLL_INTERVAL_MS    30000    // 30s between status polls
#define PI_BACKUP_TIMEOUT_MS          7200000  // 2h max backup time
#define PI_SHUTDOWN_WAIT_MS           30000    // 30s after shutdown command

// ── Config file path on LittleFS ──

#define CONFIG_FILE                   "/config.json"
