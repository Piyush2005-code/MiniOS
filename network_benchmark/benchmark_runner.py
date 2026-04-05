#!/usr/bin/env python3
"""
benchmark_runner.py
===================
MiniOS Network Protocol Benchmark Suite
========================================

Runs REAL network benchmarks on the host machine to characterize
the performance of protocols used in the MiniOS project:

  1. UDP loopback latency (raw send/recv) — models QEMU SLIRP path
  2. TCP loopback latency — comparison baseline against SFU/UDP
  3. SFU-over-UDP loopback (full wire protocol with CRC16-CCITT + header)
  4. CRC16-CCITT throughput: payload sizes 64B → 64KB
  5. Protocol overhead analysis: header vs payload bytes
  6. Jitter (variance) under load: burst stress at different rates
  7. Packet-loss simulation: SFU reliability vs raw UDP
  8. In-flight table scan performance (O(N) lookup)
  9. Tensor serialization latency: numpy→bytes for different model input sizes
 10. End-to-end SFU inference round-trip emulation (loopback server)

All results are saved as JSON in ./data/ for reproducibility.
"""

import socket
import struct
import time
import json
import os
import threading
import random
import statistics
import sys
import numpy as np
from datetime import datetime

# ── Paths ──────────────────────────────────────────────────────────────────
BASE_DIR   = os.path.dirname(os.path.abspath(__file__))
DATA_DIR   = os.path.join(BASE_DIR, "data")
PLOTS_DIR  = os.path.join(BASE_DIR, "plots")

os.makedirs(DATA_DIR,  exist_ok=True)
os.makedirs(PLOTS_DIR, exist_ok=True)

# ── SFU Protocol Constants (mirror sfu.h + sfu_client.py) ─────────────────
SFU_MAGIC   = 0xDEAD6969
SFU_VERSION = 0x01
SFU_PORT    = 9900           # local test port (avoid conflict with real QEMU)

SFU_MSG_PING          = 0x05
SFU_MSG_PONG          = 0x06
SFU_MSG_INFER_REQUEST = 0x01
SFU_MSG_INFER_RESPONSE= 0x02
SFU_MSG_ACK           = 0x03

_HDR_FMT  = "<IBBHIIIHI"    # wait — correct format is IBBHIIIHH (24 bytes)
_HDR_FMT  = "<IBBHIIIHH"    # <I B B H I I I H H = 4+1+1+2+4+4+4+2+2 = 24
_HDR_SIZE = struct.calcsize(_HDR_FMT)
assert _HDR_SIZE == 24, f"Header size mismatch: {_HDR_SIZE}"

RUNS          = 300      # samples per test
BURST_RUNS    = 100      # for jitter tests
LARGE_RUNS    = 30       # for large payload tests
WARMUP        = 10       # warmup iterations discarded

# ── CRC16-CCITT ────────────────────────────────────────────────────────────
def crc16_ccitt(data: bytes) -> int:
    """CRC16-CCITT (poly=0x1021, init=0xFFFF). Matches SFU_Checksum() exactly."""
    if not data:
        return 0
    crc = 0xFFFF
    for byte in data:
        crc ^= (byte << 8)
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


# ── SFU Packet Builders ────────────────────────────────────────────────────
def build_sfu_packet(msg_type: int, req_id: int, payload: bytes) -> bytes:
    checksum    = crc16_ccitt(payload)
    payload_len = len(payload)
    header = struct.pack(
        _HDR_FMT,
        SFU_MAGIC,
        SFU_VERSION,
        msg_type,
        0x0000,       # flags
        req_id,
        0,            # seq_num
        1,            # total_seq
        checksum,
        payload_len,
    )
    return header + payload


def parse_sfu_packet(data: bytes) -> dict:
    if len(data) < _HDR_SIZE:
        raise ValueError("Too short")
    magic, version, msg_type, flags, req_id, seq_num, total_seq, checksum, payload_len = \
        struct.unpack_from(_HDR_FMT, data, 0)
    if magic != SFU_MAGIC:
        raise ValueError(f"Bad magic: 0x{magic:08X}")
    payload = data[_HDR_SIZE: _HDR_SIZE + payload_len]
    computed = crc16_ccitt(payload)
    if computed != checksum:
        raise ValueError(f"CRC mismatch: got {computed:#06x}, expected {checksum:#06x}")
    return {"msg_type": msg_type, "req_id": req_id, "payload": payload}


