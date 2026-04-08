#!/usr/bin/env bash
# MiniOS unified test runner
#
# This is the only supported test script.
# Default run executes all host UT/CT/IT suites (~179 total including one expected-fail gap case).
# Optional: pass --with-qemu to also run QEMU suites.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR"

RUN_QEMU=0
if [[ "${1:-}" == "--with-qemu" ]]; then
	RUN_QEMU=1
fi

echo "============================================"
echo "  MiniOS Unified Test Runner"
echo "============================================"
echo ""

echo ">>> [1/3] Host passing suites"
make -C "$PROJECT_ROOT/tests/host" clean
make -C "$PROJECT_ROOT/tests/host" test
echo ""

echo ">>> [2/3] Expected-fail gap suite"
set +e
make -C "$PROJECT_ROOT/tests/host" test_expected_fail >/tmp/minios_expected_fail.log 2>&1
GAP_EXIT=$?
set -e

if [[ $GAP_EXIT -eq 0 ]]; then
	echo "[FAIL] Expected-fail gap suite unexpectedly passed."
	cat /tmp/minios_expected_fail.log
	exit 1
fi

if grep -q "Expected 2 Was 3" /tmp/minios_expected_fail.log; then
	echo "[PASS] Expected-fail behavior preserved (shell quote parsing gap)."
else
	echo "[FAIL] Expected-fail suite failed for an unexpected reason."
	cat /tmp/minios_expected_fail.log
	exit 1
fi

echo ""
if [[ $RUN_QEMU -eq 1 ]]; then
	echo ">>> [3/3] QEMU suites"
	make -C "$PROJECT_ROOT" TEST=1 test_qemu
	echo ""
else
	echo ">>> [3/3] QEMU suites skipped (use --with-qemu to include)"
	echo ""
fi

echo "============================================"
echo "  SUMMARY"
echo "  - Host passing suites: PASS"
echo "  - Expected-fail gap suite: PASS (intentional fail verified)"
if [[ $RUN_QEMU -eq 1 ]]; then
	echo "  - QEMU suites: executed"
else
	echo "  - QEMU suites: skipped"
fi
echo "  - Approx host test cases covered: 179"
echo "============================================"
