# TriX Pure C

**Frozen shapes. Native code. No runtime. No excuses.**

```
Computation is frozen geometry.
Learning is navigation.
The shapes are eternal.
```

---

## What Is This?

Pure C implementation of TriX - ternary neural computation with frozen shapes.

**No Python. No PyTorch. No dependencies. Just math.**

---

## Quick Start

```bash
# Build everything
make

# Run the test suite
make test
```

---

## The Core Insight

Traditional neural networks learn everything - including mathematical truths that don't need to be learned.

TriX freezes the computation. XOR is always `a + b - 2ab`. That's not an opinion. We just compile it.

```c
// This is exact on binary inputs {0, 1}
float xor(float a, float b) {
    return a + b - 2.0f * a * b;
}
```

---

## Project Structure

```
zor/
├── include/trixc/
│   ├── apu.h           # Precision management, core logic shapes
│   ├── shapes.h        # Full adder, ripple adder, Hamming distance
│   ├── onnx_shapes.h   # ONNX-compatible shapes (matmul, activations)
│   ├── providence.h    # Content-addressed memory
│   └── sparse_octave.h # Multi-scale sparse lookup
│
├── examples/
│   ├── 01_hello_xor.c     # Your first frozen shape
│   ├── 02_logic_gates.c   # AND, OR, NOT, NAND, NOR, XNOR
│   ├── 03_full_adder.c    # Building arithmetic from logic
│   ├── 04_activations.c   # ReLU, GELU, Sigmoid
│   ├── 05_matmul.c        # Matrix operations
│   └── 06_tiny_mlp.c      # A complete neural network
│
├── test/
│   └── test_rigorous.c    # 1,300+ tests
│
└── Makefile
```

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
| FULL_ADDER | XOR + AND + OR | Sum + Carry from 3 bits |
| RIPPLE_ADD | Chained full adders | N-bit addition |
| HAMMING | XOR + popcount | Bit distance metric |

These are the building blocks. Any deterministic computation can be composed from frozen shapes.

---

## Philosophy

1. **Frozen shapes** - Mathematical truths compiled to native code
2. **Routing is the only learning** - Which shape for which input
3. **Precision is a shape** - FP4, FP8, FP16, FP32 conversions are frozen
4. **No runtime** - Your model becomes a binary

---

## Documentation

The theoretical foundations and engineering specifications are documented in `docs/`:

| Document | Description |
|----------|-------------|
| [The 5 Primes](docs/THE_5_PRIMES.md) | The irreducible atoms: ADD, MUL, EXP, MAX, CONST |
| [Periodic Table](docs/PERIODIC_TABLE.md) | Taxonomy of ~30 frozen shapes by function and complexity |
| [Addressable Intelligence](docs/ADDRESSABLE_INTELLIGENCE.md) | The paradigm: data addresses computation |
| [Engineering](docs/ENGINEERING.md) | Complete buildable specification: data structures, APIs, hardware |

**Key insight:** All frozen shapes derive from the 5 Primes. NAND is not elemental — it's a compound of ADD, MUL, and CONST.

---

## Proofs of Concept

See `proofs/` for existence proofs demonstrating that frozen shapes can implement complete systems:

- `proofs/6502/` - A complete 6502 ALU (16 shapes, 0 learnable params, 100% accuracy)

These are demos, not core TriX.

---

## Requirements

- GCC or Clang with C11 support
- libc (that's it)

---

## License

MIT
