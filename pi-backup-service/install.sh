#!/usr/bin/env bash
# =============================================================================
# Raspberry Pi — Cold Backup API Service Setup
# =============================================================================
# Run ONCE on the Raspberry Pi as root.
#
# Usage:  sudo bash install.sh
# =============================================================================

set -euo pipefail

echo "============================================"
echo "  Cold Backup API Service — Pi Setup"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"

SERVICE_USER="coldbackup"
INSTALL_DIR="/opt/cold-backup"
CONFIG_DIR="/etc/cold-backup"
LOG_DIR="/var/log/cold-backup"

# ─── 1. Install system packages ──────────────────────────────────────

echo ""
echo "[1/7] Installing system packages..."
apt-get update -qq
apt-get install -y -qq cifs-utils rsync python3 python3-venv

# ─── 2. Create service user ──────────────────────────────────────────

echo ""
echo "[2/7] Creating user '${SERVICE_USER}'..."
if id "${SERVICE_USER}" &>/dev/null; then
    echo "[i] User '${SERVICE_USER}' already exists"
else
    useradd -r -s /usr/sbin/nologin -m -d "${INSTALL_DIR}" "${SERVICE_USER}"
    echo "[✓] User created"
fi

# ─── 3. Create directories ───────────────────────────────────────────

echo ""
echo "[3/7] Creating directories..."
mkdir -p "${INSTALL_DIR}" "${CONFIG_DIR}" "${LOG_DIR}"
mkdir -p /mnt/smb-backup /backup/cold-storage /backup/snapshots
chown -R "${SERVICE_USER}:${SERVICE_USER}" "${INSTALL_DIR}" "${LOG_DIR}" /backup
echo "[✓] Directories created"

# ─── 4. Copy application files ───────────────────────────────────────

echo ""
echo "[4/7] Installing application..."
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

cp -r "${SCRIPT_DIR}/src/" "${INSTALL_DIR}/"
cp "${SCRIPT_DIR}/requirements.txt" "${INSTALL_DIR}/"

# Create venv and install dependencies
sudo -u "${SERVICE_USER}" python3 -m venv "${INSTALL_DIR}/venv"
sudo -u "${SERVICE_USER}" "${INSTALL_DIR}/venv/bin/pip" install --quiet \
    -r "${INSTALL_DIR}/requirements.txt"
echo "[✓] Application installed"

# ─── 5. Install config ───────────────────────────────────────────────

echo ""
echo "[5/7] Installing configuration..."
if [ ! -f "${CONFIG_DIR}/config.yaml" ]; then
    cp "${SCRIPT_DIR}/config.yaml" "${CONFIG_DIR}/config.yaml"
    chmod 600 "${CONFIG_DIR}/config.yaml"
    chown "${SERVICE_USER}:${SERVICE_USER}" "${CONFIG_DIR}/config.yaml"
    echo "[!] Edit ${CONFIG_DIR}/config.yaml with your actual settings"
else
    echo "[i] Config already exists — not overwriting"
fi

# ─── 6. Configure sudoers ────────────────────────────────────────────

echo ""
echo "[6/7] Configuring sudoers..."

cat > /etc/sudoers.d/cold-backup <<'EOF'
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/mount -t cifs *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/umount /mnt/smb-backup
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/umount -l /mnt/smb-backup
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/rsync *
coldbackup ALL=(ALL) NOPASSWD: /usr/sbin/shutdown *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/mkdir -p /mnt/smb-backup
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/mkdir -p /backup/*
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/rm -f /tmp/.smb_backup_creds
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/rm -rf /backup/snapshots/*
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/ln -sfn *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/chmod *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/printf *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/mountpoint *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/du *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/df *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/ls *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/test *
coldbackup ALL=(ALL) NOPASSWD: /usr/bin/readlink *
EOF
chmod 440 /etc/sudoers.d/cold-backup

if visudo -cf /etc/sudoers.d/cold-backup &>/dev/null; then
    echo "[✓] Sudoers configured"
else
    echo "[!] Sudoers syntax error"
    exit 1
fi

# ─── 7. Install systemd service ──────────────────────────────────────

echo ""
echo "[7/7] Installing systemd service..."
cp "${SCRIPT_DIR}/cold-backup-api.service" /etc/systemd/system/
systemctl daemon-reload
systemctl enable cold-backup-api.service

# Only start if config has been edited (check for placeholder values)
if grep -q "changeme" "${CONFIG_DIR}/config.yaml" 2>/dev/null; then
    echo "[!] Service installed but NOT started — config has placeholder values"
    echo ""
    echo "    1. Edit config:  sudo nano ${CONFIG_DIR}/config.yaml"
    echo "    2. Start:        sudo systemctl start cold-backup-api"
else
    systemctl start cold-backup-api.service
    echo "[✓] Service started"
fi

# ─── 8. Static IP reminder ────────────────────────────────────────────

echo ""
PI_IP=$(hostname -I | awk '{print $1}')
echo "[i] Current Pi IP: ${PI_IP}"
echo "    The ESP32 needs to know this IP. If it changes, update the ESP32 config."
echo ""
echo "    To set a static IP:"
if systemctl is-active --quiet NetworkManager 2>/dev/null; then
    # Pi 5 / Bookworm uses NetworkManager
    ETH_CON=$(nmcli -t -f NAME,TYPE con show --active 2>/dev/null | grep ethernet | head -1 | cut -d: -f1)
    echo "    (NetworkManager detected — Pi 5 / Bookworm)"
    echo "    nmcli con mod \"${ETH_CON:-Wired connection 1}\" ipv4.method manual \\"
    echo "      ipv4.addresses ${PI_IP}/24 ipv4.gateway $(ip route | grep default | awk '{print $3}' | head -1)"
    echo "    nmcli con up \"${ETH_CON:-Wired connection 1}\""
else
    # Pi 3/4 / Bullseye uses dhcpcd
    echo "    (dhcpcd detected — Pi 3/4 / Bullseye)"
    echo "    Add to /etc/dhcpcd.conf:"
    echo "      interface eth0"
    echo "      static ip_address=${PI_IP}/24"
    echo "      static routers=$(ip route | grep default | awk '{print $3}' | head -1)"
    echo "      static domain_name_servers=$(ip route | grep default | awk '{print $3}' | head -1)"
fi
echo ""
echo "    Or use a DHCP reservation on your router."

echo ""
echo "============================================"
echo "  Setup Complete!"
echo "============================================"
echo ""
echo "  Config:  ${CONFIG_DIR}/config.yaml"
echo "  Logs:    ${LOG_DIR}/backup.log"
echo "  Status:  sudo systemctl status cold-backup-api"
echo "  Test:    curl http://localhost:5000/api/health"
echo ""
