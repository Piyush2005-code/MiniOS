#!/usr/bin/env python3
import cmd
import sys
import argparse
import numpy as np
from sfu_client import SFUClient

class SFUClientCLI(cmd.Cmd):
    intro = "Welcome to the MiniOS SFU Networking CLI.\\nType help or ? to list commands.\\n"
    prompt = "(minios-sfu) "

    def __init__(self, host, port, debug_mode):
        super().__init__()
        self.host = host
        self.port = port
        self.debug_mode = debug_mode
        self.client = None

    def preloop(self):
        print(f"Connecting to MiniOS at {self.host}:{self.port}...")
        try:
            self.client = SFUClient(host=self.host, port=self.port, debug=self.debug_mode)
            print("Connected successfully. Type 'help' for commands.")
        except Exception as e:
            print(f"Failed to connect: {e}")
            sys.exit(1)

    def do_ping(self, arg):
        """ping\\nSend a PING to MiniOS and measure the round-trip time."""
        try:
            rtt = self.client.ping()
            print(f"[PING] Success! RTT = {rtt:.2f} ms")
        except Exception as e:
            print(f"Ping failed: {e}")

    def do_list(self, arg):
        """list\\nList all ONNX models available on the MiniOS NVRAM."""
        try:
            models = self.client.cmd("LIST_MODELS")
            print(models)
        except Exception as e:
            print(f"Command failed: {e}")

    def do_exec(self, arg):
        """exec <command>\\nExecute an arbitrary text command via the SFU CMD mechanism."""
        if not arg:
            print("Usage: exec <command>")
            return
        try:
            response = self.client.cmd(arg)
            print(response)
        except Exception as e:
            print(f"Command failed: {e}")

    def do_infer(self, arg):
        """infer <numbers>\\nRun an INFER_REQUEST using the provided comma/space separated float numbers.\\nExample: infer 1.0, 2.5, 3.1"""
        if not arg:
            print("Usage: infer <n1, n2, ...>")
            return
        
        try:
            # Parse commas or spaces
            clean_arg = arg.replace(',', ' ')
            nums = [float(x) for x in clean_arg.split()]
            inputs = np.array(nums, dtype=np.float32)
            print(f"Sending input: {inputs}")
            
            output = self.client.infer(inputs)
            print(f"Output received: {output}")
        except ValueError:
            print("Invalid input format. Please provide numbers separated by spaces or commas.")
        except Exception as e:
            print(f"Inference failed: {e}")

    def do_quit(self, arg):
        """quit\\nExit the CLI."""
        print("Closing connection...")
        return True

    def do_exit(self, arg):
        """exit\\nExit the CLI."""
        return self.do_quit(arg)

    def postloop(self):
        if self.client:
            self.client.close()

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="MiniOS SFU Interactive CLI")
    parser.add_argument("--host", default="127.0.0.1", help="MiniOS IP address (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=9000, help="MiniOS UDP port (default: 9000)")
    parser.add_argument("--debug", action="store_true", help="Enable debug packet logging")
    
    args = parser.parse_args()
    
    cli = SFUClientCLI(args.host, args.port, args.debug)
    try:
        cli.cmdloop()
    except KeyboardInterrupt:
        print("\\nExiting...")
        if cli.client:
            cli.client.close()
