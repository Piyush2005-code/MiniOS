#!/usr/bin/env python3
"""
run_remaining.py — Runs Benchmarks 8, 9, 10 only.
Data from 1-7 is already saved; this completes the suite.
"""

import socket, struct, time, json, os, threading, statistics
import numpy as np
from datetime import datetime

BASE_DIR  = os.path.dirname(os.path.abspath(__file__))
DATA_DIR  = os.path.join(BASE_DIR, "data")
os.makedirs(DATA_DIR, exist_ok=True)

SFU_MAGIC    = 0xDEAD6969
SFU_VERSION  = 0x01
_HDR_FMT     = "<IBBHIIIHH"
_HDR_SIZE    = struct.calcsize(_HDR_FMT)
assert _HDR_SIZE == 24

SFU_MSG_INFER_REQUEST  = 0x01
SFU_MSG_INFER_RESPONSE = 0x02

def crc16_ccitt(data: bytes) -> int:
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

def build_sfu_packet(msg_type, req_id, payload):
    """Only valid for payload <= 65535 bytes."""
    checksum = crc16_ccitt(payload)
    header = struct.pack(
        _HDR_FMT,
        SFU_MAGIC, SFU_VERSION, msg_type, 0x0000,
        req_id, 0, 1, checksum, len(payload),
    )
    return header + payload

def parse_sfu_packet(data):
    if len(data) < _HDR_SIZE:
        raise ValueError("Too short")
    magic, version, msg_type, flags, req_id, seq_num, total_seq, checksum, payload_len = \
        struct.unpack_from(_HDR_FMT, data, 0)
    if magic != SFU_MAGIC:
        raise ValueError("Bad magic")
    payload = data[_HDR_SIZE: _HDR_SIZE + payload_len]
    if crc16_ccitt(payload) != checksum:
        raise ValueError("CRC mismatch")
    return {"msg_type": msg_type, "req_id": req_id, "payload": payload}

def save_json(name, data):
    path = os.path.join(DATA_DIR, name)
    with open(path, "w") as f:
        json.dump(data, f, indent=2)
    print(f"  ✓ Saved: data/{name}")

def section(t):
    print(f"\n{'═'*60}\n  {t}\n{'═'*60}")


