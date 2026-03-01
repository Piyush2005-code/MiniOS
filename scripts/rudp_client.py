#!/usr/bin/env python3
"""
scripts/rudp_client.py — Host-side RUDP client for MiniOS-NetProtocol

Sends ML inference commands to the ARM64 unikernel over raw Ethernet
(RUDP frames) using a tap0 interface.

Usage:
    # Load a model
    python3 rudp_client.py --cmd load_model --file model.onnx

    # Run inference
    python3 rudp_client.py --cmd run_inference

    # Get results
    python3 rudp_client.py --cmd get_results

    # System status
    python3 rudp_client.py --cmd status

Requirements:
    pip install scapy
    sudo python3 rudp_client.py ...   (raw socket requires root)

Interface:    tap0 (or set --iface)
EtherType:    0x88B5 (RUDP)
"""

import argparse
import struct
import time
import sys

try:
    from scapy.all import Ether, sendp, sniff, conf
    SCAPY_AVAILABLE = True
except ImportError:
    SCAPY_AVAILABLE = False
    print("[client] scapy not found — using raw socket fallback")
    import socket
    import os

# -----------------------------------------------------------------------
# Protocol constants (mirror net_types.h)
# -----------------------------------------------------------------------
RUDP_ETHERTYPE  = 0x88B5
RUDP_MAGIC      = 0xAE
RUDP_MAX_PAYLOAD = 1472

# Flags
FLAG_RELIABLE   = 0x01
FLAG_ACK        = 0x02
FLAG_NACK       = 0x04
FLAG_FRAG       = 0x08
FLAG_FRAG_END   = 0x10
FLAG_KEEPALIVE  = 0x20
FLAG_RESET      = 0x40

# Commands
CMD_LOAD_MODEL      = 0x01
CMD_SET_INPUT       = 0x02
CMD_RUN_INFERENCE   = 0x03
CMD_GET_RESULTS     = 0x04
CMD_SYSTEM_STATUS   = 0x05
CMD_CONFIG_UPDATE   = 0x06
CMD_KEEPALIVE       = 0x10

RELIABLE_CMDS = {CMD_LOAD_MODEL, CMD_SET_INPUT,
                 CMD_RUN_INFERENCE, CMD_GET_RESULTS, CMD_CONFIG_UPDATE}

# Default MACs
LOCAL_MAC   = "52:54:00:12:34:57"   # host (peer from kernel's perspective)
REMOTE_MAC  = "52:54:00:12:34:56"   # ARM64 unikernel

