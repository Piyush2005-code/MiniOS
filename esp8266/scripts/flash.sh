#!/usr/bin/env bash
# ===========================================================================
# flash.sh — MiniOS-ESP8266 Flash Helper
#
# Usage:
#   ./scripts/flash.sh                  # use defaults
#   ./scripts/flash.sh /dev/ttyUSB0     # specify port
#   ./scripts/flash.sh /dev/tty.usbserial-0001 921600  # port + baud
#
# Requires: esptool.py (pip install esptool)
# ===========================================================================

set -e

PORT="${1:-/dev/ttyUSB0}"
BAUD="${2:-921600}"
BUILD_DIR="$(dirname "$0")/../build"

echo "========================================"
echo "  MiniOS-ESP8266 Flash Utility"
echo "  Port: $PORT | Baud: $BAUD"
echo "========================================"

# Check esptool is available
if ! command -v esptool.py &>/dev/null; then
    echo "Error: esptool.py not found. Install with: pip install esptool"
    exit 1
fi

# Check build artifacts exist
if [ ! -f "$BUILD_DIR/minios_esp8266-0x00000.bin" ]; then
    echo "Error: firmware not built. Run 'make' first."
    exit 1
fi

echo ""
echo "Step 1: Erasing flash..."
esptool.py --port "$PORT" --baud "$BAUD" erase_flash

echo ""
echo "Step 2: Writing firmware..."
esptool.py \
    --port "$PORT" \
    --baud "$BAUD" \
    write_flash \
    --flash_freq 80m \
    --flash_mode dio \
    --flash_size 4MB \
    0x00000 "$BUILD_DIR/minios_esp8266-0x00000.bin" \
    0x10000 "$BUILD_DIR/minios_esp8266-0x10000.bin"

echo ""
echo "Flash complete! Opening serial monitor at 115200 baud..."
echo "(Press Ctrl-A then K to exit screen)"
sleep 1
screen "$PORT" 115200
