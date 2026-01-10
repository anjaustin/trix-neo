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

# Run the 6502 ALU demo
make demo

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

## The Existence Proof

A complete 6502 ALU in ~350 lines of C:
- 16 frozen shapes
- 0 learnable parameters
- 100% accuracy
- ~6 KB binary

The same ALU that powered the Apple II.

---

## Project Structure

```
zor/
├── include/trixc/
│   ├── apu.h           # Precision management, core logic shapes
│   ├── shapes.h        # Full adder, ripple adder, Hamming distance
│   ├── alu6502.h       # 6502 ALU implementation
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
├── experiments/
│   └── frozen_6502_standalone.c  # Self-contained 6502 ALU
│
├── test/
│   └── test_rigorous.c    # 1,300+ tests
│
└── Makefile
```

---

## The 16 Frozen Shapes

| ID | Shape | Operation |
|----|-------|-----------|
| 0 | XOR | `a + b - 2ab` |
| 1 | AND | `a * b` |
| 2 | OR | `a + b - ab` |
| 3 | NOT | `1 - a` |
| 4 | FULL_ADDER | Sum + Carry from 3 bits |
| 5 | RIPPLE_ADD | 8-bit addition |
| 6 | RIPPLE_SUB | 8-bit subtraction |
| 7 | ASL | Arithmetic shift left |
| 8 | LSR | Logical shift right |
| 9 | ROL | Rotate left through carry |
| 10 | ROR | Rotate right through carry |
| 11 | INC | Increment |
| 12 | DEC | Decrement |
| 13 | HAMMING | XOR + popcount distance |
| 14 | COMPARE | Soft comparison for routing |
| 15 | IDENTITY | Pass-through |

---

## Philosophy

1. **Frozen shapes** - Mathematical truths compiled to native code
2. **Routing is the only learning** - Which shape for which input
3. **Precision is a shape** - FP4, FP8, FP16, FP32 conversions are frozen
4. **No runtime** - Your model becomes a binary

---

## Requirements

- GCC or Clang with C11 support
- libc (that's it)

---

## License

MIT
