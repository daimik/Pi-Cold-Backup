# =============================================================================
# Flash ESP32 (WT32-ETH01) - Cold Backup Controller  (Windows / PowerShell)
# =============================================================================
# Usage:
#   .\flash_esp.ps1                        # Build + flash firmware + LittleFS
#   .\flash_esp.ps1 -FlashOnly             # Flash both (skip build)
#   .\flash_esp.ps1 -FirmwareOnly          # Flash firmware only (skip build)
#   .\flash_esp.ps1 -FilesystemOnly        # Flash LittleFS only (skip build)
#   .\flash_esp.ps1 -BuildOnly             # Build only (no flash)
#   .\flash_esp.ps1 -Monitor               # Open serial monitor after flash
#   .\flash_esp.ps1 -Port COM5             # Custom COM port (default: auto-detect)
#
# Requirements:
#   pip install platformio
#
# To enter flash mode on WT32-ETH01:
#   Hold IO0 (GPIO0) LOW -> press EN (reset) -> release IO0
# =============================================================================

param(
    [string] $Port           = "",
    [switch] $FlashOnly,
    [switch] $FirmwareOnly,
    [switch] $FilesystemOnly,
    [switch] $BuildOnly,
    [switch] $Monitor
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ScriptDir

$BUILD_DIR = "$ScriptDir\.pio\build\wt32-eth01"

$SkipBuild  = $FlashOnly -or $FirmwareOnly -or $FilesystemOnly
$DoFirmware = -not $FilesystemOnly
$DoFS       = -not $FirmwareOnly

Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Cold Backup ESP32 - Build + Flash"
Write-Host "  $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')"
Write-Host "============================================"
Write-Host ""

# ── Require port when flashing ────────────────────────────────────────────────

if (-not $BuildOnly) {
    if ($Port -eq "") {
        $ports = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($ports.Count -eq 0) {
            Write-Host "[!] No COM ports found. Connect the USB-to-TTL adapter." -ForegroundColor Red
            exit 1
        } elseif ($ports.Count -eq 1) {
            $Port = $ports[0]
            Write-Host "[i] Auto-detected port: $Port"
        } else {
            Write-Host "[i] Available COM ports: $($ports -join ', ')"
            Write-Host "    Check Device Manager -> Ports (COM & LPT) for your USB-to-TTL adapter."
            $Port = Read-Host "Enter COM port"
        }
    }
    Write-Host "[i] Serial port: $Port"
}

# ── Build ─────────────────────────────────────────────────────────────────────

if (-not $SkipBuild) {
    if (-not (Get-Command pio -ErrorAction SilentlyContinue)) {
        Write-Host "[!] PlatformIO CLI not found." -ForegroundColor Red
        Write-Host "    Install with:  pip install platformio"
        exit 1
    }

    Write-Host ""
    Write-Host "[1/3] Building firmware..."
    pio run
    if ($LASTEXITCODE -ne 0) { Write-Host "[!] Build failed" -ForegroundColor Red; exit 1 }
    Write-Host "[OK] Firmware built" -ForegroundColor Green

    Write-Host ""
    Write-Host "[2/3] Building LittleFS image..."
    pio run --target buildfs
    if ($LASTEXITCODE -ne 0) { Write-Host "[!] LittleFS build failed" -ForegroundColor Red; exit 1 }
    Write-Host "[OK] LittleFS image built" -ForegroundColor Green
} else {
    Write-Host "[i] Skipping build"
}

if ($BuildOnly) {
    Write-Host ""
    Write-Host "[OK] Build complete. Binaries in .pio\build\wt32-eth01\" -ForegroundColor Green
    Write-Host "    firmware.bin  - application firmware"
    Write-Host "    littlefs.bin  - web dashboard filesystem"
    exit 0
}

# ── Verify binaries ───────────────────────────────────────────────────────────

$toCheck = @()
if ($DoFirmware)  { $toCheck += "$BUILD_DIR\firmware.bin" }
if ($DoFS)        { $toCheck += "$BUILD_DIR\littlefs.bin" }

foreach ($f in $toCheck) {
    if (-not (Test-Path $f)) {
        Write-Host "[!] Missing: $f" -ForegroundColor Red
        Write-Host "    Run build first (without -FlashOnly / -FirmwareOnly / -FilesystemOnly)"
        exit 1
    }
}

# ── Flash firmware ────────────────────────────────────────────────────────────

if ($DoFirmware) {
    Write-Host ""
    Write-Host "[3a] Flashing firmware via $Port..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  >>> Put ESP32 in DOWNLOAD MODE <<<" -ForegroundColor Yellow
    Write-Host "  >>> Hold IO0 -> press EN -> release IO0 <<<" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter when ready (or Ctrl+C to cancel)"

    Write-Host ""
    Write-Host "Flashing firmware..."
    pio run --target upload --upload-port $Port
    if ($LASTEXITCODE -ne 0) { Write-Host "[!] Firmware flash failed" -ForegroundColor Red; exit 1 }
    Write-Host "[OK] Firmware flashed" -ForegroundColor Green
}

# ── Flash LittleFS ────────────────────────────────────────────────────────────

if ($DoFS) {
    Write-Host ""
    Write-Host "[3b] Flashing LittleFS via $Port..." -ForegroundColor Cyan
    Write-Host ""
    Write-Host "  >>> Put ESP32 in DOWNLOAD MODE <<<" -ForegroundColor Yellow
    Write-Host "  >>> Hold IO0 -> press EN -> release IO0 <<<" -ForegroundColor Yellow
    Write-Host ""
    Read-Host "Press Enter when ready (or Ctrl+C to cancel)"

    Write-Host ""
    Write-Host "Flashing LittleFS (web dashboard)..."
    pio run --target uploadfs --upload-port $Port
    if ($LASTEXITCODE -ne 0) { Write-Host "[!] LittleFS flash failed" -ForegroundColor Red; exit 1 }
    Write-Host "[OK] LittleFS flashed" -ForegroundColor Green
}

Write-Host ""
Write-Host "[OK] Flash complete!" -ForegroundColor Green
Write-Host ""
Write-Host "  Press EN (reset) on the ESP32 to boot."
Write-Host "  The ESP32 IP will appear on serial output."
Write-Host ""
Write-Host "  To open serial monitor:"
Write-Host "    pio device monitor --port $Port --baud 115200"
Write-Host ""

# ── Optional: serial monitor ──────────────────────────────────────────────────

if ($Monitor) {
    Write-Host "Opening serial monitor..."
    pio device monitor --port $Port --baud 115200
}
