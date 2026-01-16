# Soft Chips

*Portable frozen computation. Define once. Forge anywhere. Trust everywhere.*

---

## What Is a Soft Chip?

A **soft chip** is a self-contained unit of frozen computation that:

- Runs identically on any hardware (CPU, GPU, FPGA, ASIC)
- Produces deterministic output (same input → same output, always)
- Can be formally verified (proofs, not prayers)
- Fits in kilobytes (L1 cache resident)

```
┌─────────────────────────────────────────────────────────────┐
│                       SOFT CHIP                             │
│                                                             │
│   ┌─────────────────────────────────────────────────────┐   │
│   │              .trix Specification                    │   │
│   │                                                     │   │
│   │   • Shapes (from 5 Primes)                          │   │
│   │   • Signatures (routing patterns)                   │   │
│   │   • Thresholds (activation sensitivity)             │   │
│   │   • State layout (512-bit cube)                     │   │
│   │                                                     │   │
│   └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│              ┌────────────┼────────────┐                    │
│              ▼            ▼            ▼                    │
│         ┌────────┐  ┌──────────┐  ┌────────┐               │
│         │   C    │  │   NEON   │  │ Verilog│               │
│         │ (any)  │  │  (ARM)   │  │ (FPGA) │               │
│         └────────┘  └──────────┘  └────────┘               │
│              │            │            │                    │
│              └────────────┼────────────┘                    │
│                           │                                 │
│                    IDENTICAL OUTPUT                         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## The Spec Format (.trix)

Soft chips are defined in human-readable YAML:

```yaml
# gesture_detector.trix
softchip:
  name: gesture_detector
  version: 1.0.0
  description: Detects swipe and tap gestures from accelerometer data

state:
  bits: 512
  layout: cube  # 8×8×8 spatial organization

shapes:
  - xor       # Core routing
  - relu      # Activation
  - sigmoid   # Probability output

signatures:
  swipe_left:
    pattern: "base64:ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=="
    threshold: 64
    shape: sigmoid

  swipe_right:
    pattern: "base64:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz+/=="
    threshold: 64
    shape: sigmoid

  tap:
    pattern: "base64:+/==0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
    threshold: 48
    shape: relu

inference:
  mode: first_match   # Stop at first matching signature
  default: unknown    # Return if no match
```

### Spec Properties

| Field | Description |
|-------|-------------|
| `softchip.name` | Identifier for the soft chip |
| `softchip.version` | Semantic version |
| `state.bits` | Resonance state size (default: 512) |
| `state.layout` | Organization: `flat` or `cube` |
| `shapes` | List of frozen shapes to include |
| `signatures` | Named patterns with thresholds |
| `inference.mode` | `first_match`, `all_match`, or `weighted` |

---

## The Toolchain

### Commands

```
trix init <name>              Create new soft chip spec
trix forge <spec> [options]   Compile to target platform
trix verify <spec>            Generate verification report
trix trace <spec> --input=    Debug inference step-by-step
trix bench <spec>             Measure performance
```

### Forge Targets

| Target | Flag | Output |
|--------|------|--------|
| Portable C | `--target=c` | Pure C99, any platform |
| ARM NEON | `--target=neon` | NEON intrinsics for ARM64 |
| Intel AVX | `--target=avx` | AVX2/AVX-512 intrinsics |
| WebAssembly | `--target=wasm` | WASM with SIMD |
| Verilog | `--target=verilog` | RTL for FPGA/ASIC |

---

## Quick Start

### 1. Create a Soft Chip

```bash
$ trix init my_classifier

Created: my_classifier.trix
Edit this file to define your soft chip.
```

### 2. Define Signatures

Edit `my_classifier.trix` to add your signatures:

```yaml
signatures:
  class_a:
    pattern: "base64:..."  # 64 bytes, base64 encoded
    threshold: 64
  class_b:
    pattern: "base64:..."
    threshold: 64
```

### 3. Forge to Your Platform

```bash
$ trix forge my_classifier.trix --target=neon

Forging: my_classifier
Target: ARM NEON

Generated:
  my_classifier.c      (1.2 KB)
  my_classifier.h      (0.4 KB)
  Makefile             (0.3 KB)
```

### 4. Build and Run

```bash
$ cd output && make
$ ./my_classifier_test

Validation: PASS (all signatures verified)
Throughput: 47M inferences/sec
```

---

## Runtime API

The generated code provides a minimal C API:

```c
#include "my_classifier.h"

int main() {
    // Initialize soft chip
    trix_chip_t* chip = trix_init();

    // Prepare input (512 bits = 64 bytes)
    uint8_t input[64];
    // ... fill input from sensor/data ...

    // Infer — deterministic, fast
    trix_result_t result = trix_infer(chip, input);

    // Check result
    if (result.match >= 0) {
        printf("Matched: %s (distance=%d)\n",
               result.label, result.distance);
    } else {
        printf("No match (default: %s)\n", result.label);
    }

    // Cleanup
    trix_free(chip);
    return 0;
}
```

### API Reference

```c
// Types
typedef struct trix_chip trix_chip_t;

