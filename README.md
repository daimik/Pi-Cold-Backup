# Cold Backup System

**Automated cold backup solution using an ESP32 to physically power-cycle a Raspberry Pi on a schedule.** The Pi wakes up, mounts an SMB/CIFS share from your NAS, runs rsync with snapshot retention, sends a Gotify notification, and shuts down. The ESP32 then cuts power — keeping the backup drive completely offline between runs.

## Why Cold Backups?

Ransomware can encrypt every connected drive and network share. A backup that's physically powered off can't be reached. This system keeps your backup Pi offline except during scheduled backup windows, providing an air-gapped layer of protection for your data.

## Architecture

```
WT32-ETH01 (ESP32)                          Raspberry Pi 3B+
┌──────────────────────────┐                ┌─────────────────────────────┐
│  Relay Controller        │  GPIO relay    │  Backup API Service         │
│  + Web Dashboard         │ ──power on───▶ │  (Flask, systemd)           │
│  + Interval Scheduler    │                │                             │
│                          │  1. Health poll │  GET /api/health            │
│                          │ ──────────────▶│                             │
│                          │  2. Trigger     │  POST /api/backup/start     │
│                          │ ──────────────▶│  → Mount SMB, rsync,        │
│                          │  3. Poll status │    retention, Gotify notify │
│                          │ ──────────────▶│                             │
│                          │  4. Shutdown    │  POST /api/shutdown         │
│                          │ ──────────────▶│                             │
│  5. Cut relay power      │ ──power off──▶ │  (off)                      │
└──────────────────────────┘                └─────────────────────────────┘
```

## Features

- **Physical air-gap** — Pi is completely powered off between backups
- **Web dashboard** — monitor status, trigger manual backups, configure settings
- **Snapshot retention** — date-stamped rsync snapshots with hardlinks (space-efficient)
- **Gotify notifications** — push alerts with backup report (size, duration, errors)
- **Wired Ethernet** — no WiFi dependency (WT32-ETH01 has built-in LAN8720)
- **Persistent config** — ESP32 settings saved to flash, survives reboots
- **Safety first** — failed backups keep Pi powered on for debugging; power-off requires confirmation

## Hardware

| Component | Purpose |
|-----------|---------|
| **WT32-ETH01 V1.4** | ESP32 with Ethernet — runs the scheduler and web dashboard |
| **Raspberry Pi 3B+** (or 4/5) | Runs backup logic (rsync over SMB) |
| **5V Relay module** | Switches Pi power on/off via ESP32 GPIO |
| **5V Power supply** | Powers both ESP32 and Pi (through relay) |
| **USB-to-TTL adapter** | For initial ESP32 flashing only (CP2102 or CH340, 3.3V) |

### Wiring

```
 +5V PSU ──┬──────────────────── ESP32 5V pin
           │
           ├── Relay COM
           │
           └── Relay NO ──────── Pi 5V pin (GPIO header pin 2 or 4)

 ESP32 GPIO4 (configurable) ──── Relay IN
 ESP32 GND ─────────────────── Relay GND
```

- **COM** = Common (always connected to 5V supply)
- **NO** = Normally Open (Pi only gets power when relay is energized)
- Use a relay module with built-in transistor driver (most common modules work with 3.3V trigger)

## Setup

### 1. Prepare the Raspberry Pi

Copy the project to the Pi and run the install script:

```bash
cd pi-backup-service

# Edit config FIRST — set SMB IP, share, credentials, Gotify URL/token
nano config.yaml

# Install (creates user, venv, sudoers, systemd service)
chmod +x install.sh
sudo ./install.sh

# Verify
curl http://localhost:5000/api/health
```

The install script:
- Creates a `coldbackup` system user with minimal sudo privileges
- Sets up a Python venv at `/opt/cold-backup/`
- Installs the systemd service (auto-starts on boot)
- Works on Pi 3, 4, and 5 (Bullseye and Bookworm)

> **Static IP recommended:** The ESP32 needs the Pi's IP address. The install script detects your network manager and prints the correct commands to set a static IP.

### 2. Flash the ESP32

The WT32-ETH01 has **no onboard USB**. You need a USB-to-TTL adapter (3.3V).

#### USB-to-TTL wiring

```
USB-to-TTL          WT32-ETH01
──────────          ──────────
TX          →       RX (GPIO3)
RX          →       TX (GPIO1)
GND         →       GND
3V3         →       3V3
                    (or external 5V → 5V pin)
```

#### Enter flash mode

1. Connect IO0 (GPIO0) to GND
2. Press EN (reset) or power cycle
3. Release IO0 after flash starts

#### Flash (PowerShell on Windows)

Open PowerShell in the `esp32-controller` folder:

```powershell
# One-time: allow script execution
Set-ExecutionPolicy -Scope CurrentUser RemoteSigned

# Install PlatformIO if not already installed
pip install platformio

# Step 1 — Build firmware + web dashboard
.\flash_esp.ps1 -BuildOnly

# Step 2 — Flash firmware (replace COM10 with your port)
.\flash_esp.ps1 -FirmwareOnly -Port COM10

# Step 3 — Flash web dashboard (LittleFS)
.\flash_esp.ps1 -FilesystemOnly -Port COM10
```

Find your COM port in **Device Manager → Ports (COM & LPT)** — look for CH340, CP210x, or FTDI.

The script auto-detects the port if only one adapter is connected. It prompts you to enter download mode before each flash step.

All flash options:

