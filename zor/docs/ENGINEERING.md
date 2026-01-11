# TriX Engineering Specification

*A complete, buildable specification for trusted deterministic computation.*

---

## Overview

TriX is a computational architecture where:
- **Shapes are frozen** — mathematical functions that never change
- **Routing is learned** — XOR-based signatures determine which shapes fire
- **Computation is deterministic** — same input always produces same output

This document provides everything needed to implement TriX in software or hardware.

---

## Core Data Structures

### The Resonance State

```c
// The 512-bit resonance state
// Can be viewed as flat array or 8×8×8 cube
typedef union {
    uint64_t flat[8];           // 8 × 64-bit words = 512 bits
    uint8_t  cube[8][8][8];     // 8×8×8 cube of bits (packed)
    uint8_t  bytes[64];         // 64 bytes flat
} ResonanceState;
```

**Size:** 64 bytes. Fits in a single cache line.

**Properties:**
- Total bits: 512
- Possible states: 2^512 (≈ 10^154)
- Cube dimensions: 8 × 8 × 8

### Signatures

```c
// A signature identifies a shape's activation region
typedef struct {
    uint64_t pattern[8];    // 512-bit pattern
    uint16_t threshold;     // Hamming distance threshold (0-512)
} Signature;
```

**Size:** 66 bytes per signature.

### Shapes

```c
// A shape is a frozen function with a signature
typedef struct {
    Signature sig;                      // When to activate
    float (*compute)(float* inputs);    // What to compute
    const char* name;                   // For debugging
} Shape;
```

---

## The 5 Primes

All shapes derive from these five irreducible operations:

### ADD — Accumulation
```c
float prime_add(float a, float b) {
    return a + b;
}
```

### MUL — Scaling
```c
float prime_mul(float a, float b) {
    return a * b;
}
```

### EXP — Growth
```c
float prime_exp(float x) {
    return expf(x);
}
```

### MAX — Selection
```c
float prime_max(float a, float b) {
    return (a > b) ? a : b;
}
```

### CONST — Anchoring
```c
float prime_const(float c) {
    return c;  // Returns fixed value
}
```

---

## Derived Shapes

Shapes are compositions of Primes. Examples:

### XOR (Logic)
```c
// XOR(a,b) = a + b - 2ab
// Exact for binary inputs {0, 1}
float shape_xor(float a, float b) {
    return a + b - 2.0f * a * b;
}
```

### AND (Logic)
```c
// AND(a,b) = a * b
float shape_and(float a, float b) {
    return a * b;
}
```

### OR (Logic)
```c
// OR(a,b) = a + b - ab
float shape_or(float a, float b) {
    return a + b - a * b;
}
```

### ReLU (Activation)
```c
// ReLU(x) = max(0, x)
float shape_relu(float x) {
    return prime_max(0.0f, x);
}
```

### Sigmoid (Activation)
```c
// Sigmoid(x) = 1 / (1 + exp(-x))
float shape_sigmoid(float x) {
    return 1.0f / (1.0f + prime_exp(-x));
}
```

### Softplus (Activation)
```c
// Softplus(x) = log(1 + exp(x))
float shape_softplus(float x) {
    return logf(1.0f + prime_exp(x));
}
```

See `PERIODIC_TABLE.md` for the complete taxonomy of ~30 shapes.

---

## The Routing Equation

The Zit detector determines which shapes fire:

```
Zit = popcount(S ⊕ input) < θ
```

### Components

| Symbol | Meaning | Implementation |
|--------|---------|----------------|
| S | Resonance state | 512-bit register |
| input | Query pattern | 512-bit input |
| ⊕ | XOR | Bitwise XOR |
| popcount | Bit count | Count 1-bits |
| θ | Threshold | Integer 0-512 |
| Zit | Activation | Boolean |

### Implementation

```c
// Compute Hamming distance between two 512-bit patterns
int hamming_distance(const uint64_t* a, const uint64_t* b) {
    int distance = 0;
    for (int i = 0; i < 8; i++) {
        distance += __builtin_popcountll(a[i] ^ b[i]);
    }
    return distance;
}

// Check if a shape should activate
bool zit_detect(const ResonanceState* state,
                const Signature* sig) {
    int dist = hamming_distance(state->flat, sig->pattern);
    return dist < sig->threshold;
}
```