typedef struct {
    int match;          // Signature index (-1 if no match)
    int distance;       // Hamming distance to matched signature
    int threshold;      // Threshold that was used
    const char* label;  // Human-readable label
} trix_result_t;

// Functions
trix_chip_t* trix_init(void);
trix_result_t trix_infer(trix_chip_t* chip, const uint8_t input[64]);
void trix_free(trix_chip_t* chip);
void trix_reset(trix_chip_t* chip);  // Reset resonance state
```

---

## Verification

Soft chips can be formally verified:

```bash
$ trix verify my_classifier.trix

╭──────────────────────────────────────────────────────────────╮
│  Verification Report: my_classifier                          │
╰──────────────────────────────────────────────────────────────╯

Determinism:     ✓ PROVEN   (pure functions only)
Reproducibility: ✓ PROVEN   (platform-independent)
Memory Bound:    ✓ 64 bytes (fixed allocation)
Time Bound:      ✓ O(n)     (n = number of signatures)

Spec Hash:    sha256:a1b2c3d4...
Output Hash:  sha256:e5f6a7b8...

Status: VERIFIED
```

### What Verification Proves

| Property | Meaning |
|----------|---------|
| **Determinism** | Same input always produces same output |
| **Reproducibility** | Behavior identical across all platforms |
| **Memory Bound** | Fixed memory usage, no allocation |
| **Time Bound** | Predictable execution time |

---

## Tracing

Debug inference step-by-step:

```bash
$ trix trace my_classifier.trix --input=data.bin

Input: 512 bits from data.bin

Zit Detection:
  class_a: distance=89  threshold=64  ✗
  class_b: distance=42  threshold=64  ✓ MATCH

Shape Activation:
  class_b → sigmoid → 0.73

Result: class_b (margin: 22 bits)
```

---

## Architecture

### Internal Structure

```
┌─────────────────────────────────────────────────────────────┐
│                     Soft Chip Internals                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   RESONANCE STATE (64 bytes)                                │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  8×8×8 cube of bits, updated via XOR                │   │
│   │  S_new = S_old ⊕ input                              │   │
│   └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│                           ▼                                 │
│   ZIT DETECTOR BANK                                         │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  For each signature:                                │   │
│   │    distance = popcount(state ⊕ signature)           │   │
│   │    if distance < threshold: ACTIVATE                │   │
│   └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│                           ▼                                 │
│   SHAPE EVALUATION                                          │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  Activated signature's shape computes output        │   │
│   │  Shapes are frozen (XOR, ReLU, Sigmoid, etc.)       │   │
│   └─────────────────────────────────────────────────────┘   │
│                           │                                 │
│                           ▼                                 │
│   RESULT                                                    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### The Zit Equation

```
activate = popcount(state ⊕ signature) < threshold
```

| Symbol | Meaning |
|--------|---------|
| `state` | Current 512-bit resonance state |
| `signature` | Target pattern (512 bits) |
| `⊕` | XOR operation |
| `popcount` | Count of 1 bits |
| `threshold` | Activation sensitivity |

---

## Performance

### Typical Metrics

| Metric | Value | Notes |
|--------|-------|-------|
| State size | 64 bytes | Fits in L1 cache |
| Code size | 1-3 KB | Per soft chip |
| Inference time | 10-100 ns | Platform dependent |
| Throughput | 10-100M/sec | Single core |

### Platform-Specific

| Platform | Optimization | Throughput |
|----------|--------------|------------|
| x86-64 (AVX2) | 256-bit SIMD | ~80M/sec |
| ARM Cortex-A78 (NEON) | 128-bit SIMD | ~50M/sec |
| Raspberry Pi 4 | NEON | ~30M/sec |
| FPGA (100MHz) | Parallel | ~100M/sec |

---

## Use Cases

### Edge Inference
- Gesture recognition
- Keyword spotting
- Anomaly detection
- Sensor classification

### Safety-Critical Systems
- Medical devices
- Automotive
- Aerospace
- Industrial control

### Embedded Systems
- Microcontrollers
- IoT devices
- Wearables
- Smart sensors

### Verifiable Computing
- Blockchain/ZK proofs
- Audit trails
- Reproducible research
- Regulatory compliance

---

## Comparison

| Feature | Neural Networks | Soft Chips |
|---------|-----------------|------------|
| Determinism | No (floating point) | Yes (exact) |
| Verification | No | Yes |
| Size | MB-GB | KB |
| Dependencies | Runtime required | Zero |
| Debugging | Black box | Bit-level trace |
| Trust | "Usually works" | Proven |

---

## Limitations

Soft chips are NOT suitable for:

- Large language models
- Image generation
- Complex multi-step reasoning
- Tasks requiring >512 bits of state

Soft chips ARE suitable for:

- Bounded classification tasks
- Pattern matching
- Deterministic decision logic
- Edge deployment with trust requirements

---

## Summary

A soft chip is:

1. **Defined** in human-readable .trix YAML
2. **Forged** to any target platform
3. **Verified** for determinism and bounds
4. **Deployed** with zero dependencies
5. **Trusted** because behavior is proven

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   "Define once. Forge anywhere. Trust everywhere."          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

*"The soft chip is crystallized intent. Hardware is just the executor."*
