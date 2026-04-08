# MiniOS vs Ubuntu — Quick ONNX Inference Benchmark (Small Models)

## What This Benchmark Measures
MiniOS is a custom bare-metal OS with zero Linux overhead — no kernel scheduler, no libc, no virtual memory manager. This quick benchmark uses the 3 smallest available models (mnist, squeezenet, shufflenet) with 50 runs each to get fast directional results before running the full exhaustive benchmark (squeezenet, mobilenetv2, resnet50, 200 runs).

## Environment
| Parameter          | Value                                        |
|--------------------|----------------------------------------------|
| QEMU CPU           | cortex-a57                                   |
| vCPUs              | 1 (-smp 1)                                   |
| RAM                | 2048 MB                                      |
| MiniOS threading   | Single-threaded (bare-metal, by design)      |
| Ubuntu threading   | OMP_NUM_THREADS=1 (artificially constrained) |
| Runs per model     | 50 (5 warmup discarded)                      |
| Models tested      | mnist, squeezenet, shufflenet                |
| ONNX Runtime       | MiniOS custom build / Ubuntu pip version     |
| Date               | 2026-04-04                                   |

## Latency Results (ms)
| Model      | OS      | Mean | p50  | p95  | Min  | Max  |
|------------|---------|------|------|------|------|------|
| mnist      | MiniOS  | 9.07 | 8.50 | 13.20 | 7.90 | 15.60 |
| mnist      | Ubuntu  | 8.55 | 8.50 | 9.08 | 8.13 | 9.46 |
| squeezenet | MiniOS  | 35251.46 | 35220.20 | 35625.00 | 34849.80 | 35919.70 |
| squeezenet | Ubuntu  | 2992.99 | 2991.77 | 3090.25 | 2904.56 | 3133.30 |
| shufflenet | MiniOS  | 30566.85 | 30566.45 | 30857.30 | 30273.80 | 31016.00 |
| shufflenet | Ubuntu  | 1386.98 | 1373.10 | 1473.69 | 1339.66 | 1516.64 |


## Memory & Load Time
| Model      | MiniOS RSS(kb) | Ubuntu RSS(kb) | ΔRSS | MiniOS Load(ms) | Ubuntu Load(ms) | ΔLoad |
|------------|----------------|----------------|------|-----------------|-----------------|-------|
| mnist      | 27             | 46824          | 46797 | 11.00           | 485.65          | 474.65 |
| squeezenet | 5422           | 57892          | 52470 | 87.00           | 488.87          | 401.87 |
| shufflenet | 9594           | 66908          | 57314 | 214.00          | 1023.53         | 809.53 |


## Delta Summary (positive = Ubuntu is slower = MiniOS wins)
| Model      | ΔLatency(ms) | ΔLatency(%) | ΔMemory(kb) | ΔLoad(ms) |
|------------|--------------|-------------|-------------|-----------|
| mnist      | -0.52        | -6.04       | 46797       | 474.65    |
| squeezenet | -32258.47    | -1077.80    | 52470       | 401.87    |
| shufflenet | -29179.87    | -2103.84    | 57314       | 809.53    |


