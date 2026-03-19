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

This generates:
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

## Using the C API

```c
#include "gesture_recognizer.h"
#include <stdio.h>

int main() {
    // Initialize the chip
    trix_chip_t* chip = trix_init();
    
    // Input pattern (512-bit / 64 bytes)
    uint8_t input[64] = {0x00, 0x01, 0x02, /* ... */};
    
    // Run inference
    trix_result_t result = trix_infer(chip, input);
    
    // Check result
    printf("Recognized: %s (distance: %d)\n", 
           result.label, result.distance);
    
    // Cleanup
    trix_free(chip);
    return 0;
}
```

## Performance Benchmarks

| Platform | Throughput | Notes |
|----------|------------|-------|
| Apple M4 | 235 GOP/s | NEON I8MM optimized |
| Intel x86 | 45 GOP/s | AVX2 optimized |
| ARM64 generic | 178 GOP/s | NEON SDOT |

## Next Steps

- [Architecture Overview](Architecture.md) - Understand TriX's design
- [Toolchain Guide](Toolchain.md) - Deep dive into the code generator
- [Deployment Guide](Deployment.md) - Docker and production deployment
- [API Reference](API-Reference.md) - Complete API documentation

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