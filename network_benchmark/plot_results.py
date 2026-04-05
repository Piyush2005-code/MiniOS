#!/usr/bin/env python3
"""
plot_results.py
===============
Generates publication-quality plots for all MiniOS Network Benchmark results.

Plots produced:
  01  udp_latency_vs_size.png        — Mean/P99 latency vs payload size (UDP)
  02  tcp_vs_udp_vs_sfu.png          — Mean latency comparison (all protocols)
  03  p99_comparison.png             — P99 tail latency across protocols
  04  crc16_throughput.png           — CRC16 throughput vs payload size
  05  crc16_latency_ns.png           — CRC16 time (ns) vs payload size
  06  sfu_serialize_overhead.png     — Serialise vs Deserialise time
  07  header_overhead_pct.png        — SFU header % overhead vs payload
  08  jitter_cv_burst.png            — Coefficient-of-Variation jitter vs burst
  09  latency_cdf.png                — CDF of per-packet latency (all protos)
  10  inflight_table_scan.png        — O(N) table-scan time vs table size
  11  tensor_ser_bar.png             — Tensor serialization latency by model
  12  e2e_inference_bar.png          — E2E inference RTT by model
  13  e2e_p99_jitter.png             — E2E P99 + jitter CV by model
  14  protocol_overhead_area.png     — Stacked header overhead vs payload
  15  latency_boxplot.png            — Box plots of raw latency distribution
  16  sfu_wire_efficiency.png        — Wire efficiency (useful bytes / total)
  17  combo_dashboard.png            — 2×3 overview dashboard
"""

import json
import os
import sys
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec
import matplotlib.patches as mpatches
from matplotlib.ticker import FuncFormatter
import seaborn as sns
from pathlib import Path

# ── Style ──────────────────────────────────────────────────────────────────
plt.rcParams.update({
    "figure.facecolor":  "#0d1117",
    "axes.facecolor":    "#161b22",
    "axes.edgecolor":    "#30363d",
    "axes.labelcolor":   "#e6edf3",
    "axes.grid":         True,
    "grid.color":        "#21262d",
    "grid.linewidth":    0.8,
    "grid.alpha":        0.6,
    "text.color":        "#e6edf3",
    "xtick.color":       "#8b949e",
    "ytick.color":       "#8b949e",
    "xtick.labelsize":   9,
    "ytick.labelsize":   9,
    "axes.titlesize":    13,
    "axes.labelsize":    11,
    "legend.facecolor":  "#161b22",
    "legend.edgecolor":  "#30363d",
    "legend.fontsize":   9,
    "font.family":       "DejaVu Sans",
    "lines.linewidth":   2.0,
    "lines.markersize":  6,
    "figure.dpi":        140,
})

# Palette
C_UDP  = "#58a6ff"   # blue
C_TCP  = "#f85149"   # red
C_SFU  = "#3fb950"   # green
C_CRC  = "#d2a8ff"   # purple
C_ORG  = "#e3b341"   # orange / accent
C_CYAN = "#39c5cf"
C_GRAY = "#8b949e"
C_GOLD = "#ffd700"

PROTO_COLORS = {"UDP (Raw)": C_UDP, "TCP": C_TCP, "SFU/UDP": C_SFU}

BASE_DIR  = Path(__file__).parent
DATA_DIR  = BASE_DIR / "data"
PLOTS_DIR = BASE_DIR / "plots"
PLOTS_DIR.mkdir(exist_ok=True)

# ── Helpers ────────────────────────────────────────────────────────────────
def load(name):
    with open(DATA_DIR / name) as f:
        return json.load(f)

def savefig(fig, name):
    path = PLOTS_DIR / name
    fig.savefig(path, bbox_inches="tight", facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"  ✓  plots/{name}")

def ms_fmt(x, _):
    return f"{x:.2f}"

def ns_fmt(x, _):
    if x >= 1e6:   return f"{x/1e6:.1f}ms"
    if x >= 1e3:   return f"{x/1e3:.1f}µs"
    return f"{x:.0f}ns"

def add_value_labels(ax, bars, fmt="{:.2f}", color="#e6edf3", fontsize=8.5):
    for bar in bars:
        h = bar.get_height()
        ax.text(
            bar.get_x() + bar.get_width() / 2,
            h * 1.02,
            fmt.format(h),
            ha="center", va="bottom",
            color=color, fontsize=fontsize, fontweight="bold",
        )

def watermark(ax):
    ax.text(
        0.99, 0.02, "MiniOS Network Benchmark",
        transform=ax.transAxes,
        fontsize=7, color="#30363d", ha="right", va="bottom",
        style="italic",
    )


