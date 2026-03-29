#!/usr/bin/env python3
"""
host_send.py — Send a model file to MiniOS running in QEMU

Usage:
    python host_send.py <model_file> [serial_port]

Example:
    python host_send.py my_model.bin /dev/ttyS0
    python host_send.py my_model.bin /dev/ttyUSB0

The script sends:
    [4 bytes magic: 0x4D494E49]
    [4 bytes size, big-endian]
    [file bytes]

On the QEMU side, type 'recv' in the MiniOS shell first.
"""

import sys
import struct
import serial   # pip install pyserial

MAGIC = 0x4D494E49  # "MINI"

def send_model(filename, port):
    with open(filename, 'rb') as f:
        data = f.read()

    print(f"[*] File: {filename}")
    print(f"[*] Size: {len(data)} bytes")
    print(f"[*] Port: {port}")

    with serial.Serial(port, baudrate=115200, timeout=5) as ser:
        # Send magic
        ser.write(struct.pack('>I', MAGIC))
        # Send size
        ser.write(struct.pack('>I', len(data)))
        # Send data
        ser.write(data)
        ser.flush()

    print("[*] Transfer complete!")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    filename = sys.argv[1]
    port = sys.argv[2] if len(sys.argv) > 2 else '/dev/ttyS0'
    send_model(filename, port)