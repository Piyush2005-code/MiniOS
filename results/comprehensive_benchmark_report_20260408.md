# Comprehensive Benchmark Report: MiniOS vs Debian 12 on ARM64 QEMU

## 1. Executive Summary

This report documents a fresh benchmark study comparing MiniOS against a Linux baseline for three ONNX models: `mnist`, `squeezenet`, and `shufflenet`. The Linux baseline used for the completed study was Debian 12 genericcloud on `arm64`, chosen because it is a stable, relatively lean, production-relevant Linux image that boots cleanly in the same virtualized environment.

The benchmark outcome is consistent across all three models: MiniOS is faster in mean latency, faster in p95 latency, dramatically faster in model load time, and reports a much smaller working-memory footprint under the benchmark harness. The strongest wins are in initialization cost and runtime regularity, which match the design goals visible in the MiniOS codebase: preallocated arena-backed tensor memory, precomputed execution schedules, cooperative single-core scheduling, and a benchmark path that removes avoidable runtime noise.

## 2. Study Goal and Relevance

The goal of the study was not to prove that MiniOS beats every Linux configuration on every hardware target. The goal was narrower and more relevant to the MiniOS design constraints:

- compare both systems under the same single-vCPU ARM64 QEMU envelope,
- run the same logical models and fixed inputs,
- keep inference single-threaded,
- separate model loading from timed inference,
- run enough iterations to estimate both central tendency and tail latency,
- measure a memory-related footprint metric from each harness,
- explain the observed differences using the actual runtime and scheduler implementation.

This framing matters because MiniOS is explicitly engineered for deterministic, low-overhead ML inference on a small, controlled execution substrate rather than for general-purpose multitasking. The benchmark therefore needs to reflect the constraints that MiniOS is designed for, not the constraints of a desktop or multicore server runtime.

## 3. Workload, Environment, and Benchmark Protocol

### 3.1 Models and iteration counts

- `mnist`
- `squeezenet`
- `shufflenet`
- exactly `50` timed runs per model per OS
- exactly `1` warmup run per model per OS

### 3.2 Execution envelope

- architecture: `aarch64`
- virtualization: QEMU `virt`
- CPU shape: single vCPU
- MiniOS path: native MiniOS benchmark command `/bench/run_bench`
- Linux path: ONNX Runtime CPUExecutionProvider with single-thread session settings

### 3.3 Validated result files

- [mnist_minios_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/mnist_minios_50.json)
- [squeezenet_minios_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/squeezenet_minios_50.json)
- [shufflenet_minios_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/shufflenet_minios_50.json)
- [mnist_debian_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/mnist_debian_50.json)
- [squeezenet_debian_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/squeezenet_debian_50.json)
- [shufflenet_debian_50.json](/home/Piyush/Documents/VSCode_ws/MiniOS/results/shufflenet_debian_50.json)

### 3.4 Validation conditions

Every output file was checked for:

- file existence,
- `runs == 50`,
- `warmup == 1`,
- `len(latencies_ms) == 50`,
- numeric `model_load_ms`,
- numeric `peak_rss_kb`.

## 4. Mathematical Assumptions and Statistical Definitions

This section makes the benchmark mathematics explicit so the conclusions stay tied to the actual experiment rather than to intuition.

### 4.1 Sample model

For each model and operating system, let the timed latencies be:

`L = {l1, l2, ..., l50}`

where each `li` is the latency in milliseconds of one timed inference pass after one warmup pass.

The benchmark treats these 50 timed runs as repeated observations from a controlled execution environment with fixed model, fixed input, fixed single-core virtual machine shape, and a stable harness configuration.

### 4.2 Mean latency

The arithmetic mean is:

`mean(L) = (1 / 50) * sum(li for i in 1..50)`

The mean is useful because it estimates average throughput cost per inference in the steady timed region.

### 4.3 p95 latency

Let `S` be the sorted latency list in ascending order. For `n = 50`, this report uses:

`p95(L) = S[ceil(0.95 * n)] = S[48]` in 1-based indexing

or index `47` in 0-based indexing.

This is the same sample p95 rule used during result processing: the 48th sorted observation is the p95 estimator for a 50-sample run. p95 matters because real systems care about upper-tail latency, not just the mean.

### 4.4 Standard deviation and coefficient of variation

To comment on stability, this report also uses:

`std(L) = sqrt((1 / n) * sum((li - mean(L))^2))`

`cv(L) = std(L) / mean(L)`

The coefficient of variation is unitless and helps compare stability across models with very different latency scales.

### 4.5 Speedup