# ══════════════════════════════════════════════════════════════════════════
# PLOT 01 — UDP Latency vs Payload Size
# ══════════════════════════════════════════════════════════════════════════
def plot_udp_latency():
    d = load("bench1_udp_raw_latency.json")
    sizes = sorted(int(k) for k in d)
    means  = [d[str(s)]["mean_ms"] for s in sizes]
    p99s   = [d[str(s)]["p99_ms"]  for s in sizes]
    mins   = [d[str(s)]["min_ms"]  for s in sizes]
    stdevs = [d[str(s)]["stdev_ms"] for s in sizes]

    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.plot(sizes, means, "o-", color=C_UDP, label="Mean RTT", zorder=3)
    ax.plot(sizes, p99s,  "s--", color=C_ORG, label="P99 RTT",  zorder=3)
    ax.plot(sizes, mins,  "^:",  color=C_CRC, label="Min RTT",  zorder=3)
    ax.fill_between(sizes,
                    [m - s for m, s in zip(means, stdevs)],
                    [m + s for m, s in zip(means, stdevs)],
                    color=C_UDP, alpha=0.15, label="±1σ band")

    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Round-Trip Time (ms)")
    ax.set_title("Raw UDP Loopback Latency vs Payload Size\n"
                 "127.0.0.1 loopback · 500 samples per point")
    ax.legend()
    ax.set_xticks(sizes)
    ax.set_xticklabels([str(s) for s in sizes])
    watermark(ax)
    savefig(fig, "01_udp_latency_vs_size.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 02 — TCP vs UDP vs SFU Mean Latency
# ══════════════════════════════════════════════════════════════════════════
def plot_protocol_comparison():
    udp  = load("bench1_udp_raw_latency.json")
    tcp  = load("bench2_tcp_latency.json")
    sfu  = load("bench3_sfu_latency.json")

    sizes = sorted(int(k) for k in udp if k in tcp and k in sfu)

    udp_means = [udp[str(s)]["mean_ms"] for s in sizes]
    tcp_means = [tcp[str(s)]["mean_ms"] for s in sizes]
    sfu_means = [sfu[str(s)]["mean_ms"] for s in sizes]

    x  = np.arange(len(sizes))
    w  = 0.26

    fig, ax = plt.subplots(figsize=(12, 6))
    b1 = ax.bar(x - w,     udp_means, w, color=C_UDP, label="UDP (Raw)",  zorder=3)
    b2 = ax.bar(x,         tcp_means, w, color=C_TCP, label="TCP",        zorder=3)
    b3 = ax.bar(x + w,     sfu_means, w, color=C_SFU, label="SFU/UDP",    zorder=3)

    for bars in [b1, b2, b3]:
        add_value_labels(ax, bars, "{:.3f}", fontsize=7.5)

    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Mean RTT (ms)")
    ax.set_title("Protocol Latency Comparison: TCP vs Raw UDP vs SFU\n"
                 "Loopback 127.0.0.1 · 500 samples per payload size")
    ax.set_xticks(x)
    ax.set_xticklabels([str(s) for s in sizes])
    ax.legend()
    watermark(ax)
    savefig(fig, "02_tcp_vs_udp_vs_sfu_mean.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 03 — P99 Tail Latency Comparison
# ══════════════════════════════════════════════════════════════════════════
def plot_p99_comparison():
    udp  = load("bench1_udp_raw_latency.json")
    tcp  = load("bench2_tcp_latency.json")
    sfu  = load("bench3_sfu_latency.json")

    sizes = sorted(int(k) for k in udp if k in tcp and k in sfu)

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))
    axes = axes.flatten()

    # Left: P99 grouped bar
    ax = axes[0]
    x = np.arange(len(sizes))
    w = 0.26
    udp_p99 = [udp[str(s)]["p99_ms"] for s in sizes]
    tcp_p99 = [tcp[str(s)]["p99_ms"] for s in sizes]
    sfu_p99 = [sfu[str(s)]["p99_ms"] for s in sizes]

    ax.bar(x - w, udp_p99, w, color=C_UDP, label="UDP Raw")
    ax.bar(x,     tcp_p99, w, color=C_TCP, label="TCP")
    ax.bar(x + w, sfu_p99, w, color=C_SFU, label="SFU/UDP")
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("P99 Latency (ms)")
    ax.set_title("P99 Tail Latency — All Protocols")
    ax.set_xticks(x)
    ax.set_xticklabels([str(s) for s in sizes])
    ax.legend()
    watermark(ax)

    # Right: Standard deviation (jitter) comparison
    ax = axes[1]
    udp_sd = [udp[str(s)]["stdev_ms"] for s in sizes]
    tcp_sd = [tcp[str(s)]["stdev_ms"] for s in sizes]
    sfu_sd = [sfu[str(s)]["stdev_ms"] for s in sizes]

    ax.plot(sizes, udp_sd, "o-", color=C_UDP, label="UDP σ")
    ax.plot(sizes, tcp_sd, "s-", color=C_TCP, label="TCP σ")
    ax.plot(sizes, sfu_sd, "^-", color=C_SFU, label="SFU σ")
    ax.fill_between(sizes, udp_sd, sfu_sd,
                    where=[u > s for u, s in zip(udp_sd, sfu_sd)],
                    alpha=0.12, color=C_ORG, label="SFU advantage")
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Std-Dev of RTT (ms)")
    ax.set_title("Latency Jitter (σ) — All Protocols")
    ax.set_xticks(sizes)
    ax.set_xticklabels([str(s) for s in sizes])
    ax.legend()
    watermark(ax)

    fig.suptitle("Tail Latency & Jitter Analysis", fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "03_p99_and_jitter_comparison.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 04 — CRC16 Throughput vs Payload Size
# ══════════════════════════════════════════════════════════════════════════
def plot_crc16_throughput():
    d = load("bench4_crc16_throughput.json")
    sizes  = sorted(int(k) for k in d)
    mbs    = [d[str(s)]["throughput_mbs"] for s in sizes]
    ns_per = [d[str(s)]["ns_per_byte"]    for s in sizes]
    mean_t = [d[str(s)]["mean_ns"]        for s in sizes]

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    # Throughput
    ax = axes[0]
    bars = ax.bar(range(len(sizes)), mbs, color=C_CRC, zorder=3,
                  edgecolor="#0d1117", linewidth=0.8)
    peak = max(mbs)
    ax.axhline(peak, color=C_ORG, ls="--", lw=1.2, label=f"Peak: {peak:.1f} MB/s")
    # Color gradient
    for bar, v in zip(bars, mbs):
        bar.set_alpha(0.5 + 0.5 * v / peak)
    add_value_labels(ax, bars, "{:.1f}", fontsize=7.5)
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Throughput (MB/s)")
    ax.set_title("CRC16-CCITT Throughput vs Payload Size\n"
                 "(Python implementation — mirrors SFU_Checksum() in C)")
    ax.set_xticks(range(len(sizes)))
    ax.set_xticklabels([str(s) for s in sizes], rotation=30, ha="right")
    ax.legend()
    watermark(ax)

    # ns/byte
    ax = axes[1]
    ax.semilogx(sizes, ns_per, "o-", color=C_CRC, zorder=3)
    ax.fill_between(sizes, ns_per, alpha=0.2, color=C_CRC)
    # Mark SFU MTU boundary
    ax.axvline(1448, color=C_SFU, ls="--", lw=1.2, label="SFU MTU (1448B)")
    ax.set_xlabel("Payload Size (bytes) — log scale")
    ax.set_ylabel("Time per Byte (ns/byte)")
    ax.set_title("CRC16-CCITT Cost per Byte vs Payload Size")
    ax.legend()
    watermark(ax)

    fig.suptitle("CRC16-CCITT Checksum Performance Analysis", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "04_crc16_throughput.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 05 — SFU Serialize / Deserialize Overhead
# ══════════════════════════════════════════════════════════════════════════
def plot_sfu_serialize():
    d = load("bench5_sfu_serialize.json")
    sizes  = sorted(int(k) for k in d)
    ser    = [d[str(s)]["serialize_mean_ns"]   / 1000 for s in sizes]  # µs
    de     = [d[str(s)]["deserialize_mean_ns"] / 1000 for s in sizes]
    ovhd   = [d[str(s)]["header_overhead_pct"] for s in sizes]

    x = np.arange(len(sizes))
    w = 0.35

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    ax = axes[0]
    b1 = ax.bar(x - w/2, ser, w, color=C_SFU, label="Serialize (build packet)", zorder=3)
    b2 = ax.bar(x + w/2, de,  w, color=C_UDP, label="Deserialize (parse + CRC)", zorder=3)
    add_value_labels(ax, b1, "{:.2f}", fontsize=7.5)
    add_value_labels(ax, b2, "{:.2f}", fontsize=7.5)
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("Time (µs)")
    ax.set_title("SFU Packet Serialize vs Deserialize Latency\n(5 000 iterations each)")
    ax.set_xticks(x)
    ax.set_xticklabels([str(s) for s in sizes])
    ax.legend()
    watermark(ax)

    ax = axes[1]
    ax.bar(range(len(sizes[1:])), ovhd[1:], color=C_ORG, zorder=3,
           edgecolor="#0d1117", linewidth=0.8)
    ax.axhline(5, color=C_SFU, ls="--", lw=1.2, label="5 % threshold")
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_ylabel("SFU Header Overhead (%)")
    ax.set_title("24-Byte SFU Header Overhead vs Payload Size")
    ax.set_xticks(range(len(sizes[1:])))
    ax.set_xticklabels([str(s) for s in sizes[1:]])
    ax.legend()
    watermark(ax)

    fig.suptitle("SFU Protocol Serialization Overhead", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "05_sfu_serialize_overhead.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 06 — Jitter under Burst Load
# ══════════════════════════════════════════════════════════════════════════
def plot_jitter_burst():
    d      = load("bench6_jitter_under_load.json")
    u_data = d["udp"]
    s_data = d["sfu"]
    bursts = sorted(int(k) for k in u_data if k in s_data)

    udp_cv   = [u_data[str(b)]["jitter_cv"]  for b in bursts]
    sfu_cv   = [s_data[str(b)]["jitter_cv"]  for b in bursts]
    udp_mean = [u_data[str(b)]["mean_ms"]    for b in bursts]
    sfu_mean = [s_data[str(b)]["mean_ms"]    for b in bursts]
    udp_p99  = [u_data[str(b)]["p99_ms"]     for b in bursts]
    sfu_p99  = [s_data[str(b)]["p99_ms"]     for b in bursts]

    fig, axes = plt.subplots(1, 3, figsize=(16, 5.5))

    # CV Jitter
    ax = axes[0]
    ax.plot(bursts, udp_cv, "o-", color=C_UDP, label="UDP Jitter (CV%)")
    ax.plot(bursts, sfu_cv, "s-", color=C_SFU, label="SFU Jitter (CV%)")
    ax.fill_between(bursts, udp_cv, sfu_cv,
                    where=[u > s for u, s in zip(udp_cv, sfu_cv)],
                    alpha=0.15, color=C_ORG)
    ax.set_xlabel("Burst Size (packets)")
    ax.set_ylabel("Jitter (CV %  = σ/mean × 100)")
    ax.set_title("Latency Jitter under Burst Load\n(256-byte payloads)")
    ax.legend()
    watermark(ax)

    # Mean latency
    ax = axes[1]
    ax.plot(bursts, udp_mean, "o-", color=C_UDP, label="UDP mean")
    ax.plot(bursts, sfu_mean, "s-", color=C_SFU, label="SFU mean")
    ax.set_xlabel("Burst Size (packets)")
    ax.set_ylabel("Mean per-packet RTT (ms)")
    ax.set_title("Mean Latency under Burst Load")
    ax.legend()
    watermark(ax)

    # P99
    ax = axes[2]
    ax.plot(bursts, udp_p99, "o-", color=C_UDP, label="UDP P99")
    ax.plot(bursts, sfu_p99, "s-", color=C_SFU, label="SFU P99")
    ax.set_xlabel("Burst Size (packets)")
    ax.set_ylabel("P99 RTT (ms)")
    ax.set_title("P99 Tail Latency under Burst Load")
    ax.legend()
    watermark(ax)

    fig.suptitle("Jitter & Load Stress Analysis — UDP vs SFU", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "06_jitter_burst_analysis.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 07 — Latency CDF
# ══════════════════════════════════════════════════════════════════════════
def plot_latency_cdf():
    udp = load("bench1_udp_raw_latency.json")
    tcp = load("bench2_tcp_latency.json")
    sfu = load("bench3_sfu_latency.json")

    # Use 256-byte payload as representative
    KEY = "256"

    udp_raw = np.array(udp[KEY]["raw"])
    tcp_raw = np.array(tcp[KEY]["raw"])
    sfu_raw = np.array(sfu[KEY]["raw"])

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    # CDF
    ax = axes[0]
    for raw, label, color in [
        (udp_raw, "UDP Raw", C_UDP),
        (tcp_raw, "TCP",     C_TCP),
        (sfu_raw, "SFU/UDP", C_SFU),
    ]:
        sorted_data = np.sort(raw)
        cdf = np.arange(1, len(sorted_data) + 1) / len(sorted_data)
        ax.plot(sorted_data, cdf, color=color, label=label, lw=2)
    ax.axhline(0.99, color=C_ORG, ls="--", lw=1, alpha=0.7, label="99th percentile")
    ax.axhline(0.95, color=C_GOLD, ls=":",  lw=1, alpha=0.7, label="95th percentile")
    ax.set_xlabel("RTT (ms)")
    ax.set_ylabel("Cumulative Probability")
    ax.set_title("Latency CDF — 256-byte Payload\n(500 samples each protocol)")
    ax.legend()
    ax.set_xlim(left=0)
    watermark(ax)

    # PDF (KDE) with seaborn
    ax = axes[1]
    for raw, label, color in [
        (udp_raw, "UDP Raw", C_UDP),
        (tcp_raw, "TCP",     C_TCP),
        (sfu_raw, "SFU/UDP", C_SFU),
    ]:
        sns.kdeplot(raw, ax=ax, color=color, label=label, fill=True, alpha=0.25, lw=2)
    ax.set_xlabel("RTT (ms)")
    ax.set_ylabel("Density")
    ax.set_title("Latency Distribution (KDE) — 256-byte Payload")
    ax.legend()
    watermark(ax)

    fig.suptitle("Latency Distribution Analysis", fontsize=14, fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "07_latency_cdf_kde.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 08 — In-Flight Table Scan
# ══════════════════════════════════════════════════════════════════════════
def plot_inflight_table():
    d = load("bench7_inflight_table.json")
    sizes  = sorted(int(k) for k in d)
    means  = [d[str(s)]["mean_ns"]      for s in sizes]
    p99s   = [d[str(s)]["p99_ns"]       for s in sizes]
    per_e  = [d[str(s)]["ns_per_entry"] for s in sizes]

    fig, axes = plt.subplots(1, 2, figsize=(13, 5.5))

    ax = axes[0]
    ax.plot(sizes, means, "o-", color=C_SFU, label="Mean scan time")
    ax.plot(sizes, p99s,  "s--", color=C_ORG,  label="P99 scan time")
    # Fit linear curve to show O(N)
    coef = np.polyfit(sizes, means, 1)
    fit  = np.poly1d(coef)
    x_fit = np.linspace(min(sizes), max(sizes), 200)
    ax.plot(x_fit, fit(x_fit), ":", color=C_GRAY, lw=1.5,
            label=f"Linear fit O(N) — {coef[0]:.1f}×N + {coef[1]:.0f}")
    ax.set_xlabel("In-Flight Table Size (entries)")
    ax.set_ylabel("Scan Time (ns)")
    ax.set_title("SFU In-Flight Table O(N) Scan Cost\n"
                 "(worst-case: target is last entry)")
    ax.legend()
    watermark(ax)

    ax = axes[1]
    bars = ax.bar(range(len(sizes)), per_e, color=C_CRC,
                  edgecolor="#0d1117", linewidth=0.8, zorder=3)
    add_value_labels(ax, bars, "{:.1f} ns", color=C_GRAY, fontsize=8)
    # Mark SFU default table size
    idx_16 = sizes.index(16) if 16 in sizes else None
    if idx_16 is not None:
        bars[idx_16].set_color(C_ORG)
        ax.annotate("SFU_MAX_INFLIGHT\n= 16 (default)",
                    xy=(idx_16, per_e[idx_16]),
                    xytext=(idx_16 + 0.5, max(per_e) * 0.8),
                    color=C_ORG, fontsize=9,
                    arrowprops=dict(arrowstyle="->", color=C_ORG))
    ax.set_xlabel("In-Flight Table Size (entries)")
    ax.set_ylabel("Time per Entry (ns/entry)")
    ax.set_title("Per-Entry Scan Cost vs Table Size")
    ax.set_xticks(range(len(sizes)))
    ax.set_xticklabels([str(s) for s in sizes])
    watermark(ax)

    fig.suptitle("In-Flight Reliability Table Performance", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "08_inflight_table_scan.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 09 — Tensor Serialization
# ══════════════════════════════════════════════════════════════════════════
def plot_tensor_serialization():
    d    = load("bench8_tensor_serialization.json")
    keys = list(d.keys())
    ser   = [d[k]["serialize_mean_ns"]  / 1000 for k in keys]  # µs
    de    = [d[k]["deserialize_mean_ns"]/ 1000 for k in keys]
    bld   = [d[k]["sfu_build_mean_ns"] / 1000 if d[k]["sfu_build_mean_ns"] is not None else None for k in keys]
    nbytes= [d[k]["payload_bytes"]              for k in keys]

    x  = np.arange(len(keys))
    w  = 0.28

    fig, axes = plt.subplots(1, 2, figsize=(15, 6))

    ax = axes[0]
    # Replace None with 0 so bar char works, then add text annotations for missing bars
    bld_plot = [b if b is not None else 0 for b in bld]

    b1 = ax.bar(x - w, ser, w, color=C_SFU,  label="numpy→bytes (serialize)", zorder=3)
    b2 = ax.bar(x,     de,  w, color=C_UDP,  label="bytes→numpy (deserialize)", zorder=3)
    b3 = ax.bar(x + w, bld_plot, w, color=C_ORG,  label="SFU packet build (serialize+CRC)", zorder=3)
    
    # Mark the N/A bars
    for i, b in enumerate(bld):
        if b is None:
            ax.text(x[i] + w, 0, "N/A\n(>65KB)", ha="center", va="bottom", fontsize=7, color=C_ORG, rotation=90)
            
    ax.set_xlabel("Model Input")
    ax.set_ylabel("Latency (µs)")
    ax.set_title("Tensor Serialization Latency by Model Input\n(3 000 iterations)")
    ax.set_xticks(x)
    ax.set_xticklabels([k.split()[0] for k in keys], rotation=30, ha="right")
    ax.legend(loc="upper left")
    watermark(ax)

    ax = axes[1]
    total_us = [s + (b or 0) for s, b in zip(ser, bld)]
    colors   = plt.cm.plasma(np.linspace(0.2, 0.85, len(keys)))
    bars = ax.barh(range(len(keys)), total_us, color=colors, zorder=3)
    ax.set_yticks(range(len(keys)))
    ax.set_yticklabels([f"{k}\n({nb:,}B)" for k, nb in zip(keys, nbytes)], fontsize=8)
    ax.set_xlabel("Total Overhead (µs)")
    ax.set_title("Total SFU Send Overhead per Model Input\n(serialize + CRC + header build)")
    for bar, v, b in zip(bars, total_us, bld):
        sfu_text = "" if b is not None else " [No SFU]"
        ax.text(v + 0.02, bar.get_y() + bar.get_height() / 2,
                f"{v:.2f} µs{sfu_text}", va="center", fontsize=8, color="#e6edf3")
    watermark(ax)

    fig.suptitle("Tensor Serialization & SFU Build Overhead", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "09_tensor_serialization.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 10 — E2E Inference Round-Trip
# ══════════════════════════════════════════════════════════════════════════
def plot_e2e_inference():
    d    = load("bench10_e2e_inference.json")
    keys = list(d.keys())
    means = [d[k]["mean_ms"] for k in keys]
    p99s  = [d[k]["p99_ms"]  for k in keys]
    cvs   = [d[k]["jitter_cv_pct"] for k in keys]
    nbytes= [d[k]["input_bytes"]   for k in keys]

    x = np.arange(len(keys))
    w = 0.35

    fig, axes = plt.subplots(1, 2, figsize=(14, 6))

    ax = axes[0]
    b1 = ax.bar(x - w/2, means, w, color=C_SFU, label="Mean E2E RTT", zorder=3)
    b2 = ax.bar(x + w/2, p99s,  w, color=C_ORG, label="P99 E2E RTT",  zorder=3)
    add_value_labels(ax, b1, "{:.3f}", fontsize=8)
    add_value_labels(ax, b2, "{:.3f}", fontsize=8)
    ax.set_xlabel("Model")
    ax.set_ylabel("Latency (ms)")
    ax.set_title("SFU E2E Inference Round-Trip Latency by Model\n"
                 "(loopback server simulating fast inference, 300 samples)")
    ax.set_xticks(x)
    ax.set_xticklabels([f"{k}\n({nb:,}B)" for k, nb in zip(keys, nbytes)],
                       rotation=15, ha="right", fontsize=8.5)
    ax.legend()
    watermark(ax)

    ax = axes[1]
    colors = [C_SFU if cv < 10 else C_ORG if cv < 20 else C_TCP for cv in cvs]
    bars = ax.bar(range(len(keys)), cvs, color=colors, zorder=3)
    ax.axhline(15, color=C_ORG, ls="--", lw=1.5, label="15% SRS PDR-001 limit")
    ax.set_xlabel("Model")
    ax.set_ylabel("Jitter (CV = σ/mean × 100%)")
    ax.set_title("Per-Model Inference Jitter (Coeff. of Variation)\n"
                 "Green < 10%  Orange 10-20%  Red > 20%")
    ax.set_xticks(range(len(keys)))
    ax.set_xticklabels([k for k in keys], rotation=20, ha="right")
    ax.legend()
    # Legend patches
    patches = [
        mpatches.Patch(color=C_SFU, label="< 10% (Excellent)"),
        mpatches.Patch(color=C_ORG, label="10-20% (Acceptable)"),
        mpatches.Patch(color=C_TCP, label="> 20% (Exceeds limit)"),
    ]
    ax.legend(handles=patches, loc="upper left", fontsize=8.5)
    watermark(ax)

    fig.suptitle("End-to-End SFU Inference Performance by Model", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "10_e2e_inference_rtt.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 11 — Protocol Header Overhead (Stacked Area)
# ══════════════════════════════════════════════════════════════════════════
def plot_protocol_overhead():
    d     = load("bench9_protocol_overhead.json")
    sizes = sorted(int(k) for k in d)

    udp_pct = [d[str(s)]["udp_overhead_pct"] for s in sizes]
    sfu_pct = [d[str(s)]["sfu_overhead_pct"] for s in sizes]
    tcp_pct = [d[str(s)]["tcp_overhead_pct"] for s in sizes]
    sfu_eff = [d[str(s)]["sfu_efficiency_pct"] for s in sizes]

    fig, axes = plt.subplots(1, 2, figsize=(14, 5.5))

    ax = axes[0]
    ax.semilogx(sizes, udp_pct, "o-", color=C_UDP, label="UDP (Eth+IP+UDP headers)")
    ax.semilogx(sizes, sfu_pct, "s-", color=C_SFU, label="SFU (Eth+IP+UDP+SFU headers)")
    ax.semilogx(sizes, tcp_pct, "^-", color=C_TCP, label="TCP (Eth+IP+TCP headers)")
    ax.fill_between(sizes, udp_pct, sfu_pct, alpha=0.15, color=C_SFU,
                    label="SFU 24B extra cost")
    ax.set_xlabel("Payload Size (bytes) — log scale")
    ax.set_ylabel("Header Overhead (%)")
    ax.set_title("Protocol Header Overhead vs Payload Size\n"
                 "Eth(14) + IPv4(20) + UDP(8) + SFU(24)")
    ax.legend(fontsize=8.5)
    ax.axvline(1448, color=C_ORG, ls=":", label="SFU MTU bound")
    ax.set_ylim(0, 100)
    watermark(ax)

    ax = axes[1]
    ax.semilogx(sizes, sfu_eff, "o-", color=C_SFU, lw=2.2)
    ax.fill_between(sizes, sfu_eff, alpha=0.25, color=C_SFU)
    ax.axhline(90, color=C_ORG, ls="--", lw=1.2, label="90% efficiency target")
    ax.axvline(1448, color=C_UDP, ls=":", lw=1.2, label="SFU MTU (1448B)")
    # Annotate MTU efficiency
    eff_at_mtu = sfu_eff[sizes.index(1448)] if 1448 in sizes else None
    if eff_at_mtu:
        ax.annotate(f"{eff_at_mtu:.1f}% at MTU",
                    xy=(1448, eff_at_mtu),
                    xytext=(500, eff_at_mtu - 12),
                    color=C_ORG, fontsize=9,
                    arrowprops=dict(arrowstyle="->", color=C_ORG))
    ax.set_xlabel("Payload Size (bytes) — log scale")
    ax.set_ylabel("Wire Efficiency (payload / total wire bytes × 100%)")
    ax.set_title("SFU Wire Efficiency vs Payload Size")
    ax.legend()
    ax.set_ylim(0, 100)
    watermark(ax)

    fig.suptitle("Protocol Overhead & Wire Efficiency Analysis", fontsize=14,
                 fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "11_protocol_overhead_efficiency.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 12 — Box Plots of Raw Latency
# ══════════════════════════════════════════════════════════════════════════
def plot_boxplots():
    udp = load("bench1_udp_raw_latency.json")
    tcp = load("bench2_tcp_latency.json")
    sfu = load("bench3_sfu_latency.json")

    # 256B and 1024B payload comparisons
    payload_keys = ["64", "256", "1024", "1448"]
    actual_keys  = [k for k in payload_keys if k in udp and k in tcp and k in sfu]

    fig, axes = plt.subplots(1, len(actual_keys), figsize=(4 * len(actual_keys), 6),
                              sharey=False)
    if len(actual_keys) == 1:
        axes = [axes]

    for ax, key in zip(axes, actual_keys):
        data_udp = udp[key]["raw"]
        data_tcp = tcp[key]["raw"]
        data_sfu = sfu[key]["raw"]

        bp = ax.boxplot(
            [data_udp, data_tcp, data_sfu],
            labels=["UDP", "TCP", "SFU"],
            patch_artist=True,
            medianprops=dict(color="#e6edf3", linewidth=2),
            whiskerprops=dict(color="#8b949e"),
            capprops=dict(color="#8b949e"),
            flierprops=dict(marker=".", markersize=3, color="#8b949e", alpha=0.4),
            boxprops=dict(linewidth=1.2),
        )
        colors_bp = [C_UDP, C_TCP, C_SFU]
        for patch, color in zip(bp["boxes"], colors_bp):
            patch.set_facecolor(color)
            patch.set_alpha(0.7)

        ax.set_title(f"{key}B Payload")
        ax.set_ylabel("RTT (ms)" if key == actual_keys[0] else "")
        ax.yaxis.set_major_formatter(FuncFormatter(ms_fmt))
        watermark(ax)

    fig.suptitle("Latency Distribution — Box Plots (All Protocols, Multiple Payload Sizes)",
                 fontsize=13, fontweight="bold", y=1.02)
    fig.tight_layout()
    savefig(fig, "12_latency_boxplots.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 13 — Heatmap: P99 across Protocols × Payload Sizes
# ══════════════════════════════════════════════════════════════════════════
def plot_heatmap():
    udp = load("bench1_udp_raw_latency.json")
    tcp = load("bench2_tcp_latency.json")
    sfu = load("bench3_sfu_latency.json")

    keys   = sorted(int(k) for k in udp if k in tcp and k in sfu)
    matrix = np.array([
        [udp[str(k)]["p99_ms"] for k in keys],
        [tcp[str(k)]["p99_ms"] for k in keys],
        [sfu[str(k)]["p99_ms"] for k in keys],
    ])

    fig, ax = plt.subplots(figsize=(11, 4))
    im = ax.imshow(matrix, aspect="auto", cmap="YlOrRd")
    ax.set_xticks(range(len(keys)))
    ax.set_xticklabels([str(k) for k in keys])
    ax.set_yticks([0, 1, 2])
    ax.set_yticklabels(["UDP (Raw)", "TCP", "SFU/UDP"])
    ax.set_xlabel("Payload Size (bytes)")
    ax.set_title("P99 Latency Heatmap — All Protocols × Payload Sizes (ms)")
    cbar = fig.colorbar(im, ax=ax, shrink=0.8)
    cbar.set_label("P99 RTT (ms)", color="#e6edf3")
    cbar.ax.yaxis.set_tick_params(color="#8b949e")
    plt.setp(cbar.ax.yaxis.get_ticklabels(), color="#e6edf3")

    # Annotate cells
    for i in range(matrix.shape[0]):
        for j in range(matrix.shape[1]):
            ax.text(j, i, f"{matrix[i,j]:.3f}", ha="center", va="center",
                    fontsize=8.5, fontweight="bold",
                    color="black" if matrix[i,j] > matrix.max() * 0.5 else "#e6edf3")

    fig.tight_layout()
    savefig(fig, "13_p99_heatmap.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 14 — SFU PING Latency Timeline (ping-like plot)
# ══════════════════════════════════════════════════════════════════════════
def plot_sfu_ping_timeline():
    d   = load("bench3_sfu_latency.json")
    key = "64"  # 64-byte for clarity
    raw = d[key]["raw"][:200]  # first 200 samples

    fig, axes = plt.subplots(2, 1, figsize=(14, 8), sharex=False)

    ax = axes[0]
    ax.plot(range(len(raw)), raw, color=C_SFU, lw=0.9, alpha=0.85, label="SFU RTT")
    ax.axhline(np.mean(raw), color=C_ORG, ls="--", lw=1.5,
               label=f"Mean: {np.mean(raw):.3f} ms")
    ax.axhline(np.percentile(raw, 99), color=C_TCP, ls=":", lw=1.2,
               label=f"P99: {np.percentile(raw, 99):.3f} ms")
    ax.fill_between(range(len(raw)), raw, np.mean(raw),
                    where=[r > np.mean(raw) for r in raw],
                    alpha=0.15, color=C_TCP)
    ax.fill_between(range(len(raw)), raw, np.mean(raw),
                    where=[r <= np.mean(raw) for r in raw],
                    alpha=0.1, color=C_SFU)
    ax.set_ylabel("RTT (ms)")
    ax.set_title("SFU/UDP Packet-by-Packet Latency Timeline (64-byte payload, first 200 samples)\n"
                 "Models VirtIO-Net loopback path through QEMU SLIRP stack")
    ax.legend()
    watermark(ax)

    # Comparison: all 3 protocols at 256B side by side on timeline
    udp = load("bench1_udp_raw_latency.json")
    tcp = load("bench2_tcp_latency.json")
    sfu = load("bench3_sfu_latency.json")
    K   = "256"
    n   = 150
    ax2 = axes[1]
    ax2.plot(range(n), udp[K]["raw"][:n], color=C_UDP, lw=0.9, alpha=0.9, label="UDP Raw")
    ax2.plot(range(n), tcp[K]["raw"][:n], color=C_TCP, lw=0.9, alpha=0.9, label="TCP")
    ax2.plot(range(n), sfu[K]["raw"][:n], color=C_SFU, lw=0.9, alpha=0.9, label="SFU/UDP")
    ax2.set_xlabel("Sample #")
    ax2.set_ylabel("RTT (ms)")
    ax2.set_title("Per-Sample RTT Timeline — All Protocols (256-byte payload, 150 samples)")
    ax2.legend()
    watermark(ax2)

    fig.tight_layout()
    savefig(fig, "14_latency_timeline.png")


# ══════════════════════════════════════════════════════════════════════════
# PLOT 15 — Master Dashboard (2×3 overview)
# ══════════════════════════════════════════════════════════════════════════
def plot_dashboard():
    udp_d = load("bench1_udp_raw_latency.json")
    tcp_d = load("bench2_tcp_latency.json")
    sfu_d = load("bench3_sfu_latency.json")
    crc_d = load("bench4_crc16_throughput.json")
    inf_d = load("bench10_e2e_inference.json")
    ovh_d = load("bench9_protocol_overhead.json")

    sizes  = sorted(int(k) for k in udp_d if k in tcp_d and k in sfu_d)
    c_sizes= sorted(int(k) for k in crc_d)
    inf_k  = list(inf_d.keys())
    ovh_s  = sorted(int(k) for k in ovh_d)

    fig = plt.figure(figsize=(20, 12))
    gs  = gridspec.GridSpec(2, 3, figure=fig, hspace=0.45, wspace=0.35)

    # ── Panel A: Mean latency ─────────────────────────────────
    ax = fig.add_subplot(gs[0, 0])
    ax.plot(sizes, [udp_d[str(s)]["mean_ms"] for s in sizes], "o-", color=C_UDP, label="UDP")
    ax.plot(sizes, [tcp_d[str(s)]["mean_ms"] for s in sizes], "s-", color=C_TCP, label="TCP")
    ax.plot(sizes, [sfu_d[str(s)]["mean_ms"] for s in sizes], "^-", color=C_SFU, label="SFU")
    ax.set_title("A. Mean RTT vs Payload", fontweight="bold")
    ax.set_xlabel("Payload (B)")
    ax.set_ylabel("ms")
    ax.legend(fontsize=8)
    ax.set_xticks(sizes[::2])

    # ── Panel B: P99 comparison ───────────────────────────────
    ax = fig.add_subplot(gs[0, 1])
    x  = np.arange(len(sizes))
    w  = 0.28
    ax.bar(x - w, [udp_d[str(s)]["p99_ms"] for s in sizes], w, color=C_UDP, label="UDP")
    ax.bar(x,     [tcp_d[str(s)]["p99_ms"] for s in sizes], w, color=C_TCP, label="TCP")
    ax.bar(x + w, [sfu_d[str(s)]["p99_ms"] for s in sizes], w, color=C_SFU, label="SFU")
    ax.set_title("B. P99 Tail Latency", fontweight="bold")
    ax.set_xlabel("Payload (B)")
    ax.set_ylabel("ms")
    ax.set_xticks(x)
    ax.set_xticklabels([str(s) for s in sizes], rotation=30, ha="right", fontsize=8)
    ax.legend(fontsize=8)

    # ── Panel C: CRC throughput ───────────────────────────────
    ax = fig.add_subplot(gs[0, 2])
    mbs = [crc_d[str(s)]["throughput_mbs"] for s in c_sizes]
    ax.semilogx(c_sizes, mbs, "o-", color=C_CRC)
    ax.fill_between(c_sizes, mbs, alpha=0.2, color=C_CRC)
    ax.axvline(1448, color=C_SFU, ls="--", lw=1, label="SFU MTU")
    ax.set_title("C. CRC16 Throughput", fontweight="bold")
    ax.set_xlabel("Size (B) — log")
    ax.set_ylabel("MB/s")
    ax.legend(fontsize=8)

    # ── Panel D: E2E inference RTT ────────────────────────────
    ax = fig.add_subplot(gs[1, 0])
    means = [inf_d[k]["mean_ms"] for k in inf_k]
    p99s  = [inf_d[k]["p99_ms"]  for k in inf_k]
    x2    = np.arange(len(inf_k))
    ax.bar(x2 - 0.2, means, 0.4, color=C_SFU, label="Mean")
    ax.bar(x2 + 0.2, p99s,  0.4, color=C_ORG, label="P99")
    ax.set_title("D. E2E Inference RTT", fontweight="bold")
    ax.set_xlabel("Model")
    ax.set_ylabel("ms")
    ax.set_xticks(x2)
    ax.set_xticklabels(inf_k, rotation=20, ha="right", fontsize=8)
    ax.legend(fontsize=8)

    # ── Panel E: Wire efficiency ──────────────────────────────
    ax = fig.add_subplot(gs[1, 1])
    eff = [ovh_d[str(s)]["sfu_efficiency_pct"] for s in ovh_s]
    ax.semilogx(ovh_s, eff, "o-", color=C_SFU, lw=2)
    ax.fill_between(ovh_s, eff, alpha=0.2, color=C_SFU)
    ax.axhline(90, color=C_ORG, ls="--", lw=1, label="90% target")
    ax.set_title("E. SFU Wire Efficiency", fontweight="bold")
    ax.set_xlabel("Payload (B) — log")
    ax.set_ylabel("Efficiency (%)")
    ax.set_ylim(0, 100)
    ax.legend(fontsize=8)

    # ── Panel F: Jitter CV ────────────────────────────────────
    jitter_d = load("bench6_jitter_under_load.json")
    bursts = sorted(int(k) for k in jitter_d["udp"] if k in jitter_d["sfu"])
    ax = fig.add_subplot(gs[1, 2])
    ax.plot(bursts, [jitter_d["udp"][str(b)]["jitter_cv"] for b in bursts],
            "o-", color=C_UDP, label="UDP")
    ax.plot(bursts, [jitter_d["sfu"][str(b)]["jitter_cv"] for b in bursts],
            "s-", color=C_SFU, label="SFU")
    ax.axhline(15, color=C_TCP, ls="--", lw=1, label="15% limit")
    ax.set_title("F. Jitter under Burst Load", fontweight="bold")
    ax.set_xlabel("Burst size (pkts)")
    ax.set_ylabel("Jitter CV (%)")
    ax.legend(fontsize=8)

    fig.suptitle(
        "MiniOS Network Protocol Benchmark — Overview Dashboard\n"
        "SFU (Simple Framed UDP) vs Raw UDP vs TCP   ·   Loopback 127.0.0.1",
        fontsize=15, fontweight="bold",
    )
    savefig(fig, "00_dashboard_overview.png")


# ══════════════════════════════════════════════════════════════════════════
# MAIN
# ══════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    print("=" * 60)
    print("  MiniOS Network Benchmark — Plot Generator")
    print("=" * 60)

    tasks = [
        ("01 UDP Latency vs Size",              plot_udp_latency),
        ("02 Protocol Comparison (Mean RTT)",   plot_protocol_comparison),
        ("03 P99 & Jitter Comparison",          plot_p99_comparison),
        ("04 CRC16 Throughput",                 plot_crc16_throughput),
        ("05 SFU Serialize Overhead",           plot_sfu_serialize),
        ("06 Jitter under Burst Load",          plot_jitter_burst),
        ("07 Latency CDF + KDE",                plot_latency_cdf),
        ("08 In-Flight Table Scan",             plot_inflight_table),
        ("09 Tensor Serialization",             plot_tensor_serialization),
        ("10 E2E Inference RTT",                plot_e2e_inference),
        ("11 Protocol Overhead & Efficiency",   plot_protocol_overhead),
        ("12 Box Plots",                        plot_boxplots),
        ("13 P99 Heatmap",                      plot_heatmap),
        ("14 Latency Timeline",                 plot_sfu_ping_timeline),
        ("15 Dashboard Overview",               plot_dashboard),
    ]

    failed = []
    for label, fn in tasks:
        try:
            print(f"\n▶  Generating: {label}")
            fn()
        except Exception as e:
            print(f"  ✗  FAILED: {e}")
            failed.append((label, str(e)))

    print("\n" + "=" * 60)
    if failed:
        print(f"  ⚠  {len(failed)} plot(s) failed:")
        for lbl, err in failed:
            print(f"     • {lbl}: {err}")
    else:
        print(f"  ✅  All {len(tasks)} plots generated in network_benchmark/plots/")
    print("=" * 60)
