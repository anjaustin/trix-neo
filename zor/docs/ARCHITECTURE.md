# TriX Architecture: From Theory to Metal

> "The 5 Primes are axioms. Everything else is derived."

This document describes the complete TriX architecture, from mathematical foundations to hardware execution.

## Table of Contents

1. [Philosophy](#philosophy)
2. [The 5 Primes](#the-5-primes)
3. [Layer Architecture](#layer-architecture)
4. [Memory Model](#memory-model)
5. [Execution Model](#execution-model)
6. [Evolution Architecture](#evolution-architecture)
7. [Deployment Targets](#deployment-targets)

---

## Philosophy

### The Core Insight

Traditional neural networks are **black boxes**: billions of parameters learned through gradient descent, producing outputs through opaque matrix multiplications.

TriX inverts this:

| Traditional | TriX |
|-------------|------|
| Learned operations | **Frozen operations** |
| Opaque weights | **Learned routing** |
| Statistical inference | **Deterministic computation** |
| GPU required | **Runs on anything** |

### The Three Pillars

1. **Frozen Shapes**: Mathematical operations that never change
2. **Learned Routing**: Signatures determine which shapes fire
3. **Verifiable Output**: Same input → same output, always

---

## The 5 Primes

All computation in TriX derives from 5 primitive operations:

### ADD
```
ADD(a, b) = a + b
```
The foundation of accumulation. Used in:
- Bias addition
- State mixing
- Reduction operations

### MUL
```
MUL(a, b) = a × b
```
The foundation of scaling and gating. Used in:
- Weight application
- Gating mechanisms
- Attention scores

### EXP
```
EXP(a) = e^a
```
The foundation of non-linearity. Used in:
- Sigmoid: `1 / (1 + EXP(-x))`
- Softmax: `EXP(x) / Σ EXP(x)`
- Decay: `EXP(-t/τ)`

### MAX
```
MAX(a, b) = max(a, b)
```
The foundation of selection. Used in:
- ReLU: `MAX(0, x)`
- Attention masking
- Argmax operations

### CONST
```
CONST(c) = c
```
The foundation of parameterization. Used in:
- Frozen weights
- Biases
- Time constants

### Derived Shapes

All other operations are compositions:

| Shape | Composition |
|-------|-------------|
| SUB | `ADD(a, MUL(-1, b))` |
| DIV | `MUL(a, EXP(-LOG(b)))` |
| XOR | `ADD(ADD(a, b), MUL(-2, MUL(a, b)))` |
| AND | `MUL(a, b)` |
| OR | `ADD(ADD(a, b), MUL(-1, MUL(a, b)))` |
| NOT | `ADD(1, MUL(-1, a))` |
| Sigmoid | `DIV(1, ADD(1, EXP(MUL(-1, a))))` |
| Tanh | `DIV(ADD(EXP(a), MUL(-1, EXP(MUL(-1, a)))), ADD(EXP(a), EXP(MUL(-1, a))))` |
| ReLU | `MAX(0, a)` |
| Softmax | `DIV(EXP(a), SUM(EXP(x)))` |

---

## Layer Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Layer 5: Applications                                                   │
│   Gesture recognition, anomaly detection, control systems               │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 4: Networks                                                       │
│   CfC networks, MLPs, attention mechanisms                              │
│   Files: cfc_shapes.h                                                   │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 3: ONNX Operations                                                │
│   MatMul, Softmax, LayerNorm, Attention                                 │
│   Files: onnx_shapes.h                                                  │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 2: Logic & Arithmetic                                             │
│   XOR, AND, OR, Full Adder, Ripple Adder                                │
│   Files: shapes.h                                                       │
├─────────────────────────────────────────────────────────────────────────┤
│ Layer 1: The 5 Primes                                                   │
│   ADD, MUL, EXP, MAX, CONST                                             │
│   Files: apu.h                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

### Layer 1: APU (Arithmetic Processing Unit)

The foundational layer providing:
- Precision types (FP32, FP16, FP8, FP4)
- Basic operations
- Precision conversion

```c
// apu.h
typedef enum {
    TRIX_FP32,
    TRIX_FP16,
    TRIX_FP8,
    TRIX_FP4
} trix_precision_t;

float trix_add(float a, float b);
float trix_mul(float a, float b);
float trix_exp(float a);
float trix_max(float a, float b);
```

### Layer 2: Logic Shapes

Boolean operations expressed as polynomials:

```c
// shapes.h
// XOR as polynomial: a + b - 2ab
static inline float trix_shape_xor(float a, float b) {
    return a + b - 2.0f * a * b;
}

// Full adder: sum = a ⊕ b ⊕ c, carry = (a ∧ b) ∨ ((a ⊕ b) ∧ c)
void trix_shape_full_adder(float a, float b, float c, float* sum, float* carry);
```

### Layer 3: ONNX Operations

Standard neural network operations:

```c
// onnx_shapes.h
void trix_onnx_matmul(const float* a, const float* b, float* c, int M, int N, int K);
void trix_onnx_softmax(const float* x, float* out, int n);
void trix_onnx_layer_norm(const float* x, const float* gamma, const float* beta,
                          float* out, int n, float eps);
```

### Layer 4: CfC Networks

Liquid neural network cells:

```c
// cfc_shapes.h
void trix_cfc_cell(const float* x, const float* h_prev, float dt,
                   const CfCParams* params, float* h_new);
void trix_cfc_forward(const float* inputs, int seq_len, float dt,
                      const CfCParams* params, const float* h_init,
                      float* outputs, float* h_final);
```

---

## Memory Model

### Frozen vs. Mutable

TriX distinguishes between two memory categories:

| Category | Location | Mutability | Example |
|----------|----------|------------|---------|
| **Frozen** | ROM / .rodata | Never changes | Weights, shapes |
| **State** | RAM / Stack | Per-inference | Hidden state, registers |

### Frozen Chip Memory Layout

```
┌────────────────────────────────────────┐
│ .rodata (Frozen)                       │
│ ├── W_gate[hidden × concat]            │
│ ├── b_gate[hidden]                     │
│ ├── W_cand[hidden × concat]            │
│ ├── b_cand[hidden]                     │
│ ├── tau[hidden] or decay[hidden]       │
│ ├── W_out[hidden × output]             │
│ └── b_out[output]                      │
├────────────────────────────────────────┤
│ Stack (State)                          │
│ ├── h[hidden]     (current state)      │
│ ├── h_new[hidden] (next state)         │
│ └── temp[...]     (intermediates)      │
└────────────────────────────────────────┘
```

### ShapeGraph Memory Layout

```
┌────────────────────────────────────────┐
│ nodes[N] — ShapeNode array             │
│ ├── [16 bytes] Node 0                  │
│ ├── [16 bytes] Node 1                  │
│ └── ...                                │
├────────────────────────────────────────┤
│ registers[M] — float array             │
│ ├── [0..input_count-1]   Inputs        │
│ ├── [...]                 State        │
│ ├── [...]                 Temp         │
│ └── [output_start..end]   Outputs      │
└────────────────────────────────────────┘
```

### Cache Considerations

| Component | Size | L1 Fit (32KB) |
|-----------|------|---------------|
| 4→8→3 CfC weights | 228 bytes | 140 instances |
| 74-node ShapeGraph | 1,496 bytes | 21 instances |
| LiquidGenome | 13,784 bytes | 2 instances |

**Design principle**: Keep hot data in L1. A single CfC chip fits 140× in L1 cache.

---

## Execution Model

### Frozen Chip Execution

```
Input ──► [Frozen Weights] ──► [State Update] ──► Output
              (ROM)              (Stack)

1. Load input into registers
2. Execute frozen operations (no branches on data)
3. Update state
4. Optionally project to output
```

### ShapeGraph Execution

```
for each node in nodes:
    if node.flags & ACTIVE:
        a = registers[node.in_a_idx]
        b = registers[node.in_b_idx]
        result = execute_opcode(node.opcode, a, b, node.value)
        registers[node.out_idx] = result
```

**Properties**:
- No data-dependent branches (opcode switch is on frozen data)
- Predictable memory access pattern
- SIMD-friendly (batch nodes of same opcode)

### Determinism Guarantees

1. **No random operations**: All computation is deterministic
2. **No floating-point non-determinism**: Careful use of associativity
3. **No external state**: Only inputs and internal state
4. **No data-dependent branching**: Same path for all inputs

---

## Evolution Architecture

### Two Representations

#### LiquidGenome (Struct-based)

```c
typedef struct {
    // Dimensions
    uint8_t input_dim, hidden_dim, output_dim;

    // Genes
    float tau[MAX_HIDDEN];
    float W_gate[MAX_HIDDEN * MAX_CONCAT];
    float b_gate[MAX_HIDDEN];
    float W_cand[MAX_HIDDEN * MAX_CONCAT];
    float b_cand[MAX_HIDDEN];
    float W_out[MAX_HIDDEN * MAX_OUTPUT];
    float b_out[MAX_OUTPUT];

    // Metadata
    float fitness;
    uint32_t id, generation, parent_a, parent_b;
} LiquidGenome;
```

**Pros**: Semantic clarity, full CfC semantics
**Cons**: 13KB per genome, 1.15M mutations/sec
**Use case**: Traditional GA, research

#### ShapeGraph (Binary)

```c
typedef struct {
    uint8_t  opcode;
    uint8_t  flags;
    uint16_t out_idx;
    uint32_t in_a_idx;
    uint32_t in_b_idx;
    float    value;
} ShapeNode;  // 16 bytes, aligned
```

**Pros**: 65.8M mutations/sec, zero-copy, mmap-able
**Cons**: Less semantic structure
**Use case**: High-speed evolution, embedded deployment

### Evolution Pipeline

```
┌─────────────────────────────────────────────────────────────────────────┐
│ Generation 0                                                            │
│ ┌───┐ ┌───┐ ┌───┐ ┌───┐                                                 │
│ │ G │ │ G │ │ G │ │ G │  ... (population)                               │
│ └───┘ └───┘ └───┘ └───┘                                                 │
└───────────────┬─────────────────────────────────────────────────────────┘
                │ Evaluate fitness
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Fitness scores: [0.3, 0.7, 0.5, 0.9, ...]                               │
└───────────────┬─────────────────────────────────────────────────────────┘
                │ Selection (tournament, elite)
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Selected parents: [G3, G1, G3, G0, ...]                                 │
└───────────────┬─────────────────────────────────────────────────────────┘
                │ Crossover + Mutation
                ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Generation 1                                                            │
│ ┌───┐ ┌───┐ ┌───┐ ┌───┐                                                 │
│ │ G'│ │ G'│ │ G'│ │ G'│  ... (new population)                           │
│ └───┘ └───┘ └───┘ └───┘                                                 │
└─────────────────────────────────────────────────────────────────────────┘
```

### Mutation Operators

| Operator | LiquidGenome | ShapeGraph |
|----------|--------------|------------|
| **Weight perturbation** | `w += gaussian(0, σ)` | `node.value += δ` |
| **Topology change** | `input_map[i] = random()` | `node.in_a_idx = random()` |
| **Time constant** | `tau[i] *= exp(gaussian())` | `node.value *= factor` |
| **Structural** | N/A | `node.opcode = random()` |

### Selection Strategies

| Strategy | Description | When to Use |
|----------|-------------|-------------|
| **Tournament** | Best of K random | Balanced exploration/exploitation |
| **Elite** | Top K preserved | Prevent fitness regression |
| **Roulette** | Proportional to fitness | Early exploration |

---

## Deployment Targets

### Target: Pure C

```bash
trix forge chip.trix --target=c
```

- Portable to any C99 compiler
- ~1-3KB code size
- 10-100M inferences/sec on modern CPU

### Target: ARM NEON

```bash
trix forge chip.trix --target=neon
```

- SIMD intrinsics for ARM64
- 4× throughput for vector operations
- Ideal for Raspberry Pi, Jetson, mobile

### Target: Intel AVX2

```bash
trix forge chip.trix --target=avx2
```

- 256-bit SIMD for x86-64
- 8× throughput for float operations
- Ideal for servers, workstations

### Target: WebAssembly

```bash
trix forge chip.trix --target=wasm
```

- Runs in browser
- WASM SIMD for performance
- Ideal for edge inference, demos

### Target: Verilog

```bash
trix forge chip.trix --target=verilog
```

- RTL for FPGA/ASIC synthesis
- Combinational logic from frozen shapes
- Sub-microsecond latency
- Ideal for real-time control, trading

---

## Performance Summary

| Metric | Value | Notes |
|--------|-------|-------|
| CfC step | 206 ns | 4→8→2 network |
| Throughput | 4.86 M steps/sec | Single core |
| Genome mutation | 1.15 M/sec | LiquidGenome |
| Binary mutation | 65.8 M/sec | ShapeGraph |
| Evolution | 2,785 gen/sec | Pop=64 |
| Chip memory | 228 bytes | Weights only |
| L1 fit | 140 chips | 32KB cache |

---

## Design Principles

1. **Freeze what's learned, route what changes**
   - Weights are frozen into ROM
   - Only state variables change at runtime

2. **Everything is a shape**
   - All operations derive from the 5 Primes
   - Complex behaviors emerge from composition

3. **Memory is topology**
   - Data layout determines performance
   - Cache locality > algorithmic complexity

4. **Mutation is integer manipulation**
   - Zero-copy evolution
   - No serialization, no allocation

5. **Determinism is non-negotiable**
   - Same input → same output, always
   - Platform-independent results

---

Created by: Tripp + Claude
Date: January 2026
