#!/usr/bin/env bash
# =============================================================================
# scripts/setup_sdcard.sh — Download Pi 4B firmware and prepare SD card
#
# Usage:
#   ./scripts/setup_sdcard.sh [SD_MOUNT]
#
# Example:
#   ./scripts/setup_sdcard.sh /media/user/BOOT
#
# This script:
#   1. Downloads required firmware blobs from the Raspberry Pi firmware repo
#   2. Copies firmware + kernel + config files to the mounted FAT32 partition
#
# Prerequisites:
#   - SD card partitioned (Partition 1: FAT32) and mounted at SD_MOUNT
#   - kernel8.img already built: make PLATFORM=rpi4 rpi4
#   - wget available
# =============================================================================

set -euo pipefail

# ---- Configuration ----------------------------------------------------------
FIRMWARE_BASE="https://github.com/raspberrypi/firmware/raw/master/boot"
FIRMWARE_CACHE="/tmp/rpi4-firmware-cache"
BUILD_DIR="$(dirname "$0")/../build"
BOOT_DIR="$(dirname "$0")/../boot"

# ---- Argument ---------------------------------------------------------------
SD_MOUNT="${1:-}"
if [ -z "$SD_MOUNT" ]; then
    echo "Usage: $0 <SD_MOUNT_POINT>"
    echo "  Example: $0 /media/user/BOOT"
    echo ""
    echo "Pass the mount point of your SD card's FAT32 partition."
    exit 1
fi

if [ ! -d "$SD_MOUNT" ]; then
    echo "ERROR: Mount point '$SD_MOUNT' does not exist or is not a directory."
    exit 1
fi

echo "=== MiniOS SD Card Setup for Raspberry Pi 4B ==="
echo "    SD card mount: $SD_MOUNT"
echo ""

# ---- Step 1: Download firmware blobs ----------------------------------------
echo "[1/3] Downloading Pi 4B firmware blobs..."
mkdir -p "$FIRMWARE_CACHE/overlays"

download_if_missing() {
    local url="$1"
    local dest="$2"
    if [ -f "$dest" ]; then
        echo "  [CACHED] $(basename "$dest")"
    else
        echo "  [DOWNLOAD] $(basename "$dest") ..."
        wget -q --show-progress -O "$dest" "$url"
    fi
}

# Core firmware files (Pi 4B specific: use start4* not start.elf)
download_if_missing "$FIRMWARE_BASE/start4cd.elf"   "$FIRMWARE_CACHE/start4cd.elf"
download_if_missing "$FIRMWARE_BASE/fixup4cd.dat"   "$FIRMWARE_CACHE/fixup4cd.dat"

# Device tree blob for BCM2711 / Pi 4B
download_if_missing "$FIRMWARE_BASE/bcm2711-rpi-4-b.dtb" "$FIRMWARE_CACHE/bcm2711-rpi-4-b.dtb"

# Overlay: disable Bluetooth to free UART0 for our serial console
download_if_missing "$FIRMWARE_BASE/overlays/disable-bt.dtbo" "$FIRMWARE_CACHE/overlays/disable-bt.dtbo"

echo "  Done."
echo ""

# ---- Step 2: Check kernel image exists --------------------------------------
KERNEL_IMG="$BUILD_DIR/kernel8.img"
if [ ! -f "$KERNEL_IMG" ]; then
    echo "ERROR: $KERNEL_IMG not found."
    echo "       Build it first with: make PLATFORM=rpi4 rpi4"
    exit 1
fi

# ---- Step 3: Copy everything to the SD card ---------------------------------
echo "[2/3] Copying files to $SD_MOUNT ..."

# Firmware blobs
cp "$FIRMWARE_CACHE/start4cd.elf"            "$SD_MOUNT/"
cp "$FIRMWARE_CACHE/fixup4cd.dat"            "$SD_MOUNT/"
cp "$FIRMWARE_CACHE/bcm2711-rpi-4-b.dtb"    "$SD_MOUNT/"
mkdir -p "$SD_MOUNT/overlays"
cp "$FIRMWARE_CACHE/overlays/disable-bt.dtbo" "$SD_MOUNT/overlays/"

# Configuration files
cp "$BOOT_DIR/config.txt"    "$SD_MOUNT/config.txt"
cp "$BOOT_DIR/cmdline.txt"   "$SD_MOUNT/cmdline.txt"

# Kernel image
cp "$KERNEL_IMG"             "$SD_MOUNT/kernel8.img"

echo "  Done."
echo ""

# ---- Step 4: Sync and report ------------------------------------------------
echo "[3/3] Syncing filesystem writes..."
sync

echo ""
echo "=== SD card is ready! ==="
echo ""
echo "    FAT32 boot partition contents:"
ls -lh "$SD_MOUNT/" 2>/dev/null | grep -v "^total" || true

echo ""
echo "    Next steps:"
echo "      1. Safely unmount the SD card:  sudo umount $SD_MOUNT"
echo "      2. Insert into Raspberry Pi 4B"
echo "      3. Connect USB-TTL serial (GPIO14=TXD, GPIO15=RXD, GND=GND)"
echo "      4. Open serial console:         minicom -b 115200 -D /dev/ttyUSB0"
echo "      5. Power on the Pi"
echo ""
echo "    See boot/README_SD_SETUP.md for detailed instructions."