# ══════════════════════════════════════════════════════════
# BENCHMARK 8 — Tensor Serialization (fixed)
# ══════════════════════════════════════════════════════════
def bench_tensor_serialization():
    section("Benchmark 8: Tensor Serialization Latency")

    # SFU payload_len is uint16 (max 65535). We only build SFU packets
    # for tensors that fit a single frame; larger ones need fragmentation.
    SHAPES = {
        "simple_add [1,2]":   (1, 2),
        "tiny_mlp [1,10]":    (1, 10),
        "mlp [1,64]":         (1, 64),
        "mlp [1,128]":        (1, 128),
        "mlp [1,256]":        (1, 256),
        "cnn [1,1,28,28]":    (1, 1, 28, 28),   # 3136 B
        "cnn [1,3,64,64]":    (1, 3, 64, 64),   # 49152 B  — exceeds SFU MTU
        "cnn [1,3,224,224]":  (1, 3, 224, 224), # 602112 B — exceeds uint16
    }
    SFU_MAX_UINT16 = 65535
    SFU_MTU        = 1448
    REPS_BASE      = 2000
    results = {}

    for name, shape in SHAPES.items():
        arr    = np.random.rand(*shape).astype(np.float32)
        nbytes = arr.nbytes
        # Scale reps inversely with size so wall time stays bounded
        reps = max(100, min(REPS_BASE, int(REPS_BASE * 512 / max(nbytes, 512))))

        # --- Serialize ---
        payload   = b""
        ser_times = []
        for _ in range(reps):
            t0 = time.perf_counter_ns()
            payload = arr.astype("<f4").tobytes()
            t1 = time.perf_counter_ns()
            ser_times.append(t1 - t0)

        # --- Deserialize ---
        de_times = []
        for _ in range(reps):
            t0 = time.perf_counter_ns()
            np.frombuffer(payload, dtype="<f4").copy()
            t1 = time.perf_counter_ns()
            de_times.append(t1 - t0)

        # --- SFU build (only when fits in uint16) ---
        if nbytes <= SFU_MAX_UINT16:
            sfu_reps   = max(50, min(reps, int(reps * 1448 / max(nbytes, 1448))))
            sfu_times  = []
            for i in range(sfu_reps):
                t0 = time.perf_counter_ns()
                build_sfu_packet(SFU_MSG_INFER_REQUEST, i, payload)
                t1 = time.perf_counter_ns()
                sfu_times.append(t1 - t0)
            sfu_mean = statistics.mean(sfu_times)
            sfu_p99  = float(np.percentile(sfu_times, 99))
            sfu_note = "direct" if nbytes <= SFU_MTU else f"multi-frag ({nbytes//SFU_MTU+1} frames)"
        else:
            sfu_mean = None
            sfu_p99  = None
            sfu_note = "N/A — exceeds uint16 payload_len limit"

        results[name] = {
            "shape":               list(shape),
            "num_elements":        int(np.prod(shape)),
            "payload_bytes":       nbytes,
            "serialize_mean_ns":   statistics.mean(ser_times),
            "serialize_p99_ns":    float(np.percentile(ser_times, 99)),
            "deserialize_mean_ns": statistics.mean(de_times),
            "deserialize_p99_ns":  float(np.percentile(de_times, 99)),
            "sfu_build_mean_ns":   sfu_mean,
            "sfu_build_p99_ns":    sfu_p99,
            "sfu_note":            sfu_note,
            "total_overhead_us":   (statistics.mean(ser_times) + (sfu_mean or 0)) / 1000,
        }
        sfu_str = f"{sfu_mean/1000:7.1f}µs" if sfu_mean else "   N/A   "
        print(f"  {name:28s} {nbytes:8,d}B  "
              f"ser={statistics.mean(ser_times)/1000:6.2f}µs  "
              f"SFU={sfu_str}  [{sfu_note}]")

    save_json("bench8_tensor_serialization.json", results)
    return results


# ══════════════════════════════════════════════════════════
# BENCHMARK 9 — Protocol Overhead (pure math, no sockets)
# ══════════════════════════════════════════════════════════
def bench_protocol_overhead():
    section("Benchmark 9: Protocol Header Overhead Analysis")
    payload_sizes = [8, 16, 40, 64, 128, 256, 512, 1024, 1448]
    eth, ipv4, udp_h, tcp_h, sfu_h = 14, 20, 8, 20, _HDR_SIZE

    results = {}
    print(f"\n  {'Payload':>8} | {'UDP%':>7} | {'SFU%':>7} | "
          f"{'TCP%':>7} | {'Wire eff%':>9}")
    print("  " + "-" * 55)

    for ps in payload_sizes:
        udp_total = eth + ipv4 + udp_h + ps
        sfu_total = eth + ipv4 + udp_h + sfu_h + ps
        tcp_total = eth + ipv4 + tcp_h + ps

        udp_pct = (eth + ipv4 + udp_h) / udp_total * 100
        sfu_pct = (eth + ipv4 + udp_h + sfu_h) / sfu_total * 100
        tcp_pct = (eth + ipv4 + tcp_h)           / tcp_total * 100
        sfu_eff = ps / sfu_total * 100

        results[ps] = {
            "payload_bytes": ps,
            "udp_total": udp_total, "sfu_total": sfu_total, "tcp_total": tcp_total,
            "udp_overhead_pct": udp_pct, "sfu_overhead_pct": sfu_pct,
            "tcp_overhead_pct": tcp_pct, "sfu_efficiency_pct": sfu_eff,
            "extra_sfu_over_udp_pts": sfu_pct - udp_pct,
        }
        print(f"  {ps:8d} | {udp_pct:6.1f}% | {sfu_pct:6.1f}% | "
              f"{tcp_pct:6.1f}% | {sfu_eff:8.1f}%")

    save_json("bench9_protocol_overhead.json", results)
    return results


