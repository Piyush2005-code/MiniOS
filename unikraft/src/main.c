/**
 * Unikraft ML Inference Benchmark - Main Entry Point
 * 
 * This is the unikernel version of the neural network benchmark.
 * Compile with kraft or standalone for Unikraft deployment.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#define INPUT_SIZE 784
#define HIDDEN_SIZE 128
#define OUTPUT_SIZE 10
#define WARMUP 100000
#define DEFAULT_ITERATIONS 1000000

// Use RDTSCP for timing on Unikraft
static inline uint64_t rdtscp_time(void) {
    uint32_t lo, hi;
    __asm__ __volatile__ ("rdtscp" : "=a" (lo), "=d" (hi) :: "ecx");
    return ((uint64_t)hi << 32) | lo;
}

// CPU frequency calibration
static double cpu_ghz = 3.0;  // Default estimate

// Model weights - aligned for SIMD
static float W1[INPUT_SIZE * HIDDEN_SIZE] __attribute__((aligned(64)));
static float B1[HIDDEN_SIZE] __attribute__((aligned(64)));
static float W2[HIDDEN_SIZE * OUTPUT_SIZE] __attribute__((aligned(64)));
static float B2[OUTPUT_SIZE] __attribute__((aligned(64)));
static float inp[INPUT_SIZE] __attribute__((aligned(64)));
static float hid[HIDDEN_SIZE] __attribute__((aligned(64)));
static float out[OUTPUT_SIZE] __attribute__((aligned(64)));

// Initialize model weights
static void init_model(void) {
    for (int i = 0; i < INPUT_SIZE * HIDDEN_SIZE; i++)
        W1[i] = ((float)(i % 1000) / 1000.0f - 0.5f) * 0.1f;
    for (int i = 0; i < HIDDEN_SIZE * OUTPUT_SIZE; i++)
        W2[i] = ((float)(i % 1000) / 1000.0f - 0.5f) * 0.1f;
    for (int i = 0; i < HIDDEN_SIZE; i++) B1[i] = 0.01f * i;
    for (int i = 0; i < OUTPUT_SIZE; i++) B2[i] = 0.01f * i;
    for (int i = 0; i < INPUT_SIZE; i++) inp[i] = (float)(i % 256) / 255.0f;
}

// Softmax activation
static inline void softmax(void) {
    float m = out[0], s = 0.0f;
    for (int i = 1; i < OUTPUT_SIZE; i++) if (out[i] > m) m = out[i];
    for (int i = 0; i < OUTPUT_SIZE; i++) { out[i] = expf(out[i] - m); s += out[i]; }
    float inv = 1.0f / s;
    for (int i = 0; i < OUTPUT_SIZE; i++) out[i] *= inv;
}

// Forward pass - optimized with unrolling
static void forward(void) {
    // Hidden layer
    for (int i = 0; i < HIDDEN_SIZE; i++) {
        float sum = B1[i];
        for (int j = 0; j < INPUT_SIZE; j += 4) {
            sum += inp[j] * W1[j * HIDDEN_SIZE + i];
            sum += inp[j+1] * W1[(j+1) * HIDDEN_SIZE + i];
            sum += inp[j+2] * W1[(j+2) * HIDDEN_SIZE + i];
            sum += inp[j+3] * W1[(j+3) * HIDDEN_SIZE + i];
        }
        hid[i] = sum > 0 ? sum : 0;  // ReLU
    }
    
    // Output layer
    for (int i = 0; i < OUTPUT_SIZE; i++) {
        float sum = B2[i];
        for (int j = 0; j < HIDDEN_SIZE; j++) {
            sum += hid[j] * W2[j * OUTPUT_SIZE + i];
        }
        out[i] = sum;
    }
    
    softmax();
}

int main(int argc, char* argv[]) {
    int iterations = DEFAULT_ITERATIONS;
    
    if (argc > 1) {
        iterations = atoi(argv[1]);
        if (iterations <= 0) iterations = DEFAULT_ITERATIONS;
    }
    
    printf("================================================================\n");
    printf("  Unikraft Neural Network Inference Benchmark\n");
    printf("================================================================\n");
    printf("[CONFIG] Iterations: %d, Warmup: %d\n", iterations, WARMUP);
    
    // Initialize
    init_model();
    
    // Warmup phase
    printf("[WARMUP] Running %d warmup iterations...\n", WARMUP);
    for (int i = 0; i < WARMUP; i++) forward();
    
    // Benchmark
    printf("[BENCH] Running %d iterations...\n", iterations);
    
    volatile float dummy = 0.0f;
    uint64_t t0 = rdtscp_time();
    
    for (int i = 0; i < iterations; i++) {
        forward();
        dummy += out[0];
    }
    
    uint64_t t1 = rdtscp_time();
    
    if (dummy == -999999.0f) printf(" ");  // Prevent optimization
    
    // Calculate results
    uint64_t cycles = t1 - t0;
    double seconds = (double)cycles / (cpu_ghz * 1e9);
    double throughput = (double)iterations / seconds;
    double latency_ns = (seconds * 1e9) / iterations;
    
    printf("\n================================================================\n");
    printf("  RESULTS: Unikraft\n");
    printf("================================================================\n");
    printf("  Iterations:       %d\n", iterations);
    printf("  Total cycles:     %lu\n", (unsigned long)cycles);
    printf("  Est. time:        %.2f ms\n", seconds * 1000.0);
    printf("  Mean latency:     %.2f ns\n", latency_ns);
    printf("  Throughput:       %.2f inf/s\n", throughput);
    printf("  Cycles/inference: %.1f\n", (double)cycles / iterations);
    printf("================================================================\n");
    
    return 0;
}