```powershell
.\flash_esp.ps1 -BuildOnly                  # Build only, no flash
.\flash_esp.ps1 -FirmwareOnly -Port COM10   # Flash firmware only
.\flash_esp.ps1 -FilesystemOnly -Port COM10 # Flash web dashboard only
.\flash_esp.ps1 -FlashOnly -Port COM10      # Flash both (skip build)
.\flash_esp.ps1 -Port COM10                 # Build + flash everything
.\flash_esp.ps1 -Port COM10 -Monitor        # Flash + open serial monitor
```

> **Linux/Mac:** A bash script is also available — `./flash_esp.sh`. See [FLASH_INSTRUCTIONS.txt](esp32-controller/FLASH_INSTRUCTIONS.txt) for details.

#### Find the ESP32 IP

After flashing, the ESP32 gets an IP via DHCP over Ethernet:

- **Serial monitor:** `pio device monitor --baud 115200`
- **Router:** Check DHCP leases for hostname `cold-backup-esp32`
- **Network scan:** `nmap -sn 10.10.1.0/24 | grep -B2 "Espressif"`

### 3. Configure via Web Dashboard

Open `http://<esp32-ip>/` in a browser:

| Setting | Default | Description |
|---------|---------|-------------|
| Relay GPIO pin | 4 | GPIO connected to relay IN |
| Pi IP | 10.10.1.11 | Raspberry Pi IP address |
| Pi Port | 5000 | Flask API port |
| Schedule interval | 43200 min (30 days) | Time between backup runs |

Settings persist to flash — no need to reflash to change configuration.

## Pi Configuration

Edit `/etc/cold-backup/config.yaml` on the Pi:

| Section | Key Settings |
|---------|-------------|
| `smb` | NAS IP, share name, credentials, mount options |
| `backup` | Source directories, destination path, excludes, rsync options |
| `retention` | Number of snapshots to keep (0 = mirror mode, no snapshots) |
| `gotify` | Server URL and app token for push notifications |
| `post_action` | `shutdown` or `none` (what to do after backup completes) |

Secrets can be set via environment variables: `SMB_SOURCE_IP`, `SMB_SHARE`, `SMB_USER`, `SMB_PASS`, `GOTIFY_URL`, `GOTIFY_API`.

## Snapshot Retention

With `retention.keep: 6`, rsync creates date-stamped snapshots using hardlinks (only changed files use extra disk space):

```
/backup/snapshots/
├── 2025-10-01_030000/
├── 2025-11-01_030000/
├── 2025-12-01_030000/
├── 2026-01-01_030000/
├── 2026-02-01_030000/
├── 2026-03-01_030000/
└── latest -> 2026-03-01_030000
```

Set `retention.keep: 0` for simple mirror mode (no snapshots).

## Safety

- If backup **fails** (all sources missing), the Pi stays powered on for debugging — the ESP32 does not cut power
- If the Pi never responds to health checks (3 min timeout), it stays powered on
- Power-off requires confirmation in the web dashboard
- ESP32 validates config changes (rejects invalid GPIO pins, port numbers, intervals)
- SMB credentials are written to a temp file with `600` permissions and deleted immediately after mount

## Project Structure

```
cold-backup-docker/
├── esp32-controller/          # ESP32 firmware (PlatformIO/Arduino)
│   ├── data/                  # Web dashboard (HTML/CSS/JS, served from LittleFS)
│   ├── include/               # Header files
│   ├── src/                   # Firmware source
│   │   ├── main.cpp           # Entry point
│   │   ├── orchestrator.cpp   # Backup state machine
│   │   ├── relay_controller.cpp
│   │   ├── pi_client.cpp      # HTTP client for Pi API
│   │   ├── scheduler.cpp      # Interval timer
│   │   ├── web_server.cpp     # REST API + dashboard serving
│   │   ├── config_manager.cpp # LittleFS config persistence
│   │   └── ethernet_manager.cpp
│   ├── platformio.ini
│   └── flash_esp.sh           # Build/flash helper script
│
└── pi-backup-service/         # Raspberry Pi backup service
    ├── src/
    │   ├── app.py             # Flask API (health, backup, shutdown)
    │   ├── backup.py          # SMB mount, rsync, retention logic
    │   ├── notify.py          # Gotify push notifications
    │   ├── config.py          # YAML config loader
    │   ├── models.py          # Data classes
    │   └── logger.py          # Rotating file logger
    ├── config.yaml            # Template config
    ├── install.sh             # One-command Pi setup
    └── cold-backup-api.service # systemd unit file
```

## Troubleshooting

| Problem | Solution |
|---------|----------|
| ESP32 dashboard unreachable | Check Ethernet cable and DHCP. Serial monitor shows IP on boot |
| Pi never responds to health check | Verify Pi IP in ESP32 config. Test `curl http://<pi-ip>:5000/api/health` |
| SMB mount fails | Check NAS IP, share name, credentials in `config.yaml`. Test with `smbclient` |
| Backup runs but no notification | Verify Gotify URL and token in `config.yaml` |
| Relay clicks but Pi doesn't power on | Check wiring: COM to 5V supply, NO to Pi 5V, verify relay module works with 3.3V trigger |
| ESP32 crashes on boot | GPIO pin conflict — avoid pins 0, 2, 5, 12, 14, 15 (strapping pins). Default GPIO4 is safe |

## License

MIT License — see [LICENSE](LICENSE) for details.

## Contributing

Contributions are welcome. Please open an issue first to discuss what you'd like to change.