### Performance

On modern x86-64 with POPCNT instruction:
- XOR: 1 cycle per 64 bits
- POPCNT: 1 cycle per 64 bits
- Total: ~16 cycles for 512-bit Hamming distance

---

## The Resonance Update

State evolves via XOR accumulation:

```
S_new = S_old ⊕ input
```

### Implementation

```c
void resonance_update(ResonanceState* state, const uint64_t* input) {
    for (int i = 0; i < 8; i++) {
        state->flat[i] ^= input[i];
    }
}
```

### Properties

- **Reversible:** `S_old = S_new ⊕ input`
- **Associative:** Order of XORs doesn't matter for final state
- **Self-inverse:** `S ⊕ S = 0`

---

## The Inference Loop

Complete inference in one function:

```c
typedef struct {
    ResonanceState state;
    Shape* shapes;
    int n_shapes;
} TriXEngine;

// Find matching shape and compute result
int trix_infer(TriXEngine* engine,
               const uint64_t* input,
               float* output) {

    // Check each shape for activation
    for (int s = 0; s < engine->n_shapes; s++) {
        if (zit_detect(&engine->state, &engine->shapes[s].sig)) {
            // Shape fires - compute output
            *output = engine->shapes[s].compute(/* inputs */);

            // Update resonance state
            resonance_update(&engine->state, input);

            return s;  // Return which shape fired
        }
    }

    // No shape matched
    resonance_update(&engine->state, input);
    return -1;
}
```

---

## 3D Cube Operations

The 512-bit state can be viewed as an 8×8×8 cube:

### Coordinate Mapping

```c
// Convert (x, y, z) to bit index
int cube_index(int x, int y, int z) {
    return z * 64 + y * 8 + x;  // 0-511
}

// Get bit at (x, y, z)
bool cube_get(const ResonanceState* s, int x, int y, int z) {
    int idx = cube_index(x, y, z);
    int word = idx / 64;
    int bit = idx % 64;
    return (s->flat[word] >> bit) & 1;
}

// Set bit at (x, y, z)
void cube_set(ResonanceState* s, int x, int y, int z, bool val) {
    int idx = cube_index(x, y, z);
    int word = idx / 64;
    int bit = idx % 64;
    if (val) {
        s->flat[word] |= (1ULL << bit);
    } else {
        s->flat[word] &= ~(1ULL << bit);
    }
}
```

### Neighborhood Access

```c
// 6 face-adjacent neighbors
int neighbors_face[6][3] = {
    {-1, 0, 0}, {1, 0, 0},  // x-axis
    {0, -1, 0}, {0, 1, 0},  // y-axis
    {0, 0, -1}, {0, 0, 1}   // z-axis
};

// Get local neighborhood (3×3×3 = 27 bits)
uint32_t cube_neighborhood(const ResonanceState* s, int cx, int cy, int cz) {
    uint32_t hood = 0;
    int bit = 0;
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int x = (cx + dx + 8) % 8;  // Wrap around
                int y = (cy + dy + 8) % 8;
                int z = (cz + dz + 8) % 8;
                if (cube_get(s, x, y, z)) {
                    hood |= (1U << bit);
                }
                bit++;
            }
        }
    }
    return hood;
}
```

### Hierarchical Access

```c
// Level 0: 8×8×8 = 512 individual bits
// Level 1: 4×4×4 = 64 blocks of 8 bits (2×2×2 each)
// Level 2: 2×2×2 = 8 octants of 64 bits (4×4×4 each)
// Level 3: 1×1×1 = 1 block of 512 bits (whole cube)

// Get octant (0-7) popcount
int octant_popcount(const ResonanceState* s, int octant) {
    int ox = (octant & 1) * 4;
    int oy = ((octant >> 1) & 1) * 4;
    int oz = ((octant >> 2) & 1) * 4;

    int count = 0;
    for (int z = oz; z < oz + 4; z++) {
        for (int y = oy; y < oy + 4; y++) {
            for (int x = ox; x < ox + 4; x++) {
                if (cube_get(s, x, y, z)) count++;
            }
        }
    }
    return count;
}
```

---

## Memory Layout

### Recommended Layout