For a given model, MiniOS mean-latency speedup over Debian is:

`speedup_mean = mean_debian / mean_minios`

Values greater than `1.0` mean MiniOS is faster.

The same shape is used for p95:

`speedup_p95 = p95_debian / p95_minios`

### 4.6 Model-load cost

Model load time is treated separately from timed inference:

- MiniOS records load time using [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1023) through [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1185).
- The Linux harness records load time using `time.perf_counter()` around `InferenceSession(...)` in [run_bench.py](/home/Piyush/Documents/VSCode_ws/MiniOS/scripts/ubuntu_bench/run_bench.py#L68).

This separation is important because MiniOS wins very strongly here, and the reason is architectural rather than accidental.

### 4.7 Memory metric assumption and limitation

The memory field is named `peak_rss_kb` in both JSON schemas, but the meanings are not identical:

- MiniOS writes `KMEM_ArenaGetUsed(g_tensor_arena) / 1024` in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1251) and emits it as `peak_rss_kb` in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1316).
- The Linux harness uses `resource.getrusage(...).ru_maxrss` in [run_bench.py](/home/Piyush/Documents/VSCode_ws/MiniOS/scripts/ubuntu_bench/run_bench.py#L102).

Therefore the memory comparison is directionally informative but not perfectly apples-to-apples. MiniOS is reporting tensor-arena usage, while Linux is reporting process maximum resident set size. This report treats the memory result as a footprint indicator, not as a strictly identical metric.

## 5. Numerical Results

### 5.1 Primary comparison table

| Model | OS | Mean latency (ms) | P95 latency (ms) | Model load (ms) | Reported memory (KB) |
| --- | --- | ---: | ---: | ---: | ---: |
| mnist | MiniOS | 4.948 | 5.200 | 9.000 | 27 |
| mnist | Debian | 9.060 | 9.714 | 453.149 | 48700 |
| squeezenet | MiniOS | 2508.436 | 2562.000 | 10.000 | 5416 |
| squeezenet | Debian | 3045.485 | 3224.312 | 322.023 | 62792 |
| shufflenet | MiniOS | 1046.990 | 1055.100 | 27.000 | 30085 |
| shufflenet | Debian | 1308.505 | 1325.061 | 710.046 | 73864 |

### 5.2 Derived comparison values

| Model | Mean speedup (MiniOS vs Debian) | P95 speedup | Debian/MiniOS load ratio | Debian/MiniOS memory ratio |
| --- | ---: | ---: | ---: | ---: |
| mnist | 1.83x | 1.87x | 50.35x | 1803.70x |
| squeezenet | 1.21x | 1.26x | 32.20x | 11.59x |
| shufflenet | 1.25x | 1.26x | 26.30x | 2.46x |

### 5.3 Stability indicators

| Model | OS | Std. dev. (ms) | Coefficient of variation |
| --- | --- | ---: | ---: |
| mnist | MiniOS | 0.187 | 0.0378 |
| mnist | Debian | 0.285 | 0.0315 |
| squeezenet | MiniOS | 25.712 | 0.0103 |
| squeezenet | Debian | 141.558 | 0.0465 |
| shufflenet | MiniOS | 10.792 | 0.0103 |
| shufflenet | Debian | 79.005 | 0.0604 |

Interpretation:

- MiniOS is faster on every model in both mean and p95.
- MiniOS is dramatically faster in model initialization.
- For the larger CNNs, MiniOS is also materially more stable over repeated runs.
- The `mnist` stability story is more nuanced: both systems are quite stable in absolute terms, although MiniOS is still clearly faster.

## 6. Visual Results

### 6.1 Mean latency

![Mean latency comparison](graphs/benchmark_mean_latency.jpg)

### 6.2 P95 latency

![P95 latency comparison](graphs/benchmark_p95_latency.jpg)

### 6.3 Model load time

![Model load comparison](graphs/benchmark_model_load.jpg)

### 6.4 Reported memory footprint

![Memory comparison](graphs/benchmark_memory.jpg)

### 6.5 Overall speedup

![Speedup comparison](graphs/benchmark_speedup.jpg)

### 6.6 Per-iteration stability

![Stability comparison](graphs/benchmark_stability.jpg)

## 7. Why This Benchmark Is Relevant to MiniOS Constraints

MiniOS is not trying to be a general-purpose operating system. Its value proposition is much closer to: "run fixed ML workloads with low system overhead, predictable memory use, and controlled execution behavior." The benchmark aligns with that proposition in several concrete ways.

### 7.1 Single-core relevance

The scheduler is explicitly documented as cooperative, priority-based, and single-core in [thread.h](/home/Piyush/Documents/VSCode_ws/MiniOS/include/kernel/thread.h#L5). That makes a single-vCPU benchmark the right fairness target. A multicore Linux server benchmark would answer a different question.

### 7.2 Inference-centric memory design

The memory subsystem is explicitly optimized for ML inference: permanent bump allocation, resettable tensor arenas, fixed-size pools, and 64-byte cache-line alignment for ARM64 in [kmem.h](/home/Piyush/Documents/VSCode_ws/MiniOS/include/kernel/kmem.h#L5). A benchmark based on repeated inference of fixed models is exactly the kind of workload that should expose the value of that design.

### 7.3 Runtime determinism relevance

The runtime architecture document states that the inference path avoids an external OS, avoids `libc`, and avoids dynamic heap allocation in the hot inference path in [ONNX_RUNTIME_ARCHITECTURE.md](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/ONNX_RUNTIME_ARCHITECTURE.md#L5). It also states that the graph is scheduled ahead of time and then executed by walking the precomputed `exec_schedule`, giving O(1) schedule traversal overhead during inference in [ONNX_RUNTIME_ARCHITECTURE.md](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/ONNX_RUNTIME_ARCHITECTURE.md#L13).

That is exactly the kind of architecture that should benefit from repeated, same-input inference benchmarks.

### 7.4 Tail-latency relevance

MiniOS is not only about average speed. Its cooperative scheduler and benchmark tuning are meant to reduce avoidable jitter. That is why p95 latency belongs in the benchmark. For `squeezenet` and `shufflenet`, MiniOS not only lowers the mean but also tightens the tail.

## 8. Codebase Analysis: Why MiniOS Performs So Well Here

The performance gap is not magic. The codebase reveals several very specific reasons.

### 8.1 Preallocated and resettable tensor memory

MiniOS allocates a tensor arena once and then resets it in O(1) via [kmem.h](/home/Piyush/Documents/VSCode_ws/MiniOS/include/kernel/kmem.h#L121). In the benchmark command, the arena is reused and reset before the run in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1020). This avoids allocator churn across repeated inferences.

That design is especially relevant for models with many intermediate tensors because repeated heap activity is one of the easiest ways to introduce noise and overhead.

### 8.2 Precomputed execution schedule

The runtime architecture states that the graph is topologically scheduled once and then executed as a linear schedule in [ONNX_RUNTIME_ARCHITECTURE.md](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/ONNX_RUNTIME_ARCHITECTURE.md#L13). The benchmark command ensures the schedule exists before timed inference in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1140).

This matters because the timed loop in MiniOS is then mostly paying for kernel execution and fixed dispatch rather than for repeated graph-planning work.

### 8.3 Warmup is used to remove one-time output preparation costs

The benchmark path enables output preparation for warmup, then disables repeated output-preparation checks for the timed loop in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1219). The runtime side shows what those checks do: infer placeholder output shapes and allocate missing output tensors in [onnx_runtime.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_runtime.c#L3081).

This is a strong design choice for benchmarking steady-state inference. It means the timed runs are much closer to "pure repeated execution" than to "execution plus repeated setup."

### 8.4 Cooperative scheduling noise is explicitly suppressed

By default, the runtime can yield between nodes in [onnx_runtime.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_runtime.c#L3121). The benchmark command disables that path in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1201), disables telemetry in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1204), disables SFU ticks in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1205), and enables FELIX scheduler mode in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1206). FELIX itself is documented as a fast voluntary-yield optimization in [thread.h](/home/Piyush/Documents/VSCode_ws/MiniOS/include/kernel/thread.h#L236).

This cluster of choices is a major reason MiniOS performs so smoothly in the timed section. The benchmark is intentionally measuring the inference engine with surrounding OS noise minimized.

### 8.5 Interrupt suppression during the timed section

MiniOS disables interrupts around the benchmark region using `arch_irq_save()` in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1209) and restores them after the timed loop in [onnx_cmds.c](/home/Piyush/Documents/VSCode_ws/MiniOS/src/onnx/onnx_cmds.c#L1279).

This is highly relevant to the tail-latency result. If a system is allowed to take timer or network activity during the measurement interval, p95 can widen substantially. MiniOS is explicitly configured to prevent that during the benchmark.

### 8.6 Lightweight single-address-space execution

The scheduler documentation emphasizes a single-address-space cooperative model in [thread.h](/home/Piyush/Documents/VSCode_ws/MiniOS/include/kernel/thread.h#L9). That reduces the amount of kernel mediation needed to keep the benchmark loop running. In contrast, the Linux-side benchmark still executes inside a general-purpose OS process with Python, ONNX Runtime, and standard Linux process accounting.

This does not make Linux "bad"; it simply means Linux is solving a more general problem than MiniOS in this experiment.

### 8.7 Linux-side harness still carries process and runtime overhead

The Linux benchmark harness uses Python, NumPy, and ONNX Runtime in [run_bench.py](/home/Piyush/Documents/VSCode_ws/MiniOS/scripts/ubuntu_bench/run_bench.py#L29). It configures single-thread execution in [run_bench.py](/home/Piyush/Documents/VSCode_ws/MiniOS/scripts/ubuntu_bench/run_bench.py#L68), which is good and makes the comparison fairer, but it still has a heavier load path because session construction is more expensive than MiniOS's embedded model path.

That is almost certainly why `model_load_ms` is where MiniOS wins most dramatically.

## 9. What We Should and Should Not Conclude

### 9.1 Conclusions that are well supported

- MiniOS is faster than the Debian baseline for all three tested models under this single-core ARM64 QEMU setup.
- MiniOS has materially lower p95 latency for the larger CNN models.
- MiniOS has much lower measured model-load overhead.
- MiniOS's execution path is intentionally engineered to reduce benchmark noise, and the code clearly shows how.

### 9.2 Conclusions that would be too strong

- this study does not prove MiniOS is faster than Linux on every hardware target,
- this study does not prove MiniOS has lower true system RSS in an apples-to-apples sense,
- this study does not prove MiniOS kernels are mathematically more optimized than ONNX Runtime kernels in general,
- this study does not evaluate multicore scaling, throughput under contention, or mixed workloads.

One especially important caution is that some MiniOS gains come from benchmark-tuned conditions such as disabled telemetry, disabled tick activity, disabled yield-between-node behavior, and interrupts being suppressed during the timed section. Those are legitimate for measuring a low-interference inference path, but they should be stated openly.

## 10. Threats to Validity and Fairness Notes

### 10.1 Virtualization effect

All absolute numbers are QEMU-mediated. The safest interpretation is relative performance inside a controlled virtual envelope, not hardware-native deployment speed.

### 10.2 Memory metric mismatch

As explained earlier, `peak_rss_kb` is not defined identically across the two harnesses. MiniOS reports arena usage; Linux reports process RSS.

### 10.3 Linux baseline scope

The Debian baseline is a strong and relevant Linux comparison, but it is still only one Linux configuration. Alpine or a more stripped userspace could reduce some of the Linux-side gap.

### 10.4 Operator implementation differences

The architecture document openly describes parts of the runtime as straightforward and sometimes naive implementations. The observed MiniOS win therefore likely comes more from reduced systems overhead, reduced benchmark noise, and tighter memory handling than from universally superior numerical kernels.

## 11. Bottom-Line Interpretation

The benchmark strongly supports the claim that MiniOS is well matched to repeated, single-core, low-interference inference workloads. The most convincing evidence is not only that MiniOS is faster, but that the shape of the win matches the codebase:

- preallocation and arena reset reduce memory churn,
- precomputed scheduling reduces runtime orchestration work,
- one-time output preparation is paid during warmup and mostly removed from timed passes,
- scheduler and interrupt noise are deliberately minimized during measurement,
- the system stays small and single-purpose compared with a general Linux process stack.

In short, MiniOS performs well here because the benchmark is measuring exactly the sort of constrained execution path the OS and runtime were built to optimize.

## 12. Appendix: Raw Outputs and Figures

### 12.1 Raw benchmark JSON files

- [mnist_minios_50.json](mnist_minios_50.json)
- [squeezenet_minios_50.json](squeezenet_minios_50.json)
- [shufflenet_minios_50.json](shufflenet_minios_50.json)
- [mnist_debian_50.json](mnist_debian_50.json)
- [squeezenet_debian_50.json](squeezenet_debian_50.json)
- [shufflenet_debian_50.json](shufflenet_debian_50.json)

### 12.2 Figure files

- [benchmark_mean_latency.jpg](graphs/benchmark_mean_latency.jpg)
- [benchmark_p95_latency.jpg](graphs/benchmark_p95_latency.jpg)
- [benchmark_model_load.jpg](graphs/benchmark_model_load.jpg)
- [benchmark_memory.jpg](graphs/benchmark_memory.jpg)
- [benchmark_speedup.jpg](graphs/benchmark_speedup.jpg)
- [benchmark_stability.jpg](graphs/benchmark_stability.jpg)
