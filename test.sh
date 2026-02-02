#!/bin/bash
# Quick test script to verify benchmark compilation and execution

set -e

echo "=========================================="
echo "  Quick Test - VM vs Unikernel Benchmark"
echo "=========================================="
echo ""

# Clean and build
echo "[1/3] Building benchmark..."
make clean > /dev/null 2>&1
make benchmark > /dev/null 2>&1
echo "✓ Build successful"
echo ""

# Run quick test
echo "[2/3] Running quick test (100K iterations)..."
./bin/ml_inference_benchmark 100000 "Test" > /tmp/benchmark_test.txt 2>&1

# Check output
if grep -q "Throughput" /tmp/benchmark_test.txt; then
    THROUGHPUT=$(grep "Throughput" /tmp/benchmark_test.txt | awk '{print $2}')
    echo "✓ Test successful"
    echo "  Throughput: $THROUGHPUT inf/s"
else
    echo "✗ Test failed"
    cat /tmp/benchmark_test.txt
    exit 1
fi
echo ""

# Verify AVX2 support
echo "[3/3] Checking CPU features..."
if grep -q avx2 /proc/cpuinfo; then
    echo "✓ AVX2 supported"
else
    echo "⚠ AVX2 not supported (performance may be degraded)"
fi

if grep -q fma /proc/cpuinfo; then
    echo "✓ FMA supported"
else
    echo "⚠ FMA not supported (performance may be degraded)"
fi

echo ""
echo "=========================================="
echo "  All tests passed!"
echo "=========================================="

rm -f /tmp/benchmark_test.txt
