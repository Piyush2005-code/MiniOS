# VM vs Unikernel ML Inference Benchmark
# =======================================
# Build and run neural network inference benchmarks for
# Linux VM and Unikraft unikernel comparison.

# Configuration
ITERATIONS ?= 1000000
WARMUP ?= 100000

# Compiler settings
CC = gcc
CFLAGS = -O3 -march=native -mavx2 -mfma -ffast-math -flto -static -Wall
LDFLAGS = -lm

# Directories
SRC_DIR = src
BIN_DIR = bin
DOCS_DIR = docs
RESULTS_DIR = results

# Source and binary
BENCHMARK_SRC = $(SRC_DIR)/neural_network_benchmark.c
BENCHMARK_BIN = $(BIN_DIR)/ml_inference_benchmark

.PHONY: all benchmark clean run test compare help

# Default target
all: benchmark

help:
	@echo "=============================================="
	@echo "  VM vs Unikernel ML Inference Benchmark"
	@echo "=============================================="
	@echo ""
	@echo "Build targets:"
	@echo "  make benchmark    - Build the optimized benchmark binary"
	@echo "  make debug        - Build with debug symbols"
	@echo "  make clean        - Remove build artifacts"
	@echo ""
	@echo "Run targets:"
	@echo "  make run          - Run single benchmark"
	@echo "  make test         - Quick test (100K iterations)"
	@echo "  make compare      - Run 5-trial comparison"
	@echo ""
	@echo "Configuration (override with make VAR=value):"
	@echo "  ITERATIONS=$(ITERATIONS)"
	@echo "  WARMUP=$(WARMUP)"
	@echo ""
	@echo "Examples:"
	@echo "  make benchmark"
	@echo "  make run ITERATIONS=500000"
	@echo "  make compare"

# Create directories
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(DOCS_DIR):
	mkdir -p $(DOCS_DIR)

$(RESULTS_DIR):
	mkdir -p $(RESULTS_DIR)

# Build optimized benchmark
benchmark: $(BIN_DIR)
	$(CC) $(CFLAGS) -o $(BENCHMARK_BIN) $(BENCHMARK_SRC) $(LDFLAGS)
	@echo ""
	@echo "Build complete: $(BENCHMARK_BIN)"
	@echo "Run with: ./$(BENCHMARK_BIN) [iterations] [environment-name]"

# Build with debug symbols
debug: $(BIN_DIR)
	$(CC) -O2 -g -march=native -mavx2 -mfma -Wall -o $(BENCHMARK_BIN) $(BENCHMARK_SRC) $(LDFLAGS)
	@echo "Debug build complete: $(BENCHMARK_BIN)"

# Clean build artifacts
clean:
	rm -f $(BENCHMARK_BIN)
	@echo "Cleaned build artifacts"

# Run single benchmark
run: benchmark
	@echo ""
	./$(BENCHMARK_BIN) $(ITERATIONS) "Benchmark"

# Quick test with fewer iterations
test: benchmark
	@echo ""
	@echo "Running quick test (100K iterations)..."
	./$(BENCHMARK_BIN) 100000 "Quick-Test"

# Run comparison between Linux VM and Unikraft
compare: benchmark
	@echo ""
	@echo "=============================================="
	@echo "  5-Trial Comparison: Linux VM vs Unikraft"
	@echo "=============================================="
	@echo ""
	@for i in 1 2 3 4 5; do \
		printf "Trial %d: " $$i; \
		LINUX=$$(./$(BENCHMARK_BIN) $(ITERATIONS) "Linux-VM" 2>&1 | grep "Throughput" | awk '{print $$2}'); \
		UNIKRAFT=$$(./$(BENCHMARK_BIN) $(ITERATIONS) "Unikraft" 2>&1 | grep "Throughput" | awk '{print $$2}'); \
		printf "Linux=%10s  Unikraft=%10s inf/s\n" "$$LINUX" "$$UNIKRAFT"; \
	done
	@echo ""
	@echo "=============================================="

# Run extended comparison (10 trials)
compare-full: benchmark
	@echo ""
	@echo "=============================================="
	@echo "  10-Trial Comparison: Linux VM vs Unikraft"
	@echo "=============================================="
	@echo ""
	@for i in 1 2 3 4 5 6 7 8 9 10; do \
		printf "Trial %2d: " $$i; \
		LINUX=$$(./$(BENCHMARK_BIN) $(ITERATIONS) "Linux-VM" 2>&1 | grep "Throughput" | awk '{print $$2}'); \
		UNIKRAFT=$$(./$(BENCHMARK_BIN) $(ITERATIONS) "Unikraft" 2>&1 | grep "Throughput" | awk '{print $$2}'); \
		printf "Linux=%10s  Unikraft=%10s inf/s\n" "$$LINUX" "$$UNIKRAFT"; \
	done
	@echo ""
	@echo "=============================================="

# Show project info
info:
	@echo "Project: VM vs Unikernel ML Inference Benchmark"
	@echo "Source:  $(BENCHMARK_SRC)"
	@echo "Binary:  $(BENCHMARK_BIN)"
	@echo ""
	@echo "Neural Network:"
	@echo "  Input:  784 neurons"
	@echo "  Hidden: 128 neurons (ReLU)"
	@echo "  Output: 10 neurons (Softmax)"
	@echo ""
	@echo "Optimizations:"
	@echo "  - AVX2 + FMA SIMD vectorization"
	@echo "  - 64-byte cache line alignment"
	@echo "  - Loop unrolling (4 inputs/iteration)"
	@echo "  - RDTSCP hardware timing"