```
┌─────────────────────────────────────────────────────────────┐
│                    TriX Memory Map                          │
├─────────────────────────────────────────────────────────────┤
│  Offset   │  Size    │  Contents                            │
├───────────┼──────────┼──────────────────────────────────────┤
│  0x0000   │  64 B    │  Resonance State (512 bits)          │
│  0x0040   │  66 B    │  Shape 0 Signature                   │
│  0x0086   │  66 B    │  Shape 1 Signature                   │
│  ...      │  ...     │  ...                                 │
│  0x0800   │  varies  │  Shape function pointers             │
│  0x1000   │  varies  │  Application data                    │
└─────────────────────────────────────────────────────────────┘
```

### Cache Optimization

- Resonance state: 64 bytes = 1 cache line
- Keep hot signatures in first few cache lines
- Align signatures to 64-byte boundaries for SIMD

---

## SIMD Optimization

### AVX2 (256-bit)

```c
#include <immintrin.h>

int hamming_distance_avx2(const uint64_t* a, const uint64_t* b) {
    __m256i va1 = _mm256_loadu_si256((__m256i*)a);
    __m256i va2 = _mm256_loadu_si256((__m256i*)(a + 4));
    __m256i vb1 = _mm256_loadu_si256((__m256i*)b);
    __m256i vb2 = _mm256_loadu_si256((__m256i*)(b + 4));

    __m256i xor1 = _mm256_xor_si256(va1, vb1);
    __m256i xor2 = _mm256_xor_si256(va2, vb2);

    // Use lookup table for popcount (AVX2 doesn't have native popcount)
    // Or fall back to scalar popcount
    // ... (implementation details)

    return total_popcount;
}
```

### AVX-512 (512-bit)

```c
#include <immintrin.h>

int hamming_distance_avx512(const uint64_t* a, const uint64_t* b) {
    __m512i va = _mm512_loadu_si512(a);
    __m512i vb = _mm512_loadu_si512(b);
    __m512i vxor = _mm512_xor_si512(va, vb);
    return _mm512_popcnt_epi64(vxor);  // AVX-512 VPOPCNTDQ
}
```

**Performance:** Single cycle for 512-bit XOR + popcount on AVX-512.

---

## Hardware Implementation

### Gate Counts (Estimated)

| Component | Gates | Notes |
|-----------|-------|-------|
| 512-bit XOR | 512 | 1 gate per bit |
| 512-bit Popcount | ~3,000 | Adder tree |
| Threshold Compare | ~20 | 9-bit comparator |
| Single Zit Detector | ~3,500 | XOR + popcount + compare |
| 30 Zit Detectors | ~50,000 | Parallel bank |
| Control Logic | ~3,000 | State machine, mux |
| **Total NGP** | **~53,000** | Complete processor |

### Verilog Sketch

```verilog
module zit_detector (
    input  wire [511:0] state,
    input  wire [511:0] signature,
    input  wire [8:0]   threshold,
    output wire         activate
);
    wire [511:0] xor_result;
    wire [9:0]   popcount;

    // XOR
    assign xor_result = state ^ signature;

    // Popcount (tree of adders)
    popcount_512 pc (.in(xor_result), .count(popcount));

    // Threshold compare
    assign activate = (popcount < {1'b0, threshold});

endmodule
```

### FPGA Targets

| FPGA | Resources | Fit? |
|------|-----------|------|
| Xilinx Artix-7 (XC7A35T) | 33K LUTs | Yes |
| Intel Cyclone V (5CEBA4) | 49K ALMs | Yes |
| Lattice iCE40 UP5K | 5K LUTs | Partial (fewer shapes) |

---

## Build Phases

### Phase 1: Software Core (Week 1-2)

```
zor/
├── include/trixc/
│   ├── state.h        # ResonanceState type
│   ├── primes.h       # 5 Prime functions
│   ├── shapes.h       # Derived shapes
│   ├── zit.h          # Zit detector
│   └── engine.h       # Inference engine
├── src/
│   ├── primes.c
│   ├── shapes.c
│   ├── zit.c
│   └── engine.c
├── test/
│   └── test_core.c
└── Makefile
```

Deliverable: Working inference engine, all tests pass.

### Phase 2: Shape Library (Week 3-4)