# ══════════════════════════════════════════════════════════
# BENCHMARK 10 — E2E SFU Inference Simulation
# ══════════════════════════════════════════════════════════
def bench_e2e_inference():
    section("Benchmark 10: End-to-End SFU Inference Round-Trip")

    # Max payload must fit in one UDP datagram on macOS loopback (~8K practical limit)
    # SFU_MAX_PAYLOAD=1448 in production; for E2E simulation we allow small CNN inputs.
    MODELS = {
        "simple_add":  np.zeros((1, 2),      dtype=np.float32),   #     8 B
        "tiny_mlp":    np.zeros((1, 10),     dtype=np.float32),   #    40 B
        "mlp_mid":     np.zeros((1, 128),    dtype=np.float32),   #   512 B
        "cnn_mnist":   np.zeros((1,1,28,28), dtype=np.float32),   # 3,136 B
        "cnn_tiny":    np.zeros((1,3,16,16), dtype=np.float32),   # 3,072 B — fits easily
    }
    REPS = 300
    PORT = 19910
    results = {}

    srv = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", PORT))
    srv.settimeout(5.0)
    stop_ev = threading.Event()

    def server():
        while not stop_ev.is_set():
            try:
                d, a = srv.recvfrom(65535)
                p = parse_sfu_packet(d)
                out = p["payload"][:16] if len(p["payload"]) >= 16 else p["payload"]
                srv.sendto(build_sfu_packet(SFU_MSG_INFER_RESPONSE, p["req_id"], out), a)
            except socket.timeout:
                pass
            except Exception:
                pass

    threading.Thread(target=server, daemon=True).start()
    cli = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    cli.settimeout(2.0)

    for model, tensor in MODELS.items():
        payload = tensor.astype("<f4").tobytes()
        nbytes  = len(payload)
        lats    = []

        # Warmup
        for wi in range(10):
            try:
                cli.sendto(build_sfu_packet(SFU_MSG_INFER_REQUEST, wi, payload), ("127.0.0.1", PORT))
                cli.recvfrom(65535)
            except Exception:
                pass

        for i in range(REPS):
            pkt = build_sfu_packet(SFU_MSG_INFER_REQUEST, 10000 + i, payload)
            t0  = time.perf_counter_ns()
            cli.sendto(pkt, ("127.0.0.1", PORT))
            try:
                parse_sfu_packet(cli.recvfrom(65535)[0])
            except Exception:
                continue
            lats.append((time.perf_counter_ns() - t0) / 1_000_000)

        if lats:
            results[model] = {
                "model": model, "input_bytes": nbytes,
                "wire_bytes": nbytes + _HDR_SIZE, "n": len(lats),
                "mean_ms": statistics.mean(lats),
                "min_ms":  min(lats), "max_ms": max(lats),
                "p50_ms":  statistics.median(lats),
                "p95_ms":  float(np.percentile(lats, 95)),
                "p99_ms":  float(np.percentile(lats, 99)),
                "stdev_ms": statistics.stdev(lats),
                "jitter_cv_pct": statistics.stdev(lats) / statistics.mean(lats) * 100,
                "raw": lats,
            }
            print(f"  {model:15s} {nbytes:7,d}B → "
                  f"mean={statistics.mean(lats):.3f}ms  "
                  f"p99={results[model]['p99_ms']:.3f}ms  "
                  f"jitter={results[model]['jitter_cv_pct']:.1f}%")

    stop_ev.set()
    cli.close(); srv.close()
    save_json("bench10_e2e_inference.json", results)
    return results


# ══════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("=" * 60)
    print("  Running Benchmarks 8, 9, 10 (completing the suite)")
    print(f"  {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}")
    print("=" * 60)

    bench_tensor_serialization()
    bench_protocol_overhead()
    bench_e2e_inference()

    # Update summary
    summary = {
        "timestamp":    datetime.now().isoformat(),
        "sfu_header_size_bytes": _HDR_SIZE,
        "crc_polynomial": "0x1021 (CRC16-CCITT)",
        "note": "Benchmarks 1-7 saved in prior run; 8-10 completed here.",
    }
    save_json("benchmark_summary.json", summary)
    print("\n✅ Done! All 10 benchmarks complete. Run: python3 plot_results.py")
