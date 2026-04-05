#!/bin/bash
# =============================================================================
# ubuntu_setup.sh — Run inside the Ubuntu QEMU VM to set up benchmarking
#
# Run this script once after booting the Ubuntu VM:
#   bash /bench/ubuntu_setup.sh
# =============================================================================

set -euo pipefail

echo "=== Ubuntu Benchmark VM Setup ==="

# ---- 1. Install dependencies ----
echo "[1/5] Installing system packages..."
sudo apt update -qq
sudo apt install -y python3 python3-pip time

echo "[1/5] Installing Python packages..."
pip3 install --quiet onnxruntime numpy

# ---- 2. Pin thread counts ----
echo "[2/5] Setting thread environment variables..."
THREAD_VARS=(
    "OMP_NUM_THREADS=1"
    "OPENBLAS_NUM_THREADS=1"
    "MKL_NUM_THREADS=1"
    "ONNXRUNTIME_NUM_THREADS=1"
)

for var in "${THREAD_VARS[@]}"; do
    if ! grep -qF "$var" /etc/environment 2>/dev/null; then
        echo "$var" | sudo tee -a /etc/environment > /dev/null
        echo "  Added $var to /etc/environment"
    else
        echo "  $var already in /etc/environment"
    fi
    # Also export for current session
    export "$var"
done

# ---- 3. Suppress background noise ----
echo "[3/5] Suppressing background services..."
for svc in cron avahi-daemon bluetooth ModemManager; do
    sudo systemctl stop "$svc" 2>/dev/null && echo "  Stopped $svc" || echo "  $svc not running (ok)"
    sudo systemctl disable "$svc" 2>/dev/null && echo "  Disabled $svc" || true
done

# ---- 4. Create directory structure ----
echo "[4/5] Creating benchmark directories..."
sudo mkdir -p /bench/models /bench/inputs /bench/results
sudo chown -R "$(whoami)" /bench

# ---- 5. Verify thread count ----
echo "[5/5] Verifying thread count..."
echo "  OMP_NUM_THREADS=${OMP_NUM_THREADS:-NOT SET}"
python3 -c "
import onnxruntime as ort
opts = ort.SessionOptions()
opts.intra_op_num_threads = 1
print('  onnxruntime intra_op_num_threads=1 ... OK')
"

echo ""
echo "=== Setup complete ==="
echo ""
echo "Next steps:"
echo "  1. Copy model files to /bench/models/"
echo "  2. Copy .npy input files to /bench/inputs/"
echo "  3. Copy run_bench.py to /bench/run_bench.py"
echo ""
echo "Dry run:"
echo "  python3 /bench/run_bench.py \\"
echo "    --model  /bench/models/squeezenet.onnx \\"
echo "    --input  /bench/inputs/squeezenet.npy \\"
echo "    --runs 1 --warmup 0 --out /bench/results/"
