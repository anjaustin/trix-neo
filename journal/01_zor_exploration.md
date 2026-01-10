# ZOR/TriX Exploration - 10 Jan 2026

## Executive Summary

TriX is a neural architecture based on a radical hypothesis:

> **Computation is frozen geometry. Learning is navigation.**

The core claim: most of what neural networks "learn" is structure that could be discovered once and frozen forever. Only routing - which structure to invoke when - needs to be task-specific.

---

## The Core Concepts

### 1. Ternary Weights {-1, 0, +1}
- Not a compression of continuous weights, but the natural alphabet for signed sparse influence
- +1 = push up, -1 = push down, 0 = skip (sparse)
- Eliminates multiplication: `y = W @ x` becomes `y = sum(x where w=+1) - sum(x where w=-1)`
- Just addition and subtraction

### 2. Frozen Shapes
Shapes are pure polynomials that ARE computation, not approximations:
```
XOR(a, b) = a + b - 2ab
AND(a, b) = ab
OR(a, b)  = a + b - ab
NOT(a)    = 1 - a
```
On binary inputs {0, 1}, these equal bitwise operations exactly.

### 3. Gradient Truth
- Rejects the Straight-Through Estimator (STE)
- STE lies to the optimizer by pretending quantization didn't happen
- Gradient Truth: gradients flow only to genuinely learnable parameters
- Frozen things don't receive gradients because they don't need them

### 4. Content-Addressable Routing
- Inputs are routed to shapes based on content, not position
- The routing layer is the ONLY learned component
- Selection via signature matching (Hamming distance, dot product)
- The "Commitment Principle": select one path, don't blend

---

## The Existence Proof: Frozen 6502

A complete 6502 CPU ALU:
- **16 frozen shapes** (ADD, SUB, AND, OR, XOR, shifts, rotates, etc.)
- **0 learned parameters at inference**
- **~2,500 parameters during training** (routing layer)
- **326 bytes deployed** (or 1.5KB executable)
- **100% accuracy** - not approximate, exact

Comparison:
- Standard neural network: ~100,000 parameters, ~99% accuracy
- TriX: 326 bytes, 100% accuracy

---

## The Architecture Stack

```
Layer 1: trix.nn (PyTorch)     - Training with gradients
Layer 2: trix.native (Pure C)  - Inference, 1.5 GOP/s
Layer 3: Binary Shapes         - Frozen polynomials, 117 GB/s
Layer 4: GILLIES Vulkan        - GPU compute, 19B ops/sec
```

Same shapes at all layers. Only execution substrate changes.

---

## Project Structure in ZOR

```
/workspace/ZOR/
├── src/trix/
│   ├── nn/                    # PyTorch modules
│   │   ├── gradient_truth.py  # Gradient Truth FFN
│   │   ├── frozen_shapes.py   # Shape definitions
│   │   ├── frozen_6502.py     # 6502 neural network
│   │   ├── octave.py          # Multi-resolution routing
│   │   ├── hierarchical.py    # Hierarchical routing
│   │   ├── providence.py      # Unified architecture
│   │   └── sparse_lookup.py   # Sparse lookup FFN
│   │
│   └── native/                # Self-hosted operations
│       ├── ops/               # C library + Python bindings
│       ├── vulkan/            # GILLIES GPU runtime
│       └── frozen.py          # Native frozen shapes
│
├── trixc/                     # Pure C implementation
│   ├── include/               # C headers (shapes.h, apu.h, etc.)
│   ├── examples/              # Progressive C examples
│   ├── forge/                 # Compiler and tooling
│   └── platforms/             # Raspberry Pi, etc.
│
├── experiments/               # Standalone experiments
│   └── f6502_package/         # Frozen 6502 standalone
│
├── onramp/                    # Guided learning journey
├── tests/                     # 2,186 tests
└── docs/                      # Documentation
```

---

## Key Design Principles

### The Paradigm Shift
| Traditional Neural Networks | TriX |
|----------------------------|------|
| Learn parameters that approximate a function | Learn routing through pre-existing functions |
| Weights are the knowledge | Weights are the terrain; routing is the knowledge |
| Training finds the right numbers | Training finds the right paths |
| The model IS the trained parameters | The model is the routing table INTO frozen parameters |

### The Split: Frozen vs Learned
| Component | Learns? | Gradients |
|-----------|---------|-----------|
| Shapes | NO | Frozen polynomials |
| Routing | YES | Real dot products |
| Scales | YES | Real multiplication |

### Two Modes, Same Architecture
- **Deterministic** (6502): Hard routing, exact, temperature = 0
- **Generative** (LLM): Soft routing, temperature > 0, sampling

---

## MVP Candidates for Rebuild

Based on exploration, the MVP should focus on:

1. **Core shapes** - The frozen polynomial primitives (XOR, AND, OR, NOT, full adder)
2. **Ternary matmul** - Addition/subtraction only, no multiplication
3. **Content-addressed routing** - Hamming distance selection
4. **The 6502 proof** - Minimal working example of the paradigm

### Files Most Relevant to MVP
- `src/trix/nn/frozen_shapes.py` - Shape definitions
- `src/trix/nn/gradient_truth.py` - Gradient Truth FFN
- `src/trix/nn/frozen_6502.py` - 6502 network
- `trixc/examples/` - Progressive C examples
- `experiments/frozen_6502_standalone.c` - Pure C existence proof

---

## The Lincoln Manifold Method

The project uses a structured problem-solving methodology:
1. **RAW** - Unfiltered brain dump
2. **NODES** - Extract key points and tensions
3. **REFLECT** - Find patterns, resolve tensions
4. **SYNTHESIZE** - Produce final artifact

This is documented in `docs/LINCOLN_MANIFOLD_METHOD_v2.md`.

---

## Open Questions

1. How many shapes does language/vision need? (6502 needs 16)
2. Can shapes be composed? Small shapes making big shapes?
3. What about recurrence and memory?
4. Training efficiency - does frozen structure help or hurt convergence?
5. Where is the boundary between "deterministic computation" and "truly fuzzy tasks"?

---

## Next Steps for Our Rebuild

1. Define the minimal shape library
2. Implement ternary matmul (no multiplication)
3. Implement content-addressed routing
4. Build the 6502 existence proof
5. Establish test invariants

The goal: **The simplest possible implementation that proves the paradigm.**
