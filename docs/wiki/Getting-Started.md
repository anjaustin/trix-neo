# Getting Started with TriX

This guide will help you get TriX up and running in minutes.

## Prerequisites

- **C compiler**: GCC, Clang, or MSVC
- **CMake**: 3.16 or higher
- **Linux/macOS/Windows**: Full cross-platform support
- **Optional**: ARM NEON for optimized inference on ARM

## Quick Install

### From Source

```bash
# Clone the repository
git clone https://github.com/triximpulse/trix.git
cd trix

# Create build directory
mkdir build && cd build

# Configure and build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)

# Run tests
ctest --output-on-failure

# Install (optional)
sudo make install
```

### Using Docker

```bash
# Pull the official image
docker pull trix/toolchain:latest

# Run the toolchain
docker run --rm -v $(pwd):/workspace trix/toolchain:latest --help
```

## Your First TriX Application

### Step 1: Create a `.trix` Spec File

Create a file called `gesture.trix`:

```yaml
softchip:
  name: gesture_recognizer
  version: 1.0.0
  description: Hand gesture recognition

state:
  bits: 512
  layout: cube

shapes:
  - xor
  - and
  - or
  - sigmoid

signatures:
  thumbs_up:
    pattern: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f
    threshold: 32
    shape: xor
  thumbs_down:
    pattern: 202122232425262728292a2b2c2d2e2f303132333435363738393a3b3c3d3e3f
    threshold: 32
    shape: xor
  open_palm:
    pattern: 404142434445464748494a4b4c4d4e4f505152535455565758595a5b5c5d5e5f
    threshold: 40
    shape: and

inference:
  mode: first_match
  default: unknown
```

### Step 2: Generate C Code

```bash
./trix gesture.trix --target=c
```

This generates files in the `output/` directory:
- `gesture_recognizer.h` - Header file
- `gesture_recognizer.c` - Implementation
- `gesture_recognizer_test.c` - Test harness
- `Makefile` - Build system

### Step 3: Compile and Run

```bash
cd output
make
./gesture_recognizer_test
```

Expected output:
```
╭──────────────────────────────────────────────────────────────╮
│  Soft Chip Test: gesture_recognizer                          │
╰──────────────────────────────────────────────────────────────╯

Signature Validation:
─────────────────────
  ✓ thumbs_up: PASS (distance=0)
  ✓ thumbs_down: PASS (distance=0)
  ✓ open_palm: PASS (distance=0)

Results: 3 passed, 0 failed
```

## Using Generated Code

The TriX toolchain generates self-contained C code that you integrate into your project.

### Header Usage

```c
#include "gesture_recognizer.h"

// Use the generated chip
void recognize_gesture(const uint8_t input[64]) {
    // Call the generated inference function
    int match = gesture_recognizer_match(input);
    
    if (match >= 0) {
        const char* labels[] = {"thumbs_up", "thumbs_down", "open_palm"};
        printf("Recognized: %s\n", labels[match]);
    } else {
        printf("No match\n");
    }
}
```

### Generated API

The code generator creates:

| Function | Description |
|----------|-------------|
| `chip_match(input)` | Returns signature index or -1 |
| `chip_distance(input)` | Returns Hamming distance to best match |
| `chip_get_label(index)` | Returns human-readable label |
| `chip_get_threshold(index)` | Returns threshold for signature |

### Configuration

Edit the generated `Makefile` to adjust optimization levels:

```makefile
CFLAGS = -O3 -march=native -ffast-math
```

## Performance Benchmarks

| Platform | Throughput | Notes |
|----------|------------|-------|
| Apple M4 | 235 GOP/s | NEON I8MM optimized |
| Intel x86 | 45 GOP/s | AVX2 optimized |
| ARM64 generic | 178 GOP/s | NEON SDOT |

## Build Targets

### C (Portable)
```bash
./trix spec.trix --target=c
```
Works everywhere. Uses portable popcount.

### NEON (ARM)
```bash
./trix spec.trix --target=neon
```
Uses ARM NEON SDOT instructions for 16x speedup.

### AVX2 (x86)
```bash
./trix spec.trix --target=avx2
```
Intel/AMD with AVX2.

### WebAssembly
```bash
./trix spec.trix --target=wasm
```
For browser/edge deployment.

## Next Steps

- [Architecture Overview](Architecture.md) - Understand TriX's design
- [Toolchain Guide](Toolchain.md) - Deep dive into the code generator
- [API Reference](API-Reference.md) - Toolchain API documentation
- [Security](Security.md) - Security model

## Common Issues

### "arm_neon.h not found"

Install ARM cross-compilation tools:
```bash
# Ubuntu
sudo apt-get install gcc-arm-linux-gnueabihf

# macOS
brew install arm-none-eabi-gcc
```

### "math.h linking error"

Ensure you're linking the math library:
```bash
gcc -o myapp myapp.c -lm
```

## Getting Help

- **Documentation**: See the docs/ directory
- **Issues**: Report bugs at https://github.com/triximpulse/trix/issues
- **Discussions**: Ask questions at https://github.com/triximpulse/trix/discussions