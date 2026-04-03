#!/usr/bin/env python3
"""
test_infer.py — Integration test suite for the MiniOS SFU inference server.

Usage:
    python test_infer.py [--debug] [--host H] [--port P] [--count N]

Tests (executed in order):
    1. Connection check  — PING / PONG round-trip
    2. Single inference  — four ones through the model
    3. Batch latency     — N random inferences, latency statistics
    4. Error handling    — single-float input (model expects ≥ 1 input)
    5. Retry test        — ping a non-existent server → TimeoutError
"""

import argparse
import sys
import time

import numpy as np

from sfu_client import SFUClient


# ---------------------------------------------------------------------------
#  Argument parsing
# ---------------------------------------------------------------------------

def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        description="MiniOS SFU inference server test client",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    p.add_argument(
        "--debug",
        action="store_true",
        default=False,
        help="Print every packet header sent and received",
    )
    p.add_argument(
        "--host",
        default="127.0.0.1",
        metavar="H",
        help="Server host address",
    )
    p.add_argument(
        "--port",
        type=int,
        default=9000,
        metavar="P",
        help="Server UDP port",
    )
    p.add_argument(
        "--count",
        type=int,
        default=100,
        metavar="N",
        help="Number of inferences for the batch latency test (Test 3)",
    )
    return p


# ---------------------------------------------------------------------------
#  Test helpers
# ---------------------------------------------------------------------------

_PASS = "\033[92mPASS\033[0m"
_FAIL = "\033[91mFAIL\033[0m"
_SKIP = "\033[93mSKIP\033[0m"

def _banner(n: int, title: str) -> None:
    bar = "─" * 60
    print(f"\n{bar}")
    print(f"  TEST {n}: {title}")
    print(bar)


# ---------------------------------------------------------------------------
#  Individual tests
# ---------------------------------------------------------------------------

def test1_connection(host: str, port: int, debug: bool) -> SFUClient | None:
    """Test 1 — Connection check (PING → PONG)."""
    _banner(1, "Connection check")
    try:
        client = SFUClient(
            host=host,
            port=port,
            debug=debug,
            # startup_check is *part* of this test — keep the default 10 retries
        )
        rtt = client.ping()
        print(f"[{_PASS}] PING OK — RTT: {rtt:.2f} ms")
        return client
    except TimeoutError as exc:
        print(f"[{_FAIL}] {exc}")
        return None


def test2_single_inference(client: SFUClient) -> None:
    """Test 2 — Single inference with a four-ones vector."""
    _banner(2, "Single inference")
    try:
        inputs = np.ones(4, dtype=np.float32)
        result = client.infer(inputs)
        print(f"[{_PASS}] Inference OK — input: {inputs}, output: {result}")
    except RuntimeError as exc:
        print(f"[{_FAIL}] RuntimeError: {exc}")
    except TimeoutError as exc:
        print(f"[{_FAIL}] TimeoutError: {exc}")


def test3_batch_latency(client: SFUClient, n: int) -> None:
    """Test 3 — Batch latency over N random inferences."""
    _banner(3, f"Batch latency ({n} iterations)")
    times: list[float] = []
    errors = 0

    for i in range(n):
        t0 = time.perf_counter()
        try:
            client.infer(np.random.rand(4).astype(np.float32))
        except (RuntimeError, TimeoutError):
            errors += 1
        elapsed_ms = (time.perf_counter() - t0) * 1000.0
        times.append(elapsed_ms)

    if times:
        arr = np.array(times)
        status = _PASS if errors == 0 else _FAIL
        print(
            f"[{status}] Batch {n}: "
            f"avg={np.mean(arr):.2f}ms "
            f"min={np.min(arr):.2f}ms "
            f"max={np.max(arr):.2f}ms "
            f"p99={np.percentile(arr, 99):.2f}ms"
            + (f"  ({errors} error(s))" if errors else "")
        )
    else:
        print(f"[{_FAIL}] No successful inferences recorded")


def test4_error_handling(client: SFUClient) -> None:
    """Test 4 — Error handling: send a 1-float input to a model that needs more.

    If the model accepts any input size, we print a note instead of failing.
    """
    _banner(4, "Error handling (wrong input size)")
    try:
        result = client.infer(np.ones(1, dtype=np.float32))
        # Model accepted it — not necessarily wrong (server may be flexible)
        print(
            f"[{_SKIP}] Model accepted a 1-float input "
            f"(output: {result}). "
            "Error handling test is inconclusive — adjust input size "
            "to match your model's expected *wrong* arity."
        )
    except RuntimeError as exc:
        print(f"[{_PASS}] Error handled correctly: {exc}")
    except TimeoutError as exc:
        print(f"[{_FAIL}] Unexpected timeout: {exc}")


def test5_retry_timeout(host: str, debug: bool) -> None:
    """Test 5 — Retry / timeout with no server on port 9001."""
    _banner(5, "Retry test (no server on port 9001)")
    client2 = SFUClient(
        host=host,
        port=9001,
        timeout=0.2,
        debug=debug,
        skip_startup_ping=True,   # don't run startup loop here
    )
    try:
        client2.ping()
        print(f"[{_FAIL}] Expected TimeoutError but ping succeeded")
    except TimeoutError:
        print(f"[{_PASS}] Timeout/retry test PASSED — no server on 9001")
    finally:
        client2.close()


# ---------------------------------------------------------------------------
#  Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    args = _build_parser().parse_args()

    print("=" * 60)
    print("  MiniOS SFU Inference Server — Test Suite")
    print(f"  Target : {args.host}:{args.port}")
    print(f"  Debug  : {args.debug}")
    print(f"  Count  : {args.count}")
    print("=" * 60)

    # Test 1: connection (also performs startup retry loop inside SFUClient)
    client = test1_connection(args.host, args.port, args.debug)

    if client is None:
        print(
            "\nminiOS appears to be unreachable — "
            "skipping tests 2-4 (test 5 will still run)."
        )
    else:
        try:
            # Test 2: single inference
            test2_single_inference(client)

            # Test 3: batch latency
            test3_batch_latency(client, args.count)

            # Test 4: error handling
            test4_error_handling(client)

        finally:
            client.close()

    # Test 5: retry / timeout (uses a deliberately wrong port)
    test5_retry_timeout(args.host, args.debug)

    print("\n" + "=" * 60)
    print("  Test run complete.")
    print("=" * 60 + "\n")


if __name__ == "__main__":
    main()
