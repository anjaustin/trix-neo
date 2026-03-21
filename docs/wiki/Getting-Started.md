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

### Step 2: Generate C Code (Alternative Approach)

```bash
./trix gesture.trix --target=c
```

This generates self-contained C code in the `output/` directory.

### Step 3: Use the Runtime Library (Recommended)

```c
#include <trixc/runtime.h>
#include <stdio.h>

int main() {
    // Load the chip
    int error = 0;
    trix_chip_t* chip = trix_load("gesture.trix", &error);
    if (!chip) {
        fprintf(stderr, "Failed to load chip: %d\n", error);
        return 1;
    }
    
    // Get chip info
    const trix_chip_info_t* info = trix_info(chip);
    printf("Loaded: %s v%s (%d signatures)\n", 
           info->name, info->version, info->num_signatures);
    
    // Run inference
    uint8_t input[64] = {0x00, 0x01, 0x02, /* ... 64 bytes ... */};
    trix_result_t result = trix_infer(chip, input);
    
    if (result.match >= 0) {
        printf("Recognized: %s (distance: %d)\n", 
               result.label, result.distance);
    } else {
        printf("No match (distance: %d)\n", result.distance);
    }
    
    // Cleanup
    trix_chip_free(chip);
    return 0;
}
```

Compile and run:
```bash
gcc -o myapp myapp.c -I/path/to/trix/zor/include -L/path/to/trix/build -ltrix_runtime -lm
./myapp
```

## Runtime API

### Loading Chips

```c
// Load from .trix spec file
trix_chip_t* chip = trix_load("chip.trix", &error);

// Get chip metadata
const trix_chip_info_t* info = trix_info(chip);
printf("Name: %s\n", info->name);
printf("Signatures: %d\n", info->num_signatures);
printf("State bits: %d\n", info->state_bits);

// Get memory footprint
size_t footprint = trix_memory_footprint(chip);
```

### Running Inference

```c
// Single inference (first match)
uint8_t input[64] = {/* your 64-byte input */};
trix_result_t result = trix_infer(chip, input);

if (result.match >= 0) {
    printf("Match: %s (index=%d, distance=%d)\n", 
           result.label, result.match, result.distance);
} else {
    printf("No match\n");
}

// Get all matching signatures
trix_result_t matches[10];
int num = trix_infer_all(chip, input, matches, 10);
for (int i = 0; i < num; i++) {
    printf("  %s: distance=%d\n", matches[i].label, matches[i].distance);
}
```

### Querying Signatures

```c
// Get label for signature index
const char* label = trix_label(chip, 0);

// Get threshold
int threshold = trix_threshold(chip, 0);

// Get signature pattern (read-only)
const uint8_t* pattern = trix_signature(chip, 0);

// Get shape type
int shape = trix_shape(chip, 0);  // 0=xor, 1=and, etc.
```

### Cleanup

```c
trix_chip_free(chip);  // Safe to call with NULL
```

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

## Performance Benchmarks

| Platform | Throughput | Notes |
|----------|------------|-------|
| Apple M4 | 235 GOP/s | NEON I8MM optimized |
| Intel x86 | 45 GOP/s | AVX2 optimized |
| ARM64 generic | 178 GOP/s | NEON SDOT |

## Linear Layer Chips (Learned Encoders)

For real-world signals (vibration, audio, sensor data), raw byte patterns rarely land close to a fixed signature. **Linear layers** let you train a neural encoder that maps arbitrary inputs into the 512-bit Hamming space, then match against learned prototypes.

### How It Works

1. **Train** a binary neural network (BNN) encoder in Python (`tools/python/train.py`)
2. **Export** int8-quantized weights + `.trix` spec with `tools/python/export.py`
3. **Load** the chip with `trix_load()` — linear layers are applied automatically before Hamming matching

### Example `.trix` with Linear Layers

```yaml
softchip:
  name: bearing_fault
  version: 1.0.0
state:
  bits: 512
linear:
  encoder_layer0:
    input_dim: 64
    output_dim: 256
    weights: bearing_fault_layer0.bin
  encoder_layer1:
    input_dim: 256
    output_dim: 512
    weights: bearing_fault_layer1.bin
signatures:
  normal:
    pattern: <128 hex chars>
    threshold: 35
  inner_race:
    pattern: <128 hex chars>
    threshold: 30
inference:
  mode: first_match
  default: unknown
```

When `trix_infer()` is called on this chip, the runtime:
1. Passes the 64-byte input through `encoder_layer0` (int8 MatVec, 64→256)
2. Clamps activations to `[-127, 127]` (matching training)
3. Passes through `encoder_layer1` (int8 MatVec, 256→512)
4. Sign-binarizes the output (z > 0 → 1) into a 512-bit code
5. Computes Hamming distance against each signature

### Training Pipeline Quick Start

```bash
# Install dependencies
pip install -r tools/python/requirements.txt

# Train on synthetic data (4-class demo)
python3 tools/python/train.py --dataset synthetic --epochs 50 --out-dir chips/

# Train on real data (CWRU bearing dataset)
python3 tools/python/train.py --dataset cwru --data-dir /path/to/cwru/ --out-dir chips/

# Run end-to-end validation
TRIX_LIB_PATH=build python3 tools/python/test_pipeline.py
```

See [Toolchain Guide](Toolchain.md) for the full `linear:` spec format and dimension constraints.

## Next Steps

- [Architecture Overview](Architecture.md) - Understand TriX's design
- [Toolchain Guide](Toolchain.md) - Deep dive into the code generator
- [API Reference](API-Reference.md) - Full API documentation
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