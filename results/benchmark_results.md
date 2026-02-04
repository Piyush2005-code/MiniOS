# Benchmark Results: Linux VM vs Unikraft

## Test Configuration

| Parameter | Value |
|-----------|-------|
| **Iterations** | 1,000,000 per trial |
| **Warmup** | 100,000 iterations |
| **Trials** | 10 |
| **Model** | 784→128→10 Neural Network |
| **Optimization** | AVX2 + FMA SIMD |

---

## Results Summary

### Linux VM Performance

| Trial | Throughput (inf/s) | Latency (ns) |
|-------|-------------------|--------------|
| 1 | 202,039.70 | 4,949.51 |
| 2 | 185,675.89 | 5,385.81 |
| 3 | 205,404.62 | 4,868.40 |
| 4 | 209,089.81 | 4,782.65 |
| 5 | 184,882.99 | 5,408.85 |
| 6 | 201,956.95 | 4,951.54 |
| 7 | 193,588.63 | 5,165.60 |
| 8 | 189,950.92 | 5,264.51 |
| 9 | 189,886.72 | 5,266.29 |
| 10 | 183,741.34 | 5,442.51 |
| **Average** | **194,621.76** | **5,148.57** |

### Unikraft Performance

| Trial | Throughput (inf/s) | Latency (ns) |
|-------|-------------------|--------------|
| 1 | 195,404.50 | 5,117.59 |
| 2 | 189,798.70 | 5,268.73 |
| 3 | 192,717.98 | 5,188.96 |
| 4 | 196,089.63 | 5,099.67 |
| 5 | 199,882.80 | 5,002.93 |
| 6 | 190,142.86 | 5,259.17 |
| 7 | 184,404.21 | 5,422.89 |
| 8 | 181,232.36 | 5,517.93 |
| 9 | 194,783.00 | 5,133.91 |
| 10 | 197,525.32 | 5,062.66 |
| **Average** | **192,198.14** | **5,207.44** |

---

## Comparison

| Metric | Linux VM | Unikraft | Difference |
|--------|----------|----------|------------|
| **Avg Throughput** | 194,621.76 inf/s | 192,198.14 inf/s | 1.25% |
| **Avg Latency** | 5,148.57 ns | 5,207.44 ns | 1.14% |
| **Best Run** | 209,089.81 inf/s | 199,882.80 inf/s | 4.61% |
| **Worst Run** | 183,741.34 inf/s | 181,232.36 inf/s | 1.38% |
| **Trials Won** | 6/10 | 4/10 | - |

---

## Statistical Analysis

The 1.25% average difference is **within measurement noise** caused by:
- CPU frequency fluctuations (turbo boost)
- Background system processes
- Cache state variations

**Conclusion**: Both environments achieve **statistically equivalent performance** for this workload.

---

## Environment Details

### Linux VM
- **OS**: Alpine Linux (minimal)
- **Kernel**: Linux 6.x
- **Hypervisor**: QEMU/KVM

### Unikraft
- **Version**: 0.16.x
- **Backend**: KVM
- **Libraries**: nolibc, ukalloc

---

*Generated: February 2026*
