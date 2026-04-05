# Formal Mathematical Analysis & Benchmark of MiniOS Network Protocols

## 1. Introduction and Architectural Assumptions

In the context of the MiniOS ARM64 Unikernel, standard network protocols interact poorly with the strict bounding constraints of deterministic machine learning inference. To objectively validate the design of the **Simple Framed UDP (SFU)** protocol against **Raw UDP** and **TCP**, we conducted exactly 10 comprehensive benchmarking suites. 

### 1.1 Mathematical Pre-requisites & Assumptions
Our benchmarks are predicated on the following formal mathematical and architectural assumptions:
1. **Time-Invariance:** The underlying loopback network channel exhibits a stationary noise process over the measurement window $[0, T]$.
2. **Coefficient of Variation (Jitter):** We formalize execution jitter as the dimensionless Coefficient of Variation ($CV$), where $\mu$ is the expected sample mean and $\sigma$ is the sample standard deviation. The threshold for deterministic execution bounds per SRS PDR-001 is bounded at $J_{max} = 0.15$ (15%).
   $$ CV = \frac{\sigma}{\mu} = \frac{\sqrt{\frac{1}{N}\sum_{i=1}^{N}(x_i - \mu)^2}}{\frac{1}{N}\sum_{i=1}^{N}x_i} $$
3. **Throughput Equivalence:** The protocol processing throughput $T_B$ in Bytes/sec is the inverse of the scalar execution time per packet length function $\tau(P)$, scaled by the clock frequency $f_{clk}$.
   $$ T_B(P) = \frac{P}{\tau(P)} = \lim_{\Delta t \to 0} \frac{\Delta V_{data}}{\Delta t} $$

---

## 2. Dashboard Analytics

As an overview, the 6-panel composite dashboard summarizes the fundamental trade-offs among the protocols concerning latency, variation, and wire efficiency.

![Dashboard Overview](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/00_dashboard_overview.png)

---

## 3. Stochastic Latency Profiling

For $N = 500$ distinct queries per payload length $P \in \{8, 64, 256, 1024, 1448\}$, we sample the random variable $\mathbf{L}_{proto}(P)$ representing protocol latency.

### 3.1 Expected Latency $\mathbb{E}[\mathbf{L}]$

The mean deterministic offset incurred by SFU's checksums scales linearly with the set payload byte-length $P$.

![UDP Latency vs Size](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/01_udp_latency_vs_size.png)
![TCP vs UDP vs SFU Mean](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/02_tcp_vs_udp_vs_sfu_mean.png)

From the graphs, we construct an approximate empirical latency prediction function for SFU:
$$ \mathbb{E}[\mathbf{L}_{SFU}(P)] \approx \mathbb{E}[\mathbf{L}_{UDP}(P)] + k_{CRC} \cdot P + c_{hdr} $$
Where the checksum proportionality constant $k_{CRC}$ dominates upper payload ranges up to the network Maximum Transmission Unit ($MTU = 1448$).

### 3.2 Variance and High-Quantile Bounds ($P_{99}$)

The 99th percentile constraint is given by the cumulative distribution function $F$:
$$ F_{\mathbf{L}}(t) = \int_{0}^{t} f_{\mathbf{L}}(x)dx \implies F_{\mathbf{L}}^{-1}(0.99) = \mathbf{L}_{p99} $$

By plotting empirical densities via Gaussian Kernel Density Estimation (KDE) and Box-and-Whisker metrics, the variance of TCP becomes starkly apparent when handling fragmented sliding windows.

![Latency CDF + KDE](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/07_latency_cdf_kde.png)
![Box Plots](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/12_latency_boxplots.png)
![Heatmap P99](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/13_p99_heatmap.png)
![P99 & Jitter](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/03_p99_and_jitter_comparison.png)

---

## 4. Stability Analysis Under Burst Traffic (Jitter)

The temporal dispersion of context-switching jitter, when a sequence of $m$ concurrent packets are burst onto the wire, dictates the actual blocking interference the ML `ONNX_ExecuteOperator()` functions will face.

![Jitter under Burst Load](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/06_jitter_burst_analysis.png)
![Latency Timeline](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/14_latency_timeline.png)

SFU demonstrates superior strict-variance clustering, absorbing burst deltas predictably without the cascading timing violations seen in TCP congestion algorithms.

---

## 5. Security & Validity Header Mathematics

### 5.1 Protocol Wire Efficiency Function

Given fixed lower-level L2/L3 headers ($H_{Eth} = 14, H_{IPv4} = 20, H_{UDP} = 8$), we define overhead ratio function $\Omega$:
$$ \Omega(P, H_{spec}) = \frac{H_{Eth} + H_{IPv4} + H_{UDP} + H_{spec}}{H_{Eth} + H_{IPv4} + H_{UDP} + H_{spec} + P} \times 100\% $$

SFU utilizes a custom $H_{SFU} = 24$ Byte header. Its wire efficiency maps directly inversely to payload lengths.

![SFU Serialize Overhead](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/05_sfu_serialize_overhead.png)
![Protocol Efficiency](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/11_protocol_overhead_efficiency.png)

As $\lim_{P \to 1448} \Omega_{SFU}(P)$, the protocol rapidly achieves transmission asymptotic efficiency of $>95\%$, an acceptable penalty for achieving $O(1)$ memory allocations.

### 5.2 CRC16-CCITT Data Integrity Throughput

The computation polynomial applied is $G(x) = x^{16} + x^{12} + x^5 + 1$. The discrete throughput performance curves match predicted theoretical thresholds.

![CRC16 Throughput](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/04_crc16_throughput.png)

---

## 6. End-to-End ONNX Emulation and $O(N)$ Lookups

### 6.1 In-Flight Table Complexity

The constraint logic of SFU implements a cyclic array lookup for reliability, executing in linear sequence $O(N)$ instead of deploying an unconstrained hashed map representation ($\mathbb{R}^n \mapsto \mathbb{Z}$).

$$ \tau_{scan}(N) = \alpha N + \tau_{cache\_miss} $$

![In-Flight Table Scan](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/08_inflight_table_scan.png)

### 6.2 Tensor Structure Iteration 

Converting higher-order multidimensional metric tensors $X \in \mathbb{R}^{B \times C \times W \times H}$ into contiguous binary byte-frames for transit determines actual inference operational bandwidth.

$$ \mathbf{ByteLength}(X) = B \cdot C \cdot W \cdot H \cdot \mathbf{sizeof}(FP32) = n_{elements} \times 4 $$

![Tensor Serialization](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/09_tensor_serialization.png)
![E2E Inference RTT](/Users/piyushsinghbhati/Documents/Programming/MiniOS/network_benchmark/plots/10_e2e_inference_rtt.png)

These serialization timings mathematically confirm that smaller ML models (`tiny_mlp`, `simple_add`) clear transmission delays safely beneath $\mathbf{T}_{threshold} = 3ms$, satisfying continuous interactive inference targets.

---

## Conclusion 

Derived directly from formal latency distributions, variation ratios, polynomial throughput bounds, and continuous byte framing costs, **Simple Framed UDP (SFU)** unequivocally maintains the strict requirements for hard $O(1)$ predictable systems modeling and zero-state dynamism (`malloc()` safety bounds). 