# -----------------------------------------------------------------------
# CRC-16 / CCITT-FALSE
# -----------------------------------------------------------------------
def crc16(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc = ((crc << 8) ^ _crc16_table[((crc >> 8) ^ b) & 0xFF]) & 0xFFFF
    return crc

_crc16_table = [0] * 256
def _build_crc16_table():
    for i in range(256):
        c = i << 8
        for _ in range(8):
            if c & 0x8000:
                c = ((c << 1) ^ 0x1021) & 0xFFFF
            else:
                c = (c << 1) & 0xFFFF
        _crc16_table[i] = c
_build_crc16_table()

# -----------------------------------------------------------------------
# Frame construction
# -----------------------------------------------------------------------
_tx_seq = 1

def build_rudp_frame(src_mac: str, dst_mac: str,
                     cmd: int, payload: bytes = b'',
                     flags: int = 0, seq: int = 0,
                     ack_seq: int = 0) -> bytes:
    # Ethernet header
    dst = bytes(int(x, 16) for x in dst_mac.split(':'))
    src = bytes(int(x, 16) for x in src_mac.split(':'))
    eth = dst + src + struct.pack('>H', RUDP_ETHERTYPE)

    # RUDP header: magic(1) flags(1) cmd(1) reserved(1) seq(4) len(2) ack(2)
    is_reliable = (cmd in RELIABLE_CMDS)
    if is_reliable:
        flags |= FLAG_RELIABLE
    rudp_hdr = struct.pack('>BBBB I HH',
                           RUDP_MAGIC, flags, cmd, 0,
                           seq, len(payload), ack_seq)

    body = eth + rudp_hdr + payload
    c = crc16(body)
    return body + struct.pack('>H', c)

def next_seq():
    global _tx_seq
    s = _tx_seq
    _tx_seq += 1
    return s

# -----------------------------------------------------------------------
# Send via scapy
# -----------------------------------------------------------------------
def send_frame(frame: bytes, iface: str):
    if SCAPY_AVAILABLE:
        raw = Ether(frame)
        sendp(raw, iface=iface, verbose=False)
    else:
        # Raw socket fallback (Linux only)
        s = socket.socket(socket.AF_PACKET, socket.SOCK_RAW)
        s.bind((iface, 0))
        s.send(frame)
        s.close()

# -----------------------------------------------------------------------
# Commands
# -----------------------------------------------------------------------
def cmd_keepalive(iface, src_mac, dst_mac):
    frame = build_rudp_frame(src_mac, dst_mac, CMD_KEEPALIVE,
                             flags=FLAG_KEEPALIVE, seq=next_seq())
    send_frame(frame, iface)
    print(f"[client] Sent KEEPALIVE seq={_tx_seq - 1}")

def cmd_load_model(iface, src_mac, dst_mac, filename: str):
    with open(filename, 'rb') as f:
        data = f.read()
    total = len(data)
    print(f"[client] Loading model: {filename} ({total} bytes)")

    offset = 0
    frag_idx = 0
    while offset < total:
        chunk = data[offset:offset + RUDP_MAX_PAYLOAD]
        is_last = (offset + len(chunk) >= total)
        flags = FLAG_FRAG_END if is_last else FLAG_FRAG
        c = CMD_LOAD_MODEL if frag_idx == 0 else 0x12  # NET_CMD_FRAG_DATA
        frame = build_rudp_frame(src_mac, dst_mac, c,
                                 payload=chunk, flags=flags,
                                 seq=next_seq(), ack_seq=frag_idx)
        send_frame(frame, iface)
        print(f"  Fragment {frag_idx}: {len(chunk)} bytes {'(last)' if is_last else ''}")
        offset += len(chunk)
        frag_idx += 1
        time.sleep(0.01)  # brief pause between fragments

def cmd_run_inference(iface, src_mac, dst_mac):
    frame = build_rudp_frame(src_mac, dst_mac, CMD_RUN_INFERENCE,
                             seq=next_seq())
    send_frame(frame, iface)
    print("[client] Sent RUN_INFERENCE")

def cmd_get_results(iface, src_mac, dst_mac):
    frame = build_rudp_frame(src_mac, dst_mac, CMD_GET_RESULTS,
                             seq=next_seq())
    send_frame(frame, iface)
    print("[client] Sent GET_RESULTS — waiting for response...")
    # Simple sniff for one response frame
    if SCAPY_AVAILABLE:
        pkts = sniff(iface=iface, count=1, timeout=5,
                     filter=f"ether proto {RUDP_ETHERTYPE}")
        for p in pkts:
            raw = bytes(p)
            if len(raw) > 26 and raw[14] == RUDP_MAGIC:
                payload_len = struct.unpack('>H', raw[22:24])[0]
                payload = raw[26:26 + payload_len]
                print(f"[client] Result payload ({payload_len} bytes): {payload.hex()}")

def cmd_status(iface, src_mac, dst_mac):
    frame = build_rudp_frame(src_mac, dst_mac, CMD_SYSTEM_STATUS,
                             seq=next_seq())
    send_frame(frame, iface)
    print("[client] Sent SYSTEM_STATUS")

# -----------------------------------------------------------------------
# CLI
# -----------------------------------------------------------------------
def main():
    parser = argparse.ArgumentParser(description="MiniOS RUDP host client")
    parser.add_argument('--iface',  default='tap0',         help='Network interface')
    parser.add_argument('--src',    default=LOCAL_MAC,      help='Source MAC')
    parser.add_argument('--dst',    default=REMOTE_MAC,     help='Destination MAC')
    parser.add_argument('--cmd',    required=True,
                        choices=['keepalive', 'load_model', 'run_inference',
                                 'get_results', 'status'],
                        help='Command to send')
    parser.add_argument('--file',   default=None,           help='Model file for load_model')
    args = parser.parse_args()

    if args.cmd == 'keepalive':
        cmd_keepalive(args.iface, args.src, args.dst)
    elif args.cmd == 'load_model':
        if not args.file:
            print("--file required for load_model"); sys.exit(1)
        cmd_load_model(args.iface, args.src, args.dst, args.file)
    elif args.cmd == 'run_inference':
        cmd_run_inference(args.iface, args.src, args.dst)
    elif args.cmd == 'get_results':
        cmd_get_results(args.iface, args.src, args.dst)
    elif args.cmd == 'status':
        cmd_status(args.iface, args.src, args.dst)

if __name__ == '__main__':
    main()