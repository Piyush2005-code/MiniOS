#!/usr/bin/env bash
# docker_qemu_test.sh — Run MiniOS QEMU tests inside a Docker container.
#
# Uses a pinned Debian image with qemu-system-arm installed so the host
# does not need QEMU at all.  The project root is mounted read-only;
# kernel.elf must already be built (make TEST=1) before calling this.
#
# Usage (from project root):
#   make TEST=1          # build kernel.elf with the test runner
#   bash scripts/docker_qemu_test.sh
#
# Or to build + test in one step:
#   bash scripts/docker_qemu_test.sh --build
#
# Exit code: 0 = all tests pass, 1 = any failure

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
ELF="$PROJECT_ROOT/build/kernel.elf"

DOCKER_IMAGE="debian:bookworm-slim"
QEMU_PKG="qemu-system-arm"   # provides qemu-system-aarch64 on Debian

# ---- Optional --build flag  ----
if [[ "${1:-}" == "--build" ]]; then
    echo ">>> Building kernel.elf with TEST=1 ..."
    make -C "$PROJECT_ROOT" TEST=1 build/kernel.elf
fi

if [[ ! -f "$ELF" ]]; then
    echo "ERROR: $ELF not found. Run 'make TEST=1' first (or pass --build)."
    exit 1
fi

echo "============================================"
echo "  MiniOS QEMU Test Runner (Docker)"
echo "  Image : $DOCKER_IMAGE"
echo "  ELF   : $ELF"
echo "============================================"
echo ""

# Pull image once (cached on subsequent runs)
docker pull "$DOCKER_IMAGE" --quiet

# Run QEMU inside the container.
# Semihosting exits QEMU; we capture the output and check the [SUITE] line.
QEMU_LOG=$(mktemp /tmp/minios_qemu_docker.XXXXXX)

docker run --rm \
    --volume "$PROJECT_ROOT/build:/minios/build:ro" \
    "$DOCKER_IMAGE" \
    /bin/sh -c "
        set -e
        DEBIAN_FRONTEND=noninteractive apt-get update -qq
        apt-get install -y --no-install-recommends $QEMU_PKG qemu-system-data 2>/dev/null | tail -1
        qemu-system-aarch64 \
            -machine virt \
            -cpu cortex-a53 \
            -m 512M \
            -nographic \
            -nodefaults \
            -serial stdio \
            -kernel /minios/build/kernel.elf \
            -semihosting-config enable=on,target=native \
            2>&1 || true
    " | tee "$QEMU_LOG"

echo ""

# Check result
if grep -qE "^\[SUITE\] [0-9]+ PASS, 0 FAIL" "$QEMU_LOG" 2>/dev/null; then
    SUITE_LINE=$(grep -E "^\[SUITE\]" "$QEMU_LOG")
    echo "============================================"
    echo "  RESULT: ALL TESTS PASSED  ($SUITE_LINE)"
    echo "============================================"
    rm -f "$QEMU_LOG"
    exit 0
else
    SUITE_LINE=$(grep -E "^\[SUITE\]" "$QEMU_LOG" 2>/dev/null || echo "(no [SUITE] line)")
    echo "============================================"
    echo "  RESULT: TESTS FAILED  ($SUITE_LINE)"
    echo "============================================"
    rm -f "$QEMU_LOG"
    exit 1
fi
