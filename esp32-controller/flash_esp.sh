#!/usr/bin/env bash
# =============================================================================
# Flash ESP32 (WT32-ETH01) — Cold Backup Controller
# =============================================================================
# Builds firmware from source with PlatformIO, then flashes via USB-to-TTL.
# Can also flash pre-built binaries if PlatformIO is not available.
#
# Usage:
#   ./flash_esp.sh              # Build + flash firmware + LittleFS
#   ./flash_esp.sh --flash-only # Flash pre-built binaries (skip build)
#   ./flash_esp.sh --build-only # Build only (no flash)
#   ./flash_esp.sh --monitor    # Open serial monitor after flash
#
# Wiring (USB-to-TTL → WT32-ETH01):
#   TTL TX  → ESP32 RX (GPIO3)
#   TTL RX  → ESP32 TX (GPIO1)
#   TTL GND → ESP32 GND
#   TTL 3V3 → ESP32 3V3  (or use external 5V via 5V pin)
#
# To enter flash mode:
#   Hold IO0 (GPIO0) LOW → press EN (reset) → release IO0
#   Or: connect IO0 to GND during power-on, remove after flash starts
# =============================================================================

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PORT="${ESP_PORT:-/dev/ttyUSB0}"
BAUD="921600"
FLASH_ONLY=false
BUILD_ONLY=false
MONITOR=false

# ── Parse arguments ──────────────────────────────────────────────────

for arg in "$@"; do
    case "$arg" in
        --flash-only) FLASH_ONLY=true ;;
        --build-only) BUILD_ONLY=true ;;
        --monitor)    MONITOR=true ;;
        --port=*)     PORT="${arg#*=}" ;;
        --help|-h)
            echo "Usage: $0 [--flash-only] [--build-only] [--monitor] [--port=/dev/ttyUSB0]"
            echo ""
            echo "  --flash-only   Skip build, flash pre-built binaries from .pio/build/"
            echo "  --build-only   Build only, do not flash"
            echo "  --monitor      Open serial monitor after flashing"
            echo "  --port=PATH    Serial port (default: /dev/ttyUSB0, or set ESP_PORT)"
            exit 0
            ;;
    esac
done

echo "============================================"
echo "  Cold Backup ESP32 — Build & Flash"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"
echo ""

cd "${SCRIPT_DIR}"

# ── Check serial port ────────────────────────────────────────────────

if [ "$BUILD_ONLY" = false ]; then
    if [ ! -e "$PORT" ]; then
        echo "[!] Serial port $PORT not found"
        echo ""
        echo "Available serial ports:"
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "  (none found)"
        echo ""
        echo "Make sure the USB-to-TTL converter is connected."
        echo "Set port with: ESP_PORT=/dev/ttyUSB1 $0"
        exit 1
    fi
    echo "[i] Serial port: $PORT"

    # Ensure user has permission
    if [ ! -w "$PORT" ]; then
        echo "[!] No write access to $PORT"
        echo "    Run: sudo usermod -aG dialout $(whoami) && logout"
        exit 1
    fi
fi

# ── Build ────────────────────────────────────────────────────────────

if [ "$FLASH_ONLY" = false ]; then
    echo ""
    echo "[1/3] Building firmware..."

    # Detect ARM architecture (Raspberry Pi)
    ARCH=$(uname -m)
    if [[ "$ARCH" == arm* || "$ARCH" == aarch64 ]]; then
        echo "[i] ARM architecture detected ($ARCH)"
        echo "    Building ESP32 firmware on ARM can be slow or unsupported on 32-bit Pi 3."
        echo "    If build fails, compile on a PC and use: $0 --flash-only"
        echo ""
    fi

    # Check for PlatformIO
    if ! command -v pio &>/dev/null; then
        echo "[!] PlatformIO CLI not found"
        echo ""
        echo "Install with:"
        echo "  pip install platformio"
        echo ""
        echo "Or build on a PC first, copy .pio/build/wt32-eth01/ to this machine,"
        echo "then flash with: $0 --flash-only"
        exit 1
    fi

    pio run
    echo "[✓] Firmware built"

    echo ""
    echo "[2/3] Building LittleFS image..."
    pio run --target buildfs
    echo "[✓] LittleFS image built"
