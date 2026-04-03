#!/usr/bin/env python3
"""
minios_cli.py — Interactive Command Line Interface for MiniOS.

Connects to the MiniOS SFU server and provides a shell-like experience
for inference, model management, and diagnostics.
"""

import sys
import cmd
import time
import argparse
import numpy as np
from sfu_client import SFUClient

class MiniOSShell(cmd.Cmd):
    intro = 'MiniOS SFU CLI — Type help or ? to list commands.\n'
    prompt = '(minios) '

    def __init__(self, host, port, timeout, retries, debug):
        super().__init__()
        try:
            self.client = SFUClient(host=host, port=port, timeout=timeout, 
                                   retries=retries, debug=debug)
        except Exception as e:
            print(f"Failed to connect to miniOS at {host}:{port}: {e}")
            sys.exit(1)

    # --- Diagnostics ---

    def do_ping(self, arg):
        """ping [count]
        Send PING packets and measure round-trip time. Default count is 4."""
        count = 1
        if arg:
            try:
                count = int(arg)
            except ValueError:
                print("Error: count must be an integer.")
                return

        print(f"PING {self.client.host}:{self.client.port}...")
        rtts = []
        for i in range(count):
            try:
                rtt = self.client.ping()
                rtts.append(rtt)
                print(f"  [{i+1}/{count}] reply from miniOS: time={rtt:.2f} ms")
            except Exception as e:
                print(f"  [{i+1}/{count}] request timed out: {e}")
        
        if rtts:
            print(f"\n--- {self.client.host} statistics ---")
            print(f"avg={sum(rtts)/len(rtts):.2f}ms min={min(rtts):.2f}ms max={max(rtts):.2f}ms")

    def do_status(self, arg):
        """status
        Show server status and active model."""
        try:
            model = self.client.cmd("GET_MODEL")
            rtt = self.client.ping()
            print(f"Status       : ONLINE")
            print(f"Active Model : {model}")
            print(f"Latency (RTT): {rtt:.2f} ms")
        except Exception as e:
            print(f"Status       : OFFLINE ({e})")

    def do_watch(self, arg):
        """watch
        Real-time latency monitor. Press Ctrl+C to stop."""
        print("Starting real-time monitor. Press Ctrl+C to exit.")
        try:
            while True:
                try:
                    rtt = self.client.ping()
                    print(f"[{time.strftime('%H:%M:%S')}] RTT: {rtt:.2f} ms")
                except Exception as e:
                    print(f"[{time.strftime('%H:%M:%S')}] TIMEOUT: {e}")
                time.sleep(1)
        except KeyboardInterrupt:
            print("\nStopped.")

    # --- Model Management ---

    def do_models(self, arg):
        """models
        List available models in MiniOS storage."""
        try:
            resp = self.client.cmd("LIST_MODELS")
            print("\nAvailable models in MiniOS:")
            print("---------------------------")
            if not resp.strip():
                print("  (no models found)")
            else:
                for line in resp.strip().split('\n'):
                    print(f"  - {line}")
            print("---------------------------")
        except Exception as e:
            print(f"Error listing models: {e}")

    def do_use(self, arg):
        """use <model_name>
        Switch the active inference model."""
        if not arg:
            print("Usage: use <model_name>")
            return
        
        try:
            print(f"Switching to model: {arg}...")
            resp = self.client.cmd(f"SELECT_MODEL {arg}")
            print(f"Success. Active model is now: {resp}")
        except Exception as e:
            print(f"Error switching model: {e}")

    # --- Inference ---

    def do_infer(self, arg):
        """infer <f1> <f2> ...
        Run inference on the provided space-separated float values."""
        if not arg:
            print("Usage: infer <f1> <f2> ...")
            return

        try:
            floats = [float(x) for x in arg.split()]
            inputs = np.array(floats, dtype=np.float32)
            
            print(f"Inference input: {inputs}")
            t0 = time.perf_counter()
            output = self.client.infer(inputs)
            dt = (time.perf_counter() - t0) * 1000.0
            
            print(f"Inference result: {output}")
            print(f"Elapsed time: {dt:.2f} ms")
        except ValueError:
            print("Error: inputs must be valid numbers.")
        except Exception as e:
            print(f"Inference failed: {e}")

    def do_bench(self, arg):
        """bench [count]
        Benchmark inference performance. Default count is 100."""
        count = 100
        if arg:
            try:
                count = int(arg)
            except ValueError:
                print("Error: count must be an integer.")
                return

        # Use dummy input (4 floats)
        inputs = np.array([1.0, 2.0, 3.0, 4.0], dtype=np.float32)
        print(f"Benchmarking {count} inferences...")
        
        latencies = []
        try:
            for i in range(count):
                t0 = time.perf_counter()
                self.client.infer(inputs)
                latencies.append((time.perf_counter() - t0) * 1000.0)
                if (i+1) % 10 == 0:
                    print(f"  Processed {i+1}/{count}...")
            
            latencies = np.array(latencies)
            print(f"\n--- Benchmark Results ---")
            print(f"Count : {count}")
            print(f"Avg   : {np.mean(latencies):.3f} ms")
            print(f"Min   : {np.min(latencies):.3f} ms")
            print(f"Max   : {np.max(latencies):.3f} ms")
            print(f"P99   : {np.percentile(latencies, 99):.3f} ms")
        except Exception as e:
            print(f"\nBenchmark failed: {e}")

    # --- CLI Control ---

    def do_exit(self, arg):
        """Exit the MiniOS CLI."""
        print("Goodbye.")
        return True

    def do_quit(self, arg):
        """Exit the MiniOS CLI."""
        return self.do_exit(arg)

    def emptyline(self):
        # Do nothing on empty line
        pass

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MiniOS SFU CLI Tool")
    parser.add_argument("--host", default="127.0.0.1", help="MiniOS IP address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9000, help="SFU UDP port (default: 9000)")
    parser.add_argument("--timeout", type=float, default=1.0, help="Response timeout in seconds")
    parser.add_argument("--retries", type=int, default=3, help="Number of retries")
    parser.add_argument("--debug", action="store_true", help="Enable protocol debug prints")
    parser.add_argument("command", nargs="*", help="Optional one-shot command to run")

    args = parser.parse_args()

    shell = MiniOSShell(host=args.host, port=args.port, 
                       timeout=args.timeout, retries=args.retries, 
                       debug=args.debug)

    if args.command:
        # One-shot mode
        cmd_str = " ".join(args.command)
        shell.onecmd(cmd_str)
        shell.do_exit(None)
    else:
        # Interactive mode
        try:
            shell.cmdloop()
        except KeyboardInterrupt:
            shell.do_exit(None)
