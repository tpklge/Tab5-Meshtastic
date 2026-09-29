#!/usr/bin/env bash
# Build the OTA binary for M5Launcher installation.
# Produces: tab5-meshtastic-rak3172-ota.bin + SHA256SUMS.txt
# Usage: ./build_ota.sh [PORT]
# Example: ./build_ota.sh /dev/ttyACM0

set -euo pipefail

PROJECT_NAME="tab5_meshtastic_v2"
OTA_BIN="tab5-meshtastic-rak3172-ota.bin"

echo "=== Tab5-Meshtastic-RAK3172 OTA Build ==="
echo "Commit: $(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')"
echo "Date:   $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo ""

# Ensure IDF is sourced
if ! command -v idf.py &>/dev/null; then
    echo "ERROR: idf.py not found. Source ESP-IDF first:"
    echo "  source ~/esp/esp-idf/export.sh"
    exit 1
fi

# Set target and build
idf.py set-target esp32p4
idf.py build

# Copy and name the OTA binary
cp "build/${PROJECT_NAME}.bin" "${OTA_BIN}"

# Generate SHA256
sha256sum "${OTA_BIN}" > SHA256SUMS.txt
echo ""
echo "=== Build complete ==="
echo "OTA binary: ${OTA_BIN}"
cat SHA256SUMS.txt

echo ""
echo "=== Binary info ==="
ls -lh "${OTA_BIN}"
echo ""
echo "Install via M5Launcher:"
echo "  1. Copy ${OTA_BIN} to SD card root"
echo "  2. Launcher → App Manager → From SD → ${OTA_BIN}"
echo "  3. Confirm flash"
echo ""
echo "See docs/LAUNCHER.md for full instructions."

# Optional: flash if port provided
if [[ "${1:-}" != "" ]]; then
    echo ""
    echo "=== Flashing to $1 ==="
    idf.py -p "$1" flash
fi