Implement all ~30 Geocadesia shapes:
- Logic Kingdom (AND, OR, XOR, NAND, NOR, XNOR)
- Activation Kingdom (ReLU, Sigmoid, Tanh, Softplus, etc.)
- Arithmetic Kingdom (ADD, SUB, MUL, DIV, etc.)
- Comparison Kingdom (MIN, MAX, CLIP, etc.)
- Transcendental Kingdom (EXP, LOG, SIN, COS, etc.)

Deliverable: Complete shape library with tests.

### Phase 3: Training Pipeline (Week 5-6)

```c
// Signature learning via XOR accumulation
void train_signature(Signature* sig,
                     uint64_t** positive_examples,
                     int n_positive) {
    // Initialize to zero
    memset(sig->pattern, 0, 64);

    // XOR accumulate positive examples
    for (int i = 0; i < n_positive; i++) {
        for (int j = 0; j < 8; j++) {
            sig->pattern[j] ^= positive_examples[i][j];
        }
    }

    // Set threshold based on distribution
    sig->threshold = compute_optimal_threshold(sig, ...);
}
```

Deliverable: Training loop, signature optimization.

### Phase 4: Benchmarks (Week 7-8)

- Task: Pattern classification
- Baseline: Decision tree, small neural net
- Metrics: Accuracy, latency, throughput, power

Deliverable: Benchmark results, comparison report.

### Phase 5: FPGA Prototype (Week 9-12)

- Target: Xilinx Artix-7
- Implement: Zit detector bank, shape evaluators
- Measure: Clock speed, utilization, power

Deliverable: Working FPGA bitstream, timing report.

---

## Performance Targets

### Software (x86-64)

| Metric | Target | Notes |
|--------|--------|-------|
| Zit detection | 10 ns | Per shape |
| Full inference | 300 ns | 30 shapes |
| Throughput | 3M inferences/sec | Single core |
| Memory | < 4 KB | State + signatures |

### Hardware (FPGA)

| Metric | Target | Notes |
|--------|--------|-------|
| Clock | 100 MHz | Conservative |
| Zit detection | 10 ns | Single cycle |
| Full inference | 10 ns | All shapes parallel |
| Throughput | 100M inferences/sec | |
| Power | < 1W | Edge-suitable |

### Hardware (ASIC)

| Metric | Target | Notes |
|--------|--------|-------|
| Clock | 1 GHz | Aggressive |
| Zit detection | 1 ns | |
| Throughput | 1B inferences/sec | |
| Power | < 100 mW | |
| Die area | < 1 mm² | 28nm |

---

## API Summary

```c
// === State Management ===
void state_init(ResonanceState* s);
void state_reset(ResonanceState* s);
void state_update(ResonanceState* s, const uint64_t* input);

// === Zit Detection ===
int  hamming_distance(const uint64_t* a, const uint64_t* b);
bool zit_detect(const ResonanceState* s, const Signature* sig);
int  zit_detect_bank(const ResonanceState* s, Shape* shapes, int n);

// === Shapes ===
float shape_eval(const Shape* shape, float* inputs);

// === Engine ===
void engine_init(TriXEngine* e, Shape* shapes, int n);
int  engine_infer(TriXEngine* e, const uint64_t* input, float* output);

// === Training ===
void signature_train(Signature* sig, uint64_t** examples, int n);
void signature_optimize_threshold(Signature* sig, ...);

// === Cube Operations (3D) ===
bool cube_get(const ResonanceState* s, int x, int y, int z);
void cube_set(ResonanceState* s, int x, int y, int z, bool val);
uint32_t cube_neighborhood(const ResonanceState* s, int x, int y, int z);
```

---

## Summary

TriX is a complete, buildable system:

| What | Specification |
|------|---------------|
| State | 512 bits = 64 bytes = 8×8×8 cube |
| Primitives | 5 Primes: ADD, MUL, EXP, MAX, CONST |
| Shapes | ~30 Geocadesia shapes |
| Routing | `popcount(S ⊕ input) < θ` |
| Update | `S = S ⊕ input` |
| Hardware | ~53K gates |

Everything is deterministic. Everything is verifiable. Everything fits in your head.

---

*"The entire system fits in a cache line."*

*"It's all in the reflexes."*
