# SD Card Setup for MiniOS on Raspberry Pi 4B

This guide explains how to prepare an SD card to boot MiniOS on a Raspberry Pi 4B.

## Required SD Card Layout

```
┌─────────────────────────────────────────────┐
│  Partition 1: FAT32  (~256 MB)  /boot       │
│    Bootloader + kernel + firmware blobs      │
├─────────────────────────────────────────────┤
│  Partition 2: ext4   (rest)     /           │
│    MiniOS root filesystem (currently empty)  │
└─────────────────────────────────────────────┘
```

> **Note**: For an initial bare-metal boot, Partition 2 is not strictly required.
> The Pi firmware only needs Partition 1 (FAT32) to find and load your kernel.
> You can add Partition 2 later if you implement SD card filesystem access.

---

## Step 1: Partition the SD Card

Use `fdisk` or `parted`. Example with `fdisk` (replace `/dev/sdX` with your device):

```bash
# ⚠️ WARNING: this erases the entire SD card
sudo fdisk /dev/sdX
```

Inside fdisk:
```
o       # New DOS partition table (wipes existing)
n       # New partition
p       # Primary
1       # Partition 1
        # Default start
+256M   # 256 MB for boot

t       # Change type
b       # FAT32 (type 0x0b or 'b')

n       # New partition
p       # Primary
2       # Partition 2
        # Default start
        # Default end (use rest of card)

w       # Write and exit
```

---

## Step 2: Format the Partitions

```bash
# Format Partition 1 as FAT32
sudo mkfs.vfat -F 32 -n BOOT /dev/sdX1

# Format Partition 2 as ext4 (optional for initial testing)
sudo mkfs.ext4 -L rootfs /dev/sdX2
```

---

## Step 3: Download Required Firmware Blobs

The Pi 4B boot firmware is **not** your kernel — it's the GPU bootstrap that loads your kernel.
These blobs **must** come from the official Raspberry Pi firmware repository.

```bash
# Create a temp directory for firmware files
mkdir -p /tmp/rpi-firmware

# Download required blobs from the official repo
FIRMWARE_URL="https://github.com/raspberrypi/firmware/raw/master/boot"

# GPU firmware (Pi 4B specific — use start4* not start.elf)
wget -P /tmp/rpi-firmware "$FIRMWARE_URL/start4.elf"
wget -P /tmp/rpi-firmware "$FIRMWARE_URL/start4cd.elf"   # Cut-down (use with gpu_mem=16)
wget -P /tmp/rpi-firmware "$FIRMWARE_URL/fixup4.dat"
wget -P /tmp/rpi-firmware "$FIRMWARE_URL/fixup4cd.dat"

# Device tree blob for BCM2711 / Pi 4B
wget -P /tmp/rpi-firmware "$FIRMWARE_URL/bcm2711-rpi-4-b.dtb"

# Device tree overlays directory (needed by firmware)
mkdir -p /tmp/rpi-firmware/overlays
wget -P /tmp/rpi-firmware/overlays "$FIRMWARE_URL/overlays/disable-bt.dtbo"

# NOTE: bootcode.bin is NOT needed on Pi 4B (it boots from EEPROM, not SD bootcode)
# It is only required for Pi 3 and earlier.
```

Alternatively, use the `scripts/setup_sdcard.sh` helper which automates this.

---

## Step 4: Copy Everything to the SD Card

```bash
# Mount the FAT32 boot partition
sudo mount /dev/sdX1 /mnt/sdcard

# Copy firmware blobs
sudo cp /tmp/rpi-firmware/start4cd.elf   /mnt/sdcard/
sudo cp /tmp/rpi-firmware/fixup4cd.dat   /mnt/sdcard/
sudo cp /tmp/rpi-firmware/bcm2711-rpi-4-b.dtb /mnt/sdcard/
sudo mkdir -p /mnt/sdcard/overlays
sudo cp /tmp/rpi-firmware/overlays/disable-bt.dtbo /mnt/sdcard/overlays/

# Copy firmware config
sudo cp boot/config.txt    /mnt/sdcard/config.txt
sudo cp boot/cmdline.txt   /mnt/sdcard/cmdline.txt

# Copy your kernel (built with: make PLATFORM=rpi4 rpi4)
sudo cp build/kernel8.img  /mnt/sdcard/kernel8.img

# Flush writes and unmount
sync
sudo umount /mnt/sdcard
```

Or use the Makefile deploy target:
```bash
make PLATFORM=rpi4 deploy SD_MOUNT=/mnt/sdcard
# (Copy firmware blobs manually first — Makefile only copies kernel + config)
```

---

## Step 5: Boot the Pi

1. Insert the SD card into the Pi 4B
2. Connect a USB-to-TTL serial adapter:
   - Adapter GND → Pi GPIO Pin 6 (GND)
   - Adapter RXD → Pi GPIO Pin 8 (GPIO14 / TXD0)
   - Adapter TXD → Pi GPIO Pin 10 (GPIO15 / RXD0)
3. Open serial console on your host:
   ```bash
   minicom -b 115200 -D /dev/ttyUSB0
   # or
   screen /dev/ttyUSB0 115200
   ```
4. Power on the Pi — you should see MiniOS boot output within 2–3 seconds

---

## Expected Serial Output

```
[UART ] PL011 initialized (48MHz clock, 115200 baud)
[GIC ] Initializing GICv2...
[GIC ] Max IRQs: 256
[GIC ] GICv2 initialized
[LIRQ] Initializing ARM Local IRQ controller...
[LIRQ] Core 0 nCNTPNSIRQ enabled
[MMU ] Building page tables...
[MMU ] Page table at: 0x0000000000090000
[MMU ] MMU and caches enabled
[TMR ] Timer initialized: freq=54 MHz, tick=10 ms
MiniOS v1.0 — ARM64 Unikernel
...
```

---

## Troubleshooting

| Symptom | Likely Cause | Fix |
|---------|-------------|-----|
| No serial output at all | UART clock mismatch or wrong config.txt | Verify `init_uart_clock=48000000` and `dtoverlay=disable-bt` in config.txt |
| Garbled serial output | Baud rate mismatch | Make sure both sides are set to 115200 |
| Pi doesn't boot (green LED doesn't blink) | Missing firmware blobs or wrong kernel name | Check `start4cd.elf`, `fixup4cd.dat` are on FAT32; kernel MUST be named `kernel8.img` |
| Hang after "MMU enabled" | GIC or Local IRQ misconfigured | Check timer IRQ setup; verify `0xFF800000` is mapped as Device |
| "Spurious interrupt" flood | Timer not acknowledged correctly | Ensure `cntp_tval_el0` is reloaded in IRQ handler |

---

## Quick Reference: Required FAT32 Files

```
/boot (FAT32 partition) must contain:
├── config.txt          ← MiniOS firmware config (from boot/)
├── cmdline.txt         ← kernel args (from boot/)
├── kernel8.img         ← your AArch64 flat binary (from build/)
├── start4cd.elf        ← Pi 4B GPU firmware (from firmware repo)
├── fixup4cd.dat        ← linker for start4cd.elf (from firmware repo)
├── bcm2711-rpi-4-b.dtb ← device tree (from firmware repo)
└── overlays/
    └── disable-bt.dtbo ← disables BT to free UART0 (from firmware repo)
```
