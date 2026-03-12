#!/usr/bin/env bash
# run_tests.sh — MiniOS CI test runner
#
# Runs host tests then QEMU tests.
# Exits 0 if and only if all tests pass.
#
# Usage: bash scripts/run_tests.sh
#        (from project root or anywhere)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

HOST_PASS=0
HOST_FAIL=0
QEMU_RESULT=0

echo "============================================"
echo "  MiniOS Test Runner"
echo "============================================"
echo ""

# ---- Host Tests ----
echo ">>> Running host tests..."
echo ""

if ! (cd "$PROJECT_ROOT/tests/host" && make test 2>&1); then
    echo ""
    echo "[FAIL] Host tests failed."
    HOST_FAIL=1
else
    echo ""
    echo "[PASS] Host tests passed."
    HOST_PASS=1
fi

echo ""

# ---- QEMU Tests (only if QEMU is available) ----
if command -v qemu-system-aarch64 &>/dev/null; then
    echo ">>> Running QEMU tests (this may take 30-60s)..."
    echo ""

    QEMU_LOG=$(mktemp /tmp/minios_qemu_test.XXXXXX)
    set +e
    (cd "$PROJECT_ROOT" && make TEST=1 test_qemu 2>&1) | tee "$QEMU_LOG"
    QEMU_EXIT=${PIPESTATUS[0]}
    set -e

    # Check for SUITE summary line
    if grep -qE "^\[SUITE\] [0-9]+ PASS, 0 FAIL" "$QEMU_LOG" 2>/dev/null; then
        echo ""
        echo "[PASS] QEMU tests passed."
        QEMU_RESULT=0
    else
        echo ""
        SUITE_LINE=$(grep -E "^\[SUITE\]" "$QEMU_LOG" 2>/dev/null || true)
        if [ -n "$SUITE_LINE" ]; then
            echo "[FAIL] QEMU tests failed: $SUITE_LINE"
        else
            echo "[FAIL] QEMU tests did not complete (no [SUITE] line found)."
        fi
        QEMU_RESULT=1
    fi
    rm -f "$QEMU_LOG"
else
    echo ">>> qemu-system-aarch64 not found — skipping QEMU tests."
    echo "    Install QEMU to run the full test suite."
    QEMU_RESULT=0  # Not a failure if QEMU isn't installed
fi

echo ""
echo "============================================"

if [ "$HOST_FAIL" -eq 0 ] && [ "$QEMU_RESULT" -eq 0 ]; then
    echo "  ALL TESTS PASSED"
    echo "============================================"
    exit 0
else
    echo "  SOME TESTS FAILED"
    [ "$HOST_FAIL" -ne 0 ] && echo "  - Host tests:  FAILED"
    [ "$QEMU_RESULT" -ne 0 ] && echo "  - QEMU tests:  FAILED"
    echo "============================================"
    exit 1
fi
