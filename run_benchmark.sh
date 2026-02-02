#!/bin/bash
# Automated benchmark script for running multiple trials
# Usage: ./run_benchmark.sh [iterations] [trials]

ITERATIONS=${1:-1000000}
TRIALS=${2:-10}
OUTPUT_FILE="results/benchmark_output_$(date +%Y%m%d_%H%M%S).txt"

echo "=============================================="
echo "  VM vs Unikernel ML Inference Benchmark"
echo "=============================================="
echo "Iterations per trial: $ITERATIONS"
echo "Number of trials: $TRIALS"
echo "Output file: $OUTPUT_FILE"
echo ""

# Create results directory if it doesn't exist
mkdir -p results

# Initialize output file
echo "VM vs Unikernel Benchmark Results" > "$OUTPUT_FILE"
echo "Date: $(date)" >> "$OUTPUT_FILE"
echo "Iterations: $ITERATIONS" >> "$OUTPUT_FILE"
echo "Trials: $TRIALS" >> "$OUTPUT_FILE"
echo "" >> "$OUTPUT_FILE"

declare -a linux_results
declare -a unikraft_results

echo "Running benchmarks..."
echo ""

for i in $(seq 1 $TRIALS); do
    printf "Trial %2d/%d: " $i $TRIALS
    
    # Run Linux VM benchmark
    LINUX=$(./bin/ml_inference_benchmark $ITERATIONS "Linux-VM" 2>&1 | \
            grep "Throughput" | awk '{print $2}')
    linux_results+=($LINUX)
    
    # Run Unikraft benchmark  
    UNIKRAFT=$(./bin/ml_inference_benchmark $ITERATIONS "Unikraft" 2>&1 | \
               grep "Throughput" | awk '{print $2}')
    unikraft_results+=($UNIKRAFT)
    
    # Log to file
    echo "Trial $i: Linux=$LINUX, Unikraft=$UNIKRAFT" >> "$OUTPUT_FILE"
    
    printf "Linux=%10.2f inf/s  Unikraft=%10.2f inf/s\n" $LINUX $UNIKRAFT
done

echo ""
echo "=============================================="
echo "  Summary"
echo "=============================================="

# Calculate averages
linux_sum=0
unikraft_sum=0
for i in $(seq 0 $((TRIALS-1))); do
    linux_sum=$(echo "$linux_sum + ${linux_results[$i]}" | bc)
    unikraft_sum=$(echo "$unikraft_sum + ${unikraft_results[$i]}" | bc)
done

linux_avg=$(echo "scale=2; $linux_sum / $TRIALS" | bc)
unikraft_avg=$(echo "scale=2; $unikraft_sum / $TRIALS" | bc)

echo "" >> "$OUTPUT_FILE"
echo "Summary:" >> "$OUTPUT_FILE"
echo "Linux VM Average: $linux_avg inf/s" >> "$OUTPUT_FILE"
echo "Unikraft Average: $unikraft_avg inf/s" >> "$OUTPUT_FILE"

echo "Linux VM Average:     $linux_avg inf/s"
echo "Unikraft Average:     $unikraft_avg inf/s"
echo ""
echo "Results saved to: $OUTPUT_FILE"
echo "=============================================="
