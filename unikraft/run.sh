#!/bin/bash
# Unikraft Build and Run Script
# =============================

set -e

ITERATIONS=${1:-1000000}

echo "=============================================="
echo "  Unikraft ML Inference Benchmark"
echo "=============================================="
echo ""

# Check if kraft is installed
if ! command -v kraft &> /dev/null; then
    echo "Error: kraft is not installed"
    echo "Install with: pip3 install kraft"
    echo "Or: curl -sSL https://get.unikraft.io | sh"
    exit 1
fi

# Build
echo "[1/3] Building Unikraft image..."
kraft build

# Update command arguments
echo "[2/3] Configuring runtime..."

# Run
echo "[3/3] Running benchmark..."
kraft run -M 256 -- /main $ITERATIONS

echo ""
echo "=============================================="
echo "  Benchmark Complete"
echo "=============================================="