# ── Progress helper ────────────────────────────────────────────────────────
def progress(msg, n=None, total=None):
    if n is not None and total is not None:
        pct = n / total * 100
        bar_len = 40
        filled = int(bar_len * n / total)
        bar = "█" * filled + "░" * (bar_len - filled)
        print(f"\r  [{bar}] {pct:5.1f}%  {msg}", end="", flush=True)
        if n == total:
            print()
    else:
        print(f"  ▶  {msg}", flush=True)


def section(title):
    print(f"\n{'═'*70}")
    print(f"  {title}")
    print(f"{'═'*70}")


def save_json(filename: str, data: dict):
    path = os.path.join(DATA_DIR, filename)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  ✓ Saved: data/{filename}")


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 1: UDP Loopback Latency (Raw)
# ══════════════════════════════════════════════════════════════════════════
def bench_udp_raw_latency():
    section("Benchmark 1: Raw UDP Loopback Latency")
    progress("Starting UDP echo server thread…")

    PORT = 19901
    PAYLOAD_SIZES = [8, 64, 256, 512, 1024, 1448]

    results = {}

    for psize in PAYLOAD_SIZES:
        payload = os.urandom(psize)
        latencies = []

        # Server
        server_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind(("127.0.0.1", PORT))
        server_sock.settimeout(5.0)

        stop_event = threading.Event()

        def udp_echo_server():
            while not stop_event.is_set():
                try:
                    data, addr = server_sock.recvfrom(65535)
                    server_sock.sendto(data, addr)
                except socket.timeout:
                    pass
                except Exception:
                    break

        t = threading.Thread(target=udp_echo_server, daemon=True)
        t.start()

        # Client
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client_sock.settimeout(2.0)

        # Warmup
        for _ in range(10):
            try:
                client_sock.sendto(payload, ("127.0.0.1", PORT))
                client_sock.recvfrom(65535)
            except Exception:
                pass

        # Measure — fewer samples for large payloads (CRC16 cost)
        n_samples = RUNS if psize <= 256 else max(100, RUNS // (psize // 256))
        for i in range(n_samples):
            t0 = time.perf_counter_ns()
            client_sock.sendto(payload, ("127.0.0.1", PORT))
            try:
                client_sock.recvfrom(65535)
            except socket.timeout:
                continue
            t1 = time.perf_counter_ns()
            latencies.append((t1 - t0) / 1_000_000)  # ms

            if (i + 1) % 100 == 0:
                progress(f"UDP raw {psize}B", i + 1, RUNS)

        stop_event.set()
        client_sock.close()
        server_sock.close()

        if latencies:
            results[psize] = {
                "payload_bytes": psize,
                "n": len(latencies),
                "mean_ms": statistics.mean(latencies),
                "min_ms":  min(latencies),
                "max_ms":  max(latencies),
                "p50_ms":  statistics.median(latencies),
                "p95_ms":  float(np.percentile(latencies, 95)),
                "p99_ms":  float(np.percentile(latencies, 99)),
                "stdev_ms":statistics.stdev(latencies),
                "raw":     latencies,
            }
            print(f"  UDP {psize:5d}B → mean={results[psize]['mean_ms']:.3f}ms  "
                  f"p99={results[psize]['p99_ms']:.3f}ms  "
                  f"stdev={results[psize]['stdev_ms']:.4f}ms")

    save_json("bench1_udp_raw_latency.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 2: TCP Loopback Latency
# ══════════════════════════════════════════════════════════════════════════
def bench_tcp_latency():
    section("Benchmark 2: TCP Loopback Latency")

    PORT = 19902
    PAYLOAD_SIZES = [8, 64, 256, 512, 1024, 1448]
    results = {}

    for psize in PAYLOAD_SIZES:
        payload = os.urandom(psize)
        latencies = []

        # Server
        server_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind(("127.0.0.1", PORT))
        server_sock.listen(1)
        server_sock.settimeout(5.0)

        stop_event = threading.Event()

        def tcp_echo_server():
            try:
                conn, _ = server_sock.accept()
                conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
                while not stop_event.is_set():
                    try:
                        data = conn.recv(65535)
                        if not data:
                            break
                        conn.sendall(data)
                    except Exception:
                        break
                conn.close()
            except Exception:
                pass

        t = threading.Thread(target=tcp_echo_server, daemon=True)
        t.start()
        time.sleep(0.05)

        # Client
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        client_sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        client_sock.connect(("127.0.0.1", PORT))
        client_sock.settimeout(2.0)

        # Warmup
        for _ in range(10):
            try:
                client_sock.sendall(payload)
                client_sock.recv(65535)
            except Exception:
                pass

        # Measure — fewer samples for large payloads (CRC16 cost)
        n_samples = RUNS if psize <= 256 else max(100, RUNS // (psize // 256))
        for i in range(n_samples):
            t0 = time.perf_counter_ns()
            client_sock.sendall(payload)
            try:
                resp = b""
                while len(resp) < psize:
                    chunk = client_sock.recv(65535)
                    if not chunk:
                        break
                    resp += chunk
            except socket.timeout:
                continue
            t1 = time.perf_counter_ns()
            latencies.append((t1 - t0) / 1_000_000)

            if (i + 1) % 100 == 0:
                progress(f"TCP {psize}B", i + 1, RUNS)

        stop_event.set()
        client_sock.close()
        server_sock.close()

        if latencies:
            results[psize] = {
                "payload_bytes": psize,
                "n": len(latencies),
                "mean_ms": statistics.mean(latencies),
                "min_ms":  min(latencies),
                "max_ms":  max(latencies),
                "p50_ms":  statistics.median(latencies),
                "p95_ms":  float(np.percentile(latencies, 95)),
                "p99_ms":  float(np.percentile(latencies, 99)),
                "stdev_ms":statistics.stdev(latencies),
                "raw":     latencies,
            }
            print(f"  TCP {psize:5d}B → mean={results[psize]['mean_ms']:.3f}ms  "
                  f"p99={results[psize]['p99_ms']:.3f}ms  "
                  f"stdev={results[psize]['stdev_ms']:.4f}ms")

    save_json("bench2_tcp_latency.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 3: Full SFU Loopback (Header + CRC16 + Payload)
# ══════════════════════════════════════════════════════════════════════════
def bench_sfu_latency():
    section("Benchmark 3: Full SFU Protocol Loopback Latency")

    PORT = 19903
    PAYLOAD_SIZES = [8, 64, 256, 512, 1024, 1448]
    results = {}

    for psize in PAYLOAD_SIZES:
        raw_payload = os.urandom(psize)
        latencies = []

        # Server
        server_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server_sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_sock.bind(("127.0.0.1", PORT))
        server_sock.settimeout(5.0)
        stop_event = threading.Event()

        def sfu_echo_server():
            req_id = 0
            while not stop_event.is_set():
                try:
                    data, addr = server_sock.recvfrom(65535)
                    parsed = parse_sfu_packet(data)
                    # Build PONG/INFER_RESPONSE
                    if parsed["msg_type"] == SFU_MSG_PING:
                        resp = build_sfu_packet(SFU_MSG_PONG, parsed["req_id"], b"")
                    elif parsed["msg_type"] == SFU_MSG_INFER_REQUEST:
                        # Echo the payload back as a response
                        resp = build_sfu_packet(SFU_MSG_INFER_RESPONSE, parsed["req_id"], parsed["payload"])
                    else:
                        resp = build_sfu_packet(SFU_MSG_ACK, parsed["req_id"], b"")
                    server_sock.sendto(resp, addr)
                except socket.timeout:
                    pass
                except Exception:
                    pass

        t = threading.Thread(target=sfu_echo_server, daemon=True)
        t.start()

        # Client
        client_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        client_sock.settimeout(2.0)

        # Warmup
        for wi in range(10):
            try:
                pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, wi, raw_payload)
                client_sock.sendto(pkt, ("127.0.0.1", PORT))
                client_sock.recvfrom(65535)
            except Exception:
                pass

        # Measure — fewer samples for large payloads (CRC16 cost)
        n_samples = RUNS if psize <= 256 else max(100, RUNS // (psize // 256))
        for i in range(n_samples):
            req_id = i + 1000
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, req_id, raw_payload)
            t0 = time.perf_counter_ns()
            client_sock.sendto(pkt, ("127.0.0.1", PORT))
            try:
                resp_data, _ = client_sock.recvfrom(65535)
                parse_sfu_packet(resp_data)  # validate CRC on response too
            except socket.timeout:
                continue
            except ValueError:
                continue
            t1 = time.perf_counter_ns()
            latencies.append((t1 - t0) / 1_000_000)

            if (i + 1) % 100 == 0:
                progress(f"SFU {psize}B", i + 1, RUNS)

        stop_event.set()
        client_sock.close()
        server_sock.close()

        if latencies:
            results[psize] = {
                "payload_bytes": psize,
                "wire_bytes":    psize + _HDR_SIZE,
                "n":             len(latencies),
                "mean_ms":       statistics.mean(latencies),
                "min_ms":        min(latencies),
                "max_ms":        max(latencies),
                "p50_ms":        statistics.median(latencies),
                "p95_ms":        float(np.percentile(latencies, 95)),
                "p99_ms":        float(np.percentile(latencies, 99)),
                "stdev_ms":      statistics.stdev(latencies),
                "raw":           latencies,
            }
            print(f"  SFU {psize:5d}B → mean={results[psize]['mean_ms']:.3f}ms  "
                  f"p99={results[psize]['p99_ms']:.3f}ms  "
                  f"stdev={results[psize]['stdev_ms']:.4f}ms")

    save_json("bench3_sfu_latency.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 4: CRC16-CCITT Throughput
# ══════════════════════════════════════════════════════════════════════════
def bench_crc16_throughput():
    section("Benchmark 4: CRC16-CCITT Checksum Throughput vs Payload Size")

    sizes = [8, 64, 128, 256, 512, 1024, 1448, 4096, 8192, 16384, 32768, 65536]
    REPS  = 2000

    results = {}
    for size in sizes:
        data = os.urandom(size)
        times = []

        # Warmup
        for _ in range(50):
            crc16_ccitt(data)

        # Measure
        for i in range(REPS):
            t0 = time.perf_counter_ns()
            crc16_ccitt(data)
            t1 = time.perf_counter_ns()
            times.append(t1 - t0)

        mean_ns      = statistics.mean(times)
        throughput   = (size / mean_ns) * 1e9 / (1024 * 1024)  # MB/s

        results[size] = {
            "size_bytes":   size,
            "iterations":   REPS,
            "mean_ns":      mean_ns,
            "min_ns":       min(times),
            "max_ns":       max(times),
            "p99_ns":       float(np.percentile(times, 99)),
            "throughput_mbs": throughput,
            "ns_per_byte":  mean_ns / size,
        }
        print(f"  CRC16 {size:6d}B → {mean_ns:8.1f} ns  "
              f"{throughput:6.1f} MB/s  ({mean_ns/size:.2f} ns/byte)")

    save_json("bench4_crc16_throughput.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 5: SFU Header Serialization / Deserialization
# ══════════════════════════════════════════════════════════════════════════
def bench_sfu_serialize():
    section("Benchmark 5: SFU Header Serialize + Deserialize Overhead")

    PAYLOAD_SIZES = [0, 8, 64, 256, 512, 1024, 1448]
    REPS = 5000
    results = {}

    for psize in PAYLOAD_SIZES:
        payload = os.urandom(psize)

        # Serialize timing
        ser_times = []
        for _ in range(REPS):
            t0 = time.perf_counter_ns()
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, 42, payload)
            t1 = time.perf_counter_ns()
            ser_times.append(t1 - t0)

        # Deserialize timing (parse + CRC validate)
        pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, 42, payload)
        de_times = []
        for _ in range(REPS):
            t0 = time.perf_counter_ns()
            parse_sfu_packet(pkt)
            t1 = time.perf_counter_ns()
            de_times.append(t1 - t0)

        results[psize] = {
            "payload_bytes":    psize,
            "wire_bytes":       psize + _HDR_SIZE,
            "serialize_mean_ns":   statistics.mean(ser_times),
            "serialize_p99_ns":    float(np.percentile(ser_times, 99)),
            "deserialize_mean_ns": statistics.mean(de_times),
            "deserialize_p99_ns":  float(np.percentile(de_times, 99)),
            "header_overhead_pct": (_HDR_SIZE / (psize + _HDR_SIZE)) * 100 if psize > 0 else 100.0,
        }
        print(f"  SFU ser/deser {psize:5d}B → "
              f"ser={statistics.mean(ser_times):.0f}ns  "
              f"deser={statistics.mean(de_times):.0f}ns  "
              f"hdr_overhead={results[psize]['header_overhead_pct']:.1f}%")

    save_json("bench5_sfu_serialize.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 6: Jitter Under Burst Load
# ══════════════════════════════════════════════════════════════════════════
def bench_jitter_under_load():
    section("Benchmark 6: Latency Jitter Under Burst Load (UDP vs SFU)")

    BURST_SIZES = [1, 5, 10, 20, 50]
    PAYLOAD = os.urandom(256)
    PORT_UDP = 19906
    PORT_SFU = 19907

    results = {"udp": {}, "sfu": {}}

    # ─ UDP echo server ─
    udp_srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    udp_srv.bind(("127.0.0.1", PORT_UDP))
    udp_srv.settimeout(5.0)
    stop_udp = threading.Event()

    def _udp_srv():
        while not stop_udp.is_set():
            try:
                d, a = udp_srv.recvfrom(65535)
                udp_srv.sendto(d, a)
            except socket.timeout:
                pass

    threading.Thread(target=_udp_srv, daemon=True).start()

    # ─ SFU echo server ─
    sfu_srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sfu_srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sfu_srv.bind(("127.0.0.1", PORT_SFU))
    sfu_srv.settimeout(5.0)
    stop_sfu = threading.Event()

    def _sfu_srv():
        while not stop_sfu.is_set():
            try:
                d, a = sfu_srv.recvfrom(65535)
                p = parse_sfu_packet(d)
                r = build_sfu_packet(SFU_MSG_INFER_RESPONSE, p["req_id"], p["payload"])
                sfu_srv.sendto(r, a)
            except socket.timeout:
                pass
            except Exception:
                pass

    threading.Thread(target=_sfu_srv, daemon=True).start()

    for burst_size in BURST_SIZES:
        udp_latencies = []
        sfu_latencies = []

        udp_cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp_cli.settimeout(2.0)
        sfu_cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sfu_cli.settimeout(2.0)

        N = max(BURST_RUNS, 100)
        for i in range(N):
            # UDP burst
            ts = []
            ok = True
            for _ in range(burst_size):
                t0 = time.perf_counter_ns()
                udp_cli.sendto(PAYLOAD, ("127.0.0.1", PORT_UDP))
                try:
                    udp_cli.recvfrom(65535)
                except socket.timeout:
                    ok = False
                    break
                ts.append(time.perf_counter_ns() - t0)
            if ok and ts:
                udp_latencies.append(statistics.mean(ts) / 1_000_000)

            # SFU burst
            ts = []
            ok = True
            for j in range(burst_size):
                pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, i * burst_size + j, PAYLOAD)
                t0 = time.perf_counter_ns()
                sfu_cli.sendto(pkt, ("127.0.0.1", PORT_SFU))
                try:
                    sfu_cli.recvfrom(65535)
                except socket.timeout:
                    ok = False
                    break
                ts.append(time.perf_counter_ns() - t0)
            if ok and ts:
                sfu_latencies.append(statistics.mean(ts) / 1_000_000)

        udp_cli.close()
        sfu_cli.close()

        for proto, lats in [("udp", udp_latencies), ("sfu", sfu_latencies)]:
            if lats:
                results[proto][burst_size] = {
                    "burst_size": burst_size,
                    "n":          len(lats),
                    "mean_ms":    statistics.mean(lats),
                    "p99_ms":     float(np.percentile(lats, 99)),
                    "stdev_ms":   statistics.stdev(lats) if len(lats) > 1 else 0,
                    "jitter_cv":  (statistics.stdev(lats) / statistics.mean(lats) * 100)
                                   if len(lats) > 1 else 0,
                    "raw":        lats,
                }

        print(f"  Burst={burst_size:2d}: "
              f"UDP mean={results['udp'].get(burst_size,{}).get('mean_ms',0):.3f}ms "
              f"jitter={results['udp'].get(burst_size,{}).get('jitter_cv',0):.1f}%  |  "
              f"SFU mean={results['sfu'].get(burst_size,{}).get('mean_ms',0):.3f}ms "
              f"jitter={results['sfu'].get(burst_size,{}).get('jitter_cv',0):.1f}%")

    stop_udp.set()
    stop_sfu.set()

    save_json("bench6_jitter_under_load.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 7: In-Flight Table Scan (O(N) Lookup)
# ══════════════════════════════════════════════════════════════════════════
def bench_inflight_table():
    section("Benchmark 7: SFU In-Flight Table Scan Performance")

    TABLE_SIZES = [1, 4, 8, 16, 32, 64]
    REPS = 10000
    results = {}

    for tsize in TABLE_SIZES:
        # Simulate the in-flight table as a list of dicts
        table = [{"in_use": True, "req_id": i} for i in range(tsize)]
        # Search for the LAST entry (worst case O(N))
        target_req_id = tsize - 1

        times = []
        for _ in range(REPS):
            t0 = time.perf_counter_ns()
            found = -1
            for idx, entry in enumerate(table):
                if entry["in_use"] and entry["req_id"] == target_req_id:
                    found = idx
                    break
            t1 = time.perf_counter_ns()
            times.append(t1 - t0)

        results[tsize] = {
            "table_size":   tsize,
            "mean_ns":      statistics.mean(times),
            "p99_ns":       float(np.percentile(times, 99)),
            "ns_per_entry": statistics.mean(times) / tsize,
        }
        print(f"  Table size={tsize:3d}: mean={statistics.mean(times):.1f}ns  "
              f"p99={results[tsize]['p99_ns']:.1f}ns  "
              f"({results[tsize]['ns_per_entry']:.1f} ns/entry)")

    save_json("bench7_inflight_table.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 8: Tensor Serialization Latency
# ══════════════════════════════════════════════════════════════════════════
def bench_tensor_serialization():
    section("Benchmark 8: Tensor Serialization (NumPy → Bytes) Latency")

    # Representative model input sizes:
    # simple_add: [1,2], tiny_mlp: [1,10], conv: [1,3,224,224], ResNet-50 input
    SHAPES = {
        "simple_add [1,2]":     (1, 2),
        "tiny_mlp [1,10]":      (1, 10),
        "mlp [1,64]":           (1, 64),
        "mlp [1,128]":          (1, 128),
        "mlp [1,256]":          (1, 256),
        "cnn [1,1,28,28]":      (1, 1, 28, 28),    # MNIST-like
        "cnn [1,3,64,64]":      (1, 3, 64, 64),
        "cnn [1,3,224,224]":    (1, 3, 224, 224),  # ImageNet-like
    }
    REPS = 3000
    results = {}

    for name, shape in SHAPES.items():
        arr = np.random.rand(*shape).astype(np.float32)
        nbytes = arr.nbytes

        # Serialization: numpy → raw bytes (little-endian f32)
        ser_times = []
        for _ in range(REPS):
            t0 = time.perf_counter_ns()
            payload = arr.astype("<f4").tobytes()
            t1 = time.perf_counter_ns()
            ser_times.append(t1 - t0)

        # Deserialization: bytes → numpy
        de_times = []
        for _ in range(REPS):
            t0 = time.perf_counter_ns()
            out = np.frombuffer(payload, dtype="<f4").copy()
            t1 = time.perf_counter_ns()
            de_times.append(t1 - t0)

        # Full SFU packet build with tensor payload
        sfu_build_times = []
        for i in range(REPS):
            t0 = time.perf_counter_ns()
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, i, payload)
            t1 = time.perf_counter_ns()
            sfu_build_times.append(t1 - t0)

        results[name] = {
            "shape":                list(shape),
            "num_elements":         int(np.prod(shape)),
            "payload_bytes":        nbytes,
            "wire_bytes":           nbytes + _HDR_SIZE,
            "serialize_mean_ns":    statistics.mean(ser_times),
            "serialize_p99_ns":     float(np.percentile(ser_times, 99)),
            "deserialize_mean_ns":  statistics.mean(de_times),
            "deserialize_p99_ns":   float(np.percentile(de_times, 99)),
            "sfu_build_mean_ns":    statistics.mean(sfu_build_times),
            "sfu_build_p99_ns":     float(np.percentile(sfu_build_times, 99)),
            "total_overhead_us":    (statistics.mean(ser_times) + statistics.mean(sfu_build_times)) / 1000,
        }
        print(f"  {name:30s} {nbytes:8d}B  "
              f"ser={statistics.mean(ser_times)/1000:6.1f}µs  "
              f"SFU-build={statistics.mean(sfu_build_times)/1000:6.1f}µs")

    save_json("bench8_tensor_serialization.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 9: Protocol Overhead Ratio
# ══════════════════════════════════════════════════════════════════════════
def bench_protocol_overhead():
    section("Benchmark 9: Protocol Header Overhead Analysis")

    # Sizes representing typical SFU inference payloads
    payload_sizes = [8, 16, 40, 64, 128, 256, 512, 1024, 1448]
    results = {}

    print(f"\n  {'Payload':>10} | {'UDP ovhd':>10} | {'SFU ovhd':>10} | "
          f"{'TCP ovhd':>10} | {'SFU/UDP':>8} | {'Efficiency':>10}")
    print(f"  {'-'*10}-+-{'-'*10}-+-{'-'*10}-+-{'-'*10}-+-{'-'*8}-+-{'-'*10}")

    for psize in payload_sizes:
        # Layer costs (headers only):
        # Ethernet II: 14 bytes
        # IPv4:        20 bytes (fixed, no options)
        # UDP:          8 bytes
        # SFU:         24 bytes
        # TCP:         20 bytes minimum

        eth_hdr  = 14
        ipv4_hdr = 20
        udp_hdr  = 8
        tcp_hdr  = 20
        sfu_hdr  = _HDR_SIZE  # 24

        # Raw UDP path: Eth + IPv4 + UDP + payload
        udp_total  = eth_hdr + ipv4_hdr + udp_hdr + psize
        udp_ovhd   = (eth_hdr + ipv4_hdr + udp_hdr) / udp_total * 100

        # SFU path: Eth + IPv4 + UDP + SFU header + payload
        sfu_total  = eth_hdr + ipv4_hdr + udp_hdr + sfu_hdr + psize
        sfu_ovhd   = (eth_hdr + ipv4_hdr + udp_hdr + sfu_hdr) / sfu_total * 100

        # TCP path: Eth + IPv4 + TCP + payload (no SFU)
        tcp_total  = eth_hdr + ipv4_hdr + tcp_hdr + psize
        tcp_ovhd   = (eth_hdr + ipv4_hdr + tcp_hdr) / tcp_total * 100

        # SFU efficiency vs TCP (what % of wire bytes are pure data?)
        sfu_eff    = psize / sfu_total * 100
        sfu_vs_udp = sfu_ovhd - udp_ovhd  # extra SFU overhead on top of raw UDP

        results[psize] = {
            "payload_bytes":  psize,
            "udp_total":      udp_total,
            "sfu_total":      sfu_total,
            "tcp_total":      tcp_total,
            "udp_overhead_pct": udp_ovhd,
            "sfu_overhead_pct": sfu_ovhd,
            "tcp_overhead_pct": tcp_ovhd,
            "sfu_efficiency_pct": sfu_eff,
            "extra_sfu_over_udp_pts": sfu_vs_udp,
        }

        print(f"  {psize:10d} | {udp_ovhd:9.1f}% | {sfu_ovhd:9.1f}% | "
              f"{tcp_ovhd:9.1f}% | {sfu_vs_udp:+7.1f}% | {sfu_eff:9.1f}%")

    save_json("bench9_protocol_overhead.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# BENCHMARK 10: SFU vs Raw UDP — End-to-End Inference Simulation
# ══════════════════════════════════════════════════════════════════════════
def bench_e2e_inference_simulation():
    section("Benchmark 10: End-to-End Inference Round-Trip Simulation")
    progress("Simulating model inference with SFU protocol overhead…")

    # Models with different input sizes
    MODELS = {
        "simple_add":    np.zeros((1, 2),       dtype=np.float32),
        "tiny_mlp":      np.zeros((1, 10),      dtype=np.float32),
        "mlp_mid":       np.zeros((1, 128),     dtype=np.float32),
        "cnn_small":     np.zeros((1,1,28,28),  dtype=np.float32),
        "cnn_large":     np.zeros((1,3,64,64),  dtype=np.float32),
    }
    REPS = 300
    results = {}

    PORT = 19910

    # Server: echoes back as INFER_RESPONSE (simulates fast inference)
    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.settimeout(5.0)
    stop_srv = threading.Event()

    def infer_server():
        while not stop_srv.is_set():
            try:
                d, a = srv.recvfrom(65535)
                p = parse_sfu_packet(d)
                # Simulate inference: echo first 4 bytes of output
                out_payload = p["payload"][:16] if len(p["payload"]) >= 16 else p["payload"]
                r = build_sfu_packet(SFU_MSG_INFER_RESPONSE, p["req_id"], out_payload)
                srv.sendto(r, a)
            except socket.timeout:
                pass
            except Exception:
                pass

    threading.Thread(target=infer_server, daemon=True).start()
    cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cli.settimeout(2.0)

    for model_name, tensor in MODELS.items():
        payload = tensor.astype("<f4").tobytes()
        nbytes = len(payload)
        latencies = []

        # Warmup
        for wi in range(10):
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, wi, payload)
            cli.sendto(pkt, ("127.0.0.1", PORT))
            try:
                cli.recvfrom(65535)
            except Exception:
                pass

        for i in range(REPS):
            req_id = 10000 + i
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, req_id, payload)

            t0 = time.perf_counter_ns()
            cli.sendto(pkt, ("127.0.0.1", PORT))
            try:
                resp, _ = cli.recvfrom(65535)
                parse_sfu_packet(resp)
            except (socket.timeout, ValueError):
                continue
            t1 = time.perf_counter_ns()
            latencies.append((t1 - t0) / 1_000_000)

        if latencies:
            results[model_name] = {
                "model":          model_name,
                "input_bytes":    nbytes,
                "wire_bytes":     nbytes + _HDR_SIZE,
                "n":              len(latencies),
                "mean_ms":        statistics.mean(latencies),
                "min_ms":         min(latencies),
                "max_ms":         max(latencies),
                "p50_ms":         statistics.median(latencies),
                "p95_ms":         float(np.percentile(latencies, 95)),
                "p99_ms":         float(np.percentile(latencies, 99)),
                "stdev_ms":       statistics.stdev(latencies),
                "jitter_cv_pct":  statistics.stdev(latencies) / statistics.mean(latencies) * 100
                                   if len(latencies) > 1 else 0,
                "raw":            latencies,
            }
            print(f"  {model_name:15s} {nbytes:8d}B input → "
                  f"mean={statistics.mean(latencies):.3f}ms  "
                  f"p99={results[model_name]['p99_ms']:.3f}ms  "
                  f"jitter={results[model_name]['jitter_cv_pct']:.1f}%")

    stop_srv.set()
    cli.close()
    srv.close()

    save_json("bench10_e2e_inference.json", results)
    return results


# ══════════════════════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("=" * 70)
    print("  MiniOS Network Protocol Benchmark Suite")
    print(f"  {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 70)

    all_results = {}

    all_results["udp_raw"]      = bench_udp_raw_latency()
    all_results["tcp"]          = bench_tcp_latency()
    all_results["sfu"]          = bench_sfu_latency()
    all_results["crc16"]        = bench_crc16_throughput()
    all_results["serialize"]    = bench_sfu_serialize()
    all_results["jitter"]       = bench_jitter_under_load()
    all_results["inflight"]     = bench_inflight_table()
    all_results["tensor_ser"]   = bench_tensor_serialization()
    all_results["overhead"]     = bench_protocol_overhead()
    all_results["e2e_infer"]    = bench_e2e_inference_simulation()

    # Save combined summary
    summary = {
        "timestamp":    datetime.now().isoformat(),
        "runs_per_test": RUNS,
        "sfu_header_size_bytes": _HDR_SIZE,
        "crc_polynomial": "0x1021 (CRC16-CCITT)",
        "benchmarks_completed": list(all_results.keys()),
    }
    save_json("benchmark_summary.json", summary)

    print("\n" + "=" * 70)
    print("  ✅ All benchmarks complete! Data saved to network_benchmark/data/")
    print("  ▶  Run: python3 plot_results.py  to generate all plots.")
    print("=" * 70)