## Observations
- Latency: MiniOS is currently slower than Ubuntu on all three models in this QEMU setup. The functional bug is fixed, but per-node serial logging and current runtime implementation dominate total inference time for larger models.
- Jitter: MiniOS jitter is tight on large models (`squeezenet` p95/mean ~1.011, `shufflenet` ~1.010) and comparable/slightly tighter than Ubuntu (`squeezenet` ~1.032, `shufflenet` ~1.063). `mnist` on MiniOS shows visible tail spikes from periodic runtime activity (`p95/mean ~1.456).
- Memory: MiniOS reports much lower peak RSS than Ubuntu for all models (ΔRSS: mnist 46797, squeezenet 52470, shufflenet 57314 KB), consistent with minimal bare-metal footprint.
- Load time: MiniOS model load is faster across all models (ΔLoad: mnist 474.65ms, squeezenet 401.87ms, shufflenet 809.53ms), matching expectation without Linux userspace and dynamic loading overheads.
- Timer resolution: No timer-resolution anomaly was detected in this run (no 0.0 latencies, non-identical distributions, and realistic millisecond-scale values).


## Verdict
mnist      → MiniOS 0.52 ms slower (6.04%)
squeezenet → MiniOS 32258.47 ms slower (1077.80%)
shufflenet → MiniOS 29179.87 ms slower (2103.84%)


## Next Step Recommendation
- Full benchmark expansion to 200 runs plus `mobilenetv2` and `resnet50` is warranted now that functional correctness is restored across mnist, squeezenet, and shufflenet.
- Before presenting performance claims, disable per-node execution prints during benchmark mode and rerun this quick suite once; current serial I/O overhead is likely inflating MiniOS latency on large models.
- Keep memory/load reporting as-is (it is now non-zero and consistent) and add one additional run on physical hardware to separate QEMU effects from true bare-metal behavior.


## Raw Data

### mnist_minios.json
```json
{
  "model": "mnist",
  "os": "minios",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    8.6,
    8.5,
    8.2,
    8.4,
    8.5,
    8.3,
    8.7,
    8.0,
    8.1,
    8.2,
    7.9,
    8.0,
    8.4,
    8.6,
    8.4,
    8.4,
    8.4,
    8.7,
    11.4,
    15.6,
    13.2,
    11.7,
    12.1,
    14.3,
    9.1,
    9.4,
    9.1,
    9.1,
    8.6,
    8.7,
    8.3,
    8.5,
    8.5,
    8.2,
    9.8,
    8.6,
    8.5,
    8.5,
    8.9,
    8.6,
    8.9,
    8.3,
    8.7,
    8.6,
    8.5,
    8.8,
    8.2,
    8.1,
    8.1,
    8.1
  ],
  "peak_rss_kb": 27,
  "model_load_ms": 11.0
}
```

### mnist_ubuntu.json
```json
{
  "model": "mnist",
  "os": "ubuntu",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    8.496665000052417,
    8.624524999959249,
    8.584295999980895,
    8.378010000001268,
    8.454130000018267,
    8.903442000018913,
    8.378505999985464,
    8.822470999916732,
    8.545491000063521,
    8.279573999971035,
    8.512049999922056,
    8.361825000065437,
    8.626253999977962,
    8.383821000052194,
    8.388287999991917,
    8.210480999991887,
    8.17918399991413,
    8.146206000105849,
    8.273794999922757,
    8.316713999988679,
    8.328559000005953,
    8.226841999999124,
    8.40447200005201,
    8.256233999986762,
    8.524216000068918,
    8.201580999980251,
    8.2884109999668,
    8.132359000001088,
    8.215940000013688,
    8.598511999934999,
    9.000485999990815,
    9.46202699992682,
    8.997716000067157,
    8.664914999940265,
    8.802667999930236,
    8.752304999916305,
    8.714556999962042,
    8.413596999957917,
    9.293088999925203,
    8.587960999989264,
    8.401798000022609,
    8.49442399999134,
    8.480048000023999,
    8.25016599992523,
    8.806205999917438,
    8.93747500003883,
    9.144721999973626,
    8.815491000063957,
    8.535661999985678,
    8.657471000105943
  ],
  "peak_rss_kb": 46824,
  "model_load_ms": 485.6542340000942
}
```

### squeezenet_minios.json
```json
{
  "model": "squeezenet",
  "os": "minios",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    35099.0,
    35440.6,
    35420.9,
    35139.1,
    35162.1,
    35625.0,
    35201.3,
    35249.4,
    35322.1,
    35380.6,
    35230.9,
    35410.0,
    35919.7,
    35482.2,
    35305.5,
    35191.1,
    34998.7,
    34849.8,
    35006.9,
    34864.6,
    35032.6,
    35182.8,
    35099.2,
    35075.4,
    35314.1,
    35378.2,
    35133.9,
    35143.2,
    35646.3,
    35209.5,
    35551.3,
    35348.2,
    35381.2,
    35362.2,
    35349.4,
    35343.4,
    35454.0,
    35188.4,
    35380.8,
    35390.3,
    35184.5,
    35282.2,
    35191.6,
    35061.5,
    35132.0,
    34905.2,
    35194.1,
    35396.3,
    34855.4,
    35106.5
  ],
  "peak_rss_kb": 5422,
  "model_load_ms": 87.0
}
```

### squeezenet_ubuntu.json
```json
{
  "model": "squeezenet",
  "os": "ubuntu",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    2989.517054999965,
    3001.237866999986,
    3054.5382740000377,
    2998.2625320000125,
    3111.706570000024,
    3049.1681839999956,
    2957.6359259999663,
    2934.139383999991,
    3018.0325219999986,
    2992.5206909999815,
    3093.4570469999016,
    3080.9755430000223,
    2994.3338920000997,
    3010.6697760001,
    3133.301829000061,
    2959.057193000035,
    3002.422700000011,
    3024.106065999945,
    2998.613774999967,
    3086.3333710000234,
    3042.409058999965,
    2991.0110030000396,
    2998.200648999955,
    2980.300457999988,
    3010.5476229999795,
    3002.4817879999546,
    2975.1943620000247,
    3079.556661999959,
    2999.8622660000365,
    3072.9075030000104,
    2984.227240999985,
    2904.5595170000524,
    2917.920067999944,
    2945.332148000034,
    2988.1194509999887,
    2937.4085270000023,
    3028.1177719999732,
    2920.0259550000283,
    2935.8913739999934,
    2930.740023999988,
    2987.4829120000186,
    2930.201468000064,
    2960.861641000065,
    2954.4545340000923,
    2993.76493200009,
    2957.435650999969,
    2979.9616489999607,
    2907.191832999956,
    2918.9301530000193,
    2924.61278899998
  ],
  "peak_rss_kb": 57892,
  "model_load_ms": 488.87322299992775
}
```

### shufflenet_minios.json
```json
{
  "model": "shufflenet",
  "os": "minios",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    30329.7,
    30730.0,
    30662.4,
    30656.3,
    30589.5,
    30709.0,
    30440.3,
    30585.4,
    30700.2,
    31016.0,
    30704.3,
    30492.7,
    30602.3,
    30914.6,
    30566.9,
    30428.9,
    30273.8,
    30555.9,
    30530.8,
    30718.2,
    30453.1,
    30578.5,
    30322.5,
    30562.9,
    30723.3,
    30747.6,
    30400.6,
    30502.7,
    30631.2,
    30618.7,
    30494.9,
    30534.3,
    30549.3,
    30501.4,
    30633.2,
    30601.8,
    30857.3,
    30566.0,
    30650.4,
    30536.8,
    30477.4,
    30316.4,
    30415.1,
    30510.1,
    30591.9,
    30599.9,
    30525.3,
    30358.7,
    30304.4,
    30569.6
  ],
  "peak_rss_kb": 9594,
  "model_load_ms": 214.0
}
```

### shufflenet_ubuntu.json
```json
{
  "model": "shufflenet",
  "os": "ubuntu",
  "runs": 50,
  "warmup": 5,
  "latencies_ms": [
    1400.5011610000793,
    1368.2292859999734,
    1369.1686270000218,
    1371.166895999977,
    1389.6176629999673,
    1351.0006340000018,
    1345.4794020000236,
    1359.455926999999,
    1339.655113000049,
    1370.6595319999906,
    1383.1239529999948,
    1392.9278739999518,
    1418.9808220000941,
    1350.2415909999854,
    1366.6284599999017,
    1400.1645419999704,
    1363.433420999968,
    1380.9896300000446,
    1386.3428170000134,
    1483.9971659999946,
    1423.739415,
    1358.9425960000199,
    1399.3126029999985,
    1383.1710770000427,
    1407.461006999938,
    1370.5515419999301,
    1431.0286119999773,
    1363.4157260000848,
    1344.47198700002,
    1404.3984070000306,
    1373.0144950000067,
    1373.1877339999983,
    1373.5413979999294,
    1372.6996269999745,
    1343.3395539999538,
    1397.7251699999442,
    1358.604315999969,
    1381.4929819999406,
    1436.7891189999682,
    1408.101835000025,
    1372.6837269999805,
    1369.0866370000094,
    1489.293760999999,
    1516.6422629999943,
    1461.0905170000024,
    1394.4258800000853,
    1354.2984219999425,
    1357.7344049999738,
    1368.8815890000114,
    1367.889468000044
  ],
  "peak_rss_kb": 66908,
  "model_load_ms": 1023.531303000027
}
```

## Run History
| Run | Date       | MiniOS Status              | Ubuntu Status |
|-----|------------|----------------------------|---------------|
| 1   | 2026-04-04 | FAILED (SHAPE_MISMATCH)    | PASSED        |
| 2   | 2026-04-05 | PASSED (bug fixed)         | skipped (reused) |
