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
| [The Forge](docs/FORGE.md) | **Chip compiler: Data in, chip out** |
| [Fuse Box](docs/FUSE_BOX.md) | **Universal chip validation harness** |
| [HSOS](docs/HSOS.md) | **Hollywood Squares OS: Distributed semantic computation** |
| [EntroMorphic OS](docs/ENTROMORPHIC_OS.md) | **Read-Only Nervous System: Real-time grid monitoring** |
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
| `09_hsos_demo` | **Hollywood Squares OS: distributed sort + CSP** |

---

## Header Files

| Header | Description |
|--------|-------------|
| `apu.h` | Precision types and basic ops |
| `shapes.h` | Logic shapes (XOR, AND, adders) |
| `onnx_shapes.h` | ONNX ops (MatMul, Softmax, LayerNorm) |
| `cfc_shapes.h` | **CfC cells and networks** |
| `hsos.h` | **Hollywood Squares OS: distributed microkernel** |
| `entromorph.h` | **Evolution engine** |
| `shapefabric.h` | **Binary executable graphs** |
| `alpha_device.cuh` | **CUDA liquid physics kernel** |

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
# THE FORGE: Data in, chip out (Product Layer)
# ─────────────────────────────────────────────────────
make forge                          # Build the forge
./build/forge train -i data.csv -o my_chip.h  # Train chip

# Validate the chip
make fuse-compile CHIP_FILE=my_chip.h CHIP_NAME=MY_CHIP
./build/fuse_test test_data.csv     # Test on new data

# ─────────────────────────────────────────────────────
# ENTROMORPHIC OS: The Read-Only Nervous System
# ─────────────────────────────────────────────────────
make entromorph             # Build sentinel + director

# Terminal 1: Launch sentinels (perception agents)
./build/sentinel 0 CPU_0 --mock &
./build/sentinel 4 NET_TX --mock &
./build/sentinel 8 MEM_FREE --mock &

# Terminal 2: Launch director (3x3 grid viewer)
./build/director_text

# ─────────────────────────────────────────────────────
# HSOS: Hollywood Squares OS (distributed computation)
# ─────────────────────────────────────────────────────
make hsos                   # Run the HSOS demo

# ─────────────────────────────────────────────────────
# GPU CORTEX: 16 Million Liquid Neurons (CUDA)
# ─────────────────────────────────────────────────────
make cortex                 # Build the cortex benchmark
./build/cortex_bench        # Run 256³ voxel benchmark

# ─────────────────────────────────────────────────────
# VISOR: Volumetric Brain Renderer (requires GLFW)
# ─────────────────────────────────────────────────────
make visor                  # Build the raymarching visualizer
./build/visor               # Watch the shockwave propagate
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

## Validation

The V3 Efficient Species passed all 5 Skeptic Tests:

| Test | Doubt | Result | Status |
|------|-------|--------|--------|
| **Stability** | "Recurrent nets drift" | 1.47% / 100M steps | ✅ |
| **Bandwidth** | "Memorized 1Hz sine" | 0.1-45 Hz @ 97.9% | ✅ |
| **Noise** | "Only works clean" | 7.8x rejection | ✅ |
| **Generalization** | "Only tracks circles" | All waveforms r>0.97 | ✅ |
| **Determinism** | "FP varies by arch" | Bit-identical ARM64 | ✅ |

See [`docs/VALIDATION.md`](docs/VALIDATION.md) for complete test results.

---

## Components

| Directory | Description |
|-----------|-------------|
| [`foundry/`](foundry/) | **Genesis seed generator** — Grow soft chips from scratch |
| [`src/`](src/) | **Production tools** — Sentinel daemon, HSOS implementation |
| [`bin/`](bin/) | **EntroMorphic OS** — Director visualization tools |
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