else
    echo "[i] Skipping build (--flash-only)"
fi

if [ "$BUILD_ONLY" = true ]; then
    echo ""
    echo "[✓] Build complete. Binaries in .pio/build/wt32-eth01/"
    echo "    firmware.bin  — application firmware"
    echo "    littlefs.bin  — web dashboard filesystem"
    exit 0
fi

# ── Flash ────────────────────────────────────────────────────────────

BUILD_DIR="${SCRIPT_DIR}/.pio/build/wt32-eth01"
FIRMWARE="${BUILD_DIR}/firmware.bin"
LITTLEFS="${BUILD_DIR}/littlefs.bin"
PARTITIONS="${BUILD_DIR}/partitions.bin"
BOOTLOADER="${BUILD_DIR}/bootloader.bin"

# Verify binaries exist
for f in "$FIRMWARE" "$LITTLEFS"; do
    if [ ! -f "$f" ]; then
        echo "[!] Missing: $f"
        echo "    Run build first: $0  (without --flash-only)"
        exit 1
    fi
done

echo ""
echo "[3/3] Flashing to ESP32 via $PORT..."
echo ""
echo "  >>> Put ESP32 in DOWNLOAD MODE now <<<"
echo "  >>> Hold IO0 → press EN → release IO0 <<<"
echo ""
read -p "Press Enter when ready (or Ctrl+C to cancel)... "

# Flash firmware using PlatformIO (handles all addresses automatically)
if command -v pio &>/dev/null; then
    echo ""
    echo "Flashing firmware..."
    pio run --target upload --upload-port "$PORT"

    echo ""
    echo "Flashing LittleFS (web dashboard)..."
    pio run --target uploadfs --upload-port "$PORT"
else
    # Fallback: use esptool directly (works on all architectures including ARM Pi)
    ESPTOOL=""
    if command -v esptool.py &>/dev/null; then
        ESPTOOL="esptool.py"
    elif python3 -m esptool version &>/dev/null 2>&1; then
        ESPTOOL="python3 -m esptool"
    else
        echo "[!] esptool.py not found — installing..."
        pip3 install --user esptool
        if python3 -m esptool version &>/dev/null 2>&1; then
            ESPTOOL="python3 -m esptool"
        else
            echo "[!] Failed to install esptool"
            echo "    Install manually: pip install esptool"
            exit 1
        fi
    fi

    # Verify all required binaries exist for raw esptool flash
    for f in "$BOOTLOADER" "$PARTITIONS"; do
        if [ ! -f "$f" ]; then
            echo "[!] Missing: $f"
            echo "    Raw esptool flash requires bootloader.bin and partitions.bin"
            echo "    Build with PlatformIO first (on PC), then copy .pio/build/wt32-eth01/ here"
            exit 1
        fi
    done

    echo "Flashing with esptool (works on Pi 3/4/5)..."
    $ESPTOOL --chip esp32 --port "$PORT" --baud "$BAUD" \
        --before default_reset --after hard_reset \
        write_flash -z \
        0x1000  "$BOOTLOADER" \
        0x8000  "$PARTITIONS" \
        0x10000 "$FIRMWARE" \
        0x1D0000 "$LITTLEFS"
fi

echo ""
echo "[✓] Flash complete!"
echo ""
echo "  Press EN (reset) on the ESP32 to boot."
echo "  The ESP32 IP will appear on serial output."
echo ""
echo "  To see serial output:"
echo "    pio device monitor --port $PORT --baud 115200"
echo "    # or: screen $PORT 115200"
echo ""

# ── Optional: serial monitor ─────────────────────────────────────────

if [ "$MONITOR" = true ]; then
    echo "Opening serial monitor..."
    if command -v pio &>/dev/null; then
        pio device monitor --port "$PORT" --baud 115200
    else
        screen "$PORT" 115200
    fi
fi
