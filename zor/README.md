# TriX Core

**Frozen shapes. Pure C. No dependencies. Just math.**

```
Computation is frozen geometry.
Learning is navigation.
The shapes are eternal.
```

---

## What Is This?

The theoretical core of TriX — ternary neural computation with frozen shapes.

**No Python. No PyTorch. No dependencies.**

This directory contains:
- **Theoretical foundations** in `docs/`
- **Core header files** in `include/` (when implemented)

For the practical toolchain, see [`../tools/`](../tools/).

---

## The Core Insight

Traditional neural networks learn everything — including mathematical truths that don't need to be learned.

TriX freezes the computation. XOR is always `a + b - 2ab`. That's not an opinion. We just compile it.

```c
// This is exact on binary inputs {0, 1}
float xor(float a, float b) {
    return a + b - 2.0f * a * b;
}
```

---

## Documentation

The theoretical foundations are documented in [`docs/`](docs/):

| Document | Description |
|----------|-------------|
| [The 5 Primes](docs/THE_5_PRIMES.md) | The irreducible atoms: ADD, MUL, EXP, MAX, CONST |
| [Periodic Table](docs/PERIODIC_TABLE.md) | Taxonomy of ~30 frozen shapes by function and complexity |
| [Addressable Intelligence](docs/ADDRESSABLE_INTELLIGENCE.md) | The paradigm: data addresses computation |
| [Engineering](docs/ENGINEERING.md) | Complete buildable specification: data structures, APIs, hardware |
| [Soft Chips](docs/SOFT_CHIPS.md) | Portable frozen computation units |
| [CfC + EntroMorph](docs/CFC_ENTROMORPH.md) | Liquid neural networks + evolution engine |
| [Zit Detection](docs/ZIT_DETECTION.md) | **Anomaly detection via phase-locked tracking** |
| [Architecture](docs/ARCHITECTURE.md) | Complete system architecture from theory to metal |

---

## Core Frozen Shapes

| Shape | Formula | Description |
|-------|---------|-------------|
| XOR | `a + b - 2ab` | Exclusive or |
| AND | `a * b` | Logical and |
| OR | `a + b - ab` | Logical or |
| NOT | `1 - a` | Logical not |
| NAND | `1 - ab` | Not-and |
| NOR | `1 - a - b + ab` | Not-or |
| XNOR | `1 - a - b + 2ab` | Exclusive nor |
| ReLU | `max(0, x)` | Rectified linear unit |
| Sigmoid | `1 / (1 + e^-x)` | Logistic activation |

These are the building blocks. Any deterministic computation can be composed from frozen shapes.

---

## The Zit Equation

The routing mechanism that selects which shapes fire:

```
Zit = popcount(S XOR input) < threshold
```

| Symbol | Meaning |
|--------|---------|
| S | 512-bit resonance state |
| input | 512-bit query |
| XOR | Bitwise exclusive or |
| popcount | Count of 1 bits |
| threshold | Activation sensitivity |

---

## Philosophy

1. **Frozen shapes** — Mathematical truths compiled to native code
2. **Routing is the only learning** — Which shape for which input
3. **Precision is a shape** — FP4, FP8, FP16, FP32 conversions are frozen
4. **No runtime** — Your model becomes a binary

---

## Examples

Run `make examples` then explore:

| Example | Description |
|---------|-------------|
| `01_hello_xor` | XOR as a polynomial |
| `02_logic_gates` | Complete truth tables |
| `03_full_adder` | Arithmetic from logic |
| `04_activations` | Neural network activations |
| `05_matmul` | Matrix multiplication |
| `06_tiny_mlp` | 2-layer perceptron |
| `07_cfc_demo` | **CfC liquid neural network** |
| `08_evolution_demo` | **EntroMorph evolution** |

---

## Header Files

| Header | Description |
|--------|-------------|
| `apu.h` | Precision types and basic ops |
| `shapes.h` | Logic shapes (XOR, AND, adders) |
| `onnx_shapes.h` | ONNX ops (MatMul, Softmax, LayerNorm) |
| `cfc_shapes.h` | **CfC cells and networks** |
| `entromorph.h` | **Evolution engine** |
| `shapefabric.h` | **Binary executable graphs** |

---

## Quick Start

```bash
# Build everything
make

# Run tests
make test

# Run benchmarks
make bench

# Run CfC demo
./build/07_cfc_demo

# Run evolution demo
./build/08_evolution_demo

# ─────────────────────────────────────────────────────
# GENESIS: Grow soft chips from scratch
# ─────────────────────────────────────────────────────
make genesis-sine      # Evolve a sine tracker
make genesis-anomaly   # Evolve an anomaly detector

# ─────────────────────────────────────────────────────
# SENTINEL: Deploy anomaly detector daemon
# ─────────────────────────────────────────────────────
make sentinel
./build/sentinel --cpu      # Monitor CPU load
./build/sentinel --memory   # Monitor memory usage
./build/sentinel --network  # Monitor network traffic
```

---

## Performance

| Metric | Value |
|--------|-------|
| CfC step latency | **206 ns** |
| Steps/second | 4.86 M |
| Evolution speed | 2,343 gen/sec |
| Binary mutation | **65.8 M/sec** |
| Chip memory | **396 bytes** |
| Genesis time | **60 seconds** |
| Tracker correlation | **r > 0.97** |

---

## Components

| Directory | Description |
|-----------|-------------|
| [`foundry/`](foundry/) | **Genesis seed generator** — Grow soft chips from scratch |
| [`src/`](src/) | **Production tools** — Sentinel daemon for anomaly detection |
| [`test/`](test/) | Benchmarks and verification tests |
| [`examples/`](examples/) | Demo programs |

## Related

- **Toolchain** — [`../tools/`](../tools/) — Build soft chips, LNN→TriX compiler
- **Visualization** — [`../viz/`](../viz/) — See computation happen
- **Proofs** — [`../proofs/`](../proofs/) — Existence proofs (6502 ALU)

---

## Requirements

- GCC or Clang with C11 support
- libc (that's it)

---

## License

MIT

---

*"It's all in the reflexes."*
