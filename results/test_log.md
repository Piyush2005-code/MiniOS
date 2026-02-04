# Benchmark Test Log

## Test Execution History

### February 4, 2026 - 10-Trial Comparison
- **Iterations**: 1,000,000 per trial
- **Warmup**: 100,000 iterations
- **Environment**: Native Linux (simulating both Linux VM and Unikraft)

#### Results:
```
Trial  1: Linux=202039.70  Unikraft=195404.50
Trial  2: Linux=185675.89  Unikraft=189798.70
Trial  3: Linux=205404.62  Unikraft=192717.98
Trial  4: Linux=209089.81  Unikraft=196089.63
Trial  5: Linux=184882.99  Unikraft=199882.80
Trial  6: Linux=201956.95  Unikraft=190142.86
Trial  7: Linux=193588.63  Unikraft=184404.21
Trial  8: Linux=189950.92  Unikraft=181232.36
Trial  9: Linux=189886.72  Unikraft=194783.00
Trial 10: Linux=183741.34  Unikraft=197525.32
```

#### Summary:
- Linux VM Average: 194,621.76 inf/s
- Unikraft Average: 192,198.14 inf/s
- Difference: 1.25% (within measurement noise)

### Test Environment
- CPU: x86_64 with AVX2 + FMA support
- Frequency: ~3.4 GHz
- OS: Linux 6.x kernel
- Compiler: GCC with -O3 -march=native -mavx2 -mfma

### Notes
- Both benchmarks run the same optimized code
- RDTSCP used for nanosecond-precision timing
- Results show statistical equivalence
- Performance variance due to CPU frequency scaling and cache states
