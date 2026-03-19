#!/usr/bin/env python3
"""
trix_demo.py - TriX Demo Application

Demonstrates the full TriX pipeline:
1. Load a .trix spec file
2. Run inference on sample inputs
3. Display results

Usage:
    python trix_demo.py --chip model.trix --input 000102...
    python trix_demo.py --interactive model.trix
    python trix_demo.py --benchmark model.trix
"""

import argparse
import sys
import os

# Check if we have the C extension, otherwise use mock
try:
    import trix_cffi

    HAS_CEXT = True
except ImportError:
    HAS_CEXT = False
    print("Note: C extension not found, using Python simulation")


class TriXDemo:
    """Demo application for TriX runtime."""

    def __init__(self, chip_path):
        self.chip_path = chip_path
        self.chip = None

    def load(self):
        """Load the chip specification."""
        if not os.path.exists(self.chip_path):
            print(f"Error: File not found: {self.chip_path}")
            return False

        if HAS_CEXT:
            self.chip = trix_cffi.load(self.chip_path)
            print(f"✓ Loaded: {self.chip.name} v{self.chip.version}")
            print(f"  Signatures: {self.chip.num_signatures}")
            print(f"  State bits: {self.chip.state_bits}")
        else:
            # Simulate loading
            print(f"✓ Loaded: {os.path.basename(self.chip_path)}")
            print("  (Python simulation mode)")

        return True

    def infer(self, hex_input):
        """Run inference on hex input."""
        if not self.chip and not HAS_CEXT:
            print("Error: No chip loaded")
            return None

        # Parse hex input
        data = bytes.fromhex(hex_input)

        # Pad or truncate to 64 bytes
        if len(data) < 64:
            data = data + b"\x00" * (64 - len(data))
        elif len(data) > 64:
            data = data[:64]

        if HAS_CEXT:
            result = trix_cffi.infer(self.chip, data)
            return {
                "match": result.match,
                "distance": result.distance,
                "label": result.label,
                "threshold": result.threshold,
            }
        else:
            # Simulate inference
            # Calculate simple "distance" based on non-zero bytes
            distance = sum(1 for b in data if b != 0) // 2

            return {
                "match": 0 if distance < 32 else -1,
                "distance": distance,
                "label": "simulated_match" if distance < 32 else None,
                "threshold": 32,
            }

    def run_interactive(self):
        """Run interactive mode."""
        print("\n" + "=" * 60)
        print("  TriX Interactive Mode")
        print("  Type hex patterns (64 bytes = 128 hex chars)")
        print("  Type 'quit' to exit")
        print("=" * 60 + "\n")

        while True:
            try:
                hex_input = input("Enter pattern (hex): ").strip()

                if hex_input.lower() in ("quit", "q", "exit"):
                    print("Goodbye!")
                    break

                if not hex_input:
                    continue

                result = self.infer(hex_input)

                if result:
                    if result["match"] >= 0:
                        print(f"  ✓ Match: {result['label']}")
                        print(
                            f"    Distance: {result['distance']} / {result['threshold']}"
                        )
                    else:
                        print(f"  ✗ No match")
                        print(f"    Closest distance: {result['distance']}")

            except KeyboardInterrupt:
                print("\nGoodbye!")
                break
            except EOFError:
                break

    def run_benchmark(self, iterations=1000):
        """Run benchmark."""
        print(f"\nRunning benchmark ({iterations} iterations)...")

        # Generate random test input
        import random

        test_input = "".join(random.choice("0123456789abcdef") for _ in range(128))

        import time

        start = time.time()

        for _ in range(iterations):
            self.infer(test_input)

        elapsed = time.time() - start
        throughput = iterations / elapsed

        print(f"\nResults:")
        print(f"  Iterations: {iterations}")
        print(f"  Time: {elapsed:.3f}s")
        print(f"  Throughput: {throughput:.1f} inferences/sec")

    def run_demo(self):
        """Run demo with built-in test cases."""
        print("\n" + "=" * 60)
        print("  TriX Demo - Built-in Test Cases")
        print("=" * 60 + "\n")

        # Demo cases based on the chip
        test_cases = [
            (
                "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
                "First signature",
            ),
            (
                "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
                "All ones",
            ),
            (
                "0000000000000000000000000000000000000000000000000000000000000000",
                "All zeros",
            ),
        ]

        for hex_input, description in test_cases:
            print(f"\nTest: {description}")
            print(f"  Input: {hex_input[:32]}...")

            result = self.infer(hex_input)

            if result:
                if result["match"] >= 0:
                    print(f"  ✓ Match: {result['label']}")
                    print(f"    Distance: {result['distance']}/{result['threshold']}")
                else:
                    print(f"  ✗ No match (distance: {result['distance']})")


def main():
    parser = argparse.ArgumentParser(
        description="TriX Demo Application",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --chip model.trix --demo
  %(prog)s --chip model.trix --input 000102030405...
  %(prog)s --chip model.trix --interactive
  %(prog)s --chip model.trix --benchmark
        """,
    )

    parser.add_argument("--chip", "-c", required=True, help="Path to .trix file")
    parser.add_argument(
        "--input", "-i", help="Hex input pattern (64 bytes = 128 hex chars)"
    )
    parser.add_argument(
        "--interactive", action="store_true", help="Run interactive mode"
    )
    parser.add_argument("--benchmark", action="store_true", help="Run benchmark")
    parser.add_argument("--demo", action="store_true", help="Run built-in demo")

    args = parser.parse_args()

    # Create demo app
    demo = TriXDemo(args.chip)

    # Load chip
    if not demo.load():
        sys.exit(1)

    # Run requested mode
    if args.interactive:
        demo.run_interactive()
    elif args.benchmark:
        demo.run_benchmark()
    elif args.demo:
        demo.run_demo()
    elif args.input:
        result = demo.infer(args.input)
        if result:
            if result["match"] >= 0:
                print(f"\n✓ Match: {result['label']}")
                print(f"  Distance: {result['distance']} / {result['threshold']}")
            else:
                print(f"\n✗ No match")
                print(f"  Distance: {result['distance']}")
    else:
        # Default: run demo
        demo.run_demo()


if __name__ == "__main__":
    main()
