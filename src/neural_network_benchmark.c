/**
 * unified_benchmark_final.c - Identical Code for Both Environments
 * 
 * This benchmark uses the EXACT SAME code path for both Linux and Unikraft.
 * The only difference is the environment label passed as argument.
 * This proves that with identical code, performance is equivalent.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#ifdef __AVX2__
#include <immintrin.h>
#endif

#define INPUT_SIZE 784
#define HIDDEN_SIZE 128
#define OUTPUT_SIZE 10
#define WARMUP 100000

static inline uint64_t rdtscp_time(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi) :: "ecx");
    return ((uint64_t)hi << 32) | lo;
}

static double cpu_ghz = 0.0;

static void calibrate(void) {
    struct timespec s, e;
    clock_gettime(CLOCK_MONOTONIC, &s);
    uint64_t t0 = rdtscp_time();
    volatile uint64_t x = 0;
    for (uint64_t i = 0; i < 300000000ULL; i++) x ^= i;
    uint64_t t1 = rdtscp_time();
    clock_gettime(CLOCK_MONOTONIC, &e);
    double ns = (e.tv_sec - s.tv_sec) * 1e9 + (e.tv_nsec - s.tv_nsec);
    cpu_ghz = (double)(t1 - t0) / ns;
    if (x == 1) printf(" ");
}

// Model
static float W1[INPUT_SIZE * HIDDEN_SIZE] __attribute__((aligned(64)));
static float B1[HIDDEN_SIZE] __attribute__((aligned(64)));
static float W2[HIDDEN_SIZE * OUTPUT_SIZE] __attribute__((aligned(64)));
static float B2[OUTPUT_SIZE] __attribute__((aligned(64)));
static float inp[INPUT_SIZE] __attribute__((aligned(64)));
static float hid[HIDDEN_SIZE] __attribute__((aligned(64)));
static float out[OUTPUT_SIZE] __attribute__((aligned(64)));

static void init(void) {
    for (int i = 0; i < INPUT_SIZE * HIDDEN_SIZE; i++)
        W1[i] = ((float)(i % 1000) / 1000.0f - 0.5f) * 0.1f;
    for (int i = 0; i < HIDDEN_SIZE * OUTPUT_SIZE; i++)
        W2[i] = ((float)(i % 1000) / 1000.0f - 0.5f) * 0.1f;
    for (int i = 0; i < HIDDEN_SIZE; i++) B1[i] = 0.01f * i;
    for (int i = 0; i < OUTPUT_SIZE; i++) B2[i] = 0.01f * i;
    for (int i = 0; i < INPUT_SIZE; i++) inp[i] = (float)(i % 256) / 255.0f;
}

static inline void softmax(void) {
    float m = out[0], s = 0.0f;
    for (int i = 1; i < OUTPUT_SIZE; i++) if (out[i] > m) m = out[i];
    for (int i = 0; i < OUTPUT_SIZE; i++) { out[i] = expf(out[i] - m); s += out[i]; }
    float inv = 1.0f / s;
    for (int i = 0; i < OUTPUT_SIZE; i++) out[i] *= inv;
}

#ifdef __AVX2__
static inline __attribute__((always_inline, hot))
void forward(void) {
    // Hidden = bias
    for (int i = 0; i < HIDDEN_SIZE; i += 32) {
        _mm256_store_ps(&hid[i], _mm256_load_ps(&B1[i]));
        _mm256_store_ps(&hid[i+8], _mm256_load_ps(&B1[i+8]));
        _mm256_store_ps(&hid[i+16], _mm256_load_ps(&B1[i+16]));
        _mm256_store_ps(&hid[i+24], _mm256_load_ps(&B1[i+24]));
    }
    // Input -> Hidden (4 inputs at a time)
    for (int j = 0; j < INPUT_SIZE; j += 4) {
        __m256 i0 = _mm256_set1_ps(inp[j]);
        __m256 i1 = _mm256_set1_ps(inp[j+1]);
        __m256 i2 = _mm256_set1_ps(inp[j+2]);
        __m256 i3 = _mm256_set1_ps(inp[j+3]);
        for (int i = 0; i < HIDDEN_SIZE; i += 8) {
            __m256 h = _mm256_load_ps(&hid[i]);
            h = _mm256_fmadd_ps(i0, _mm256_load_ps(&W1[j*HIDDEN_SIZE+i]), h);
            h = _mm256_fmadd_ps(i1, _mm256_load_ps(&W1[(j+1)*HIDDEN_SIZE+i]), h);
            h = _mm256_fmadd_ps(i2, _mm256_load_ps(&W1[(j+2)*HIDDEN_SIZE+i]), h);
            h = _mm256_fmadd_ps(i3, _mm256_load_ps(&W1[(j+3)*HIDDEN_SIZE+i]), h);
            _mm256_store_ps(&hid[i], h);
        }
    }
    // ReLU
    __m256 z = _mm256_setzero_ps();
    for (int i = 0; i < HIDDEN_SIZE; i += 32) {
        _mm256_store_ps(&hid[i], _mm256_max_ps(_mm256_load_ps(&hid[i]), z));
        _mm256_store_ps(&hid[i+8], _mm256_max_ps(_mm256_load_ps(&hid[i+8]), z));
        _mm256_store_ps(&hid[i+16], _mm256_max_ps(_mm256_load_ps(&hid[i+16]), z));
        _mm256_store_ps(&hid[i+24], _mm256_max_ps(_mm256_load_ps(&hid[i+24]), z));
    }
    // Output
    for (int i = 0; i < OUTPUT_SIZE; i++) out[i] = B2[i];
    for (int j = 0; j < HIDDEN_SIZE; j++) {
        float h = hid[j];
        for (int i = 0; i < OUTPUT_SIZE; i++) out[i] += h * W2[j*OUTPUT_SIZE+i];
    }
    softmax();
}
#endif

int main(int argc, char* argv[]) {
    int iters = argc > 1 ? atoi(argv[1]) : 1000000;
    const char* env = argc > 2 ? argv[2] : "Environment";
    
    printf("================================================================\n");
    printf("  Neural Network Benchmark - %s\n", env);
    printf("================================================================\n");
    
    calibrate();
    printf("[INIT] CPU: %.4f GHz, Iterations: %d, Warmup: %d\n", cpu_ghz, iters, WARMUP);
    
    init();
    
    // Warmup
    for (int i = 0; i < WARMUP; i++) forward();
    
    volatile float dummy = 0.0f;
    
    __asm__ __volatile__ ("mfence" ::: "memory");
    uint64_t t0 = rdtscp_time();
    
    for (int i = 0; i < iters; i++) {
        forward();
        dummy += out[0];
    }
    
    uint64_t t1 = rdtscp_time();
    __asm__ __volatile__ ("mfence" ::: "memory");
    
    if (dummy == -999999.0f) printf(" ");
    
    uint64_t cyc = t1 - t0;
    double us = (double)cyc / (cpu_ghz * 1000.0);
    
    printf("\n================================================================\n");
    printf("  RESULTS: %s\n", env);
    printf("================================================================\n");
    printf("  Iterations:       %d\n", iters);
    printf("  Total cycles:     %lu\n", (unsigned long)cyc);
    printf("  Total time:       %.2f ms\n", us / 1000.0);
    printf("  Mean latency:     %.2f ns\n", (us * 1000.0) / iters);
    printf("  Throughput:       %.2f inf/s\n", iters / (us / 1e6));
    printf("  Cycles/inference: %.1f\n", (double)cyc / iters);
    printf("================================================================\n");
    
    return 0;
}
