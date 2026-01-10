# 6502 ALU - Proof of Concept

**This is a DEMO, not core TriX.**

The 6502 ALU proves that frozen shapes can implement deterministic computation with 100% accuracy. It is one application of the TriX paradigm, not the paradigm itself.

---

## What This Proves

| Claim | Evidence |
|-------|----------|
| Frozen shapes work | 16 shapes implement complete ALU |
| 0 learnable parameters | Routing table is static |
| 100% accuracy | Not approximate - exact |
| Minimal size | ~4.5KB binary |

---

## Quick Start

```bash
make run
```

Output:
```
Frozen 6502 - Standalone C Implementation
==========================================

ADC: 42 + 13 = 55 (expected 55) OK
EOR: 0x55 ^ 0xFF = 0xAA (expected 0xAA) OK
...
```

---

## The 16 Shapes

| ID | Shape | What It Does |
|----|-------|--------------|
| 0 | RIPPLE_ADD | 8-bit addition with carry |
| 1 | RIPPLE_SUB | 8-bit subtraction with borrow |
| 2 | PARALLEL_AND | Bitwise AND |
| 3 | PARALLEL_OR | Bitwise OR |
| 4 | PARALLEL_XOR | Bitwise XOR |
| 5 | SHIFT_LEFT | Arithmetic shift left |
| 6 | SHIFT_RIGHT | Logical shift right |
| 7 | ROTATE_LEFT | Rotate left through carry |
| 8 | ROTATE_RIGHT | Rotate right through carry |
| 9 | INCREMENT | Add 1 |
| 10 | DECREMENT | Subtract 1 |
| 11 | TRANSFER | Identity (TAX, TXA, etc.) |
| 12 | LOAD | Load value |
| 13 | STORE | Store value |
| 14 | BIT_TEST | AND for flags only |
| 15 | IDENTITY | Pass through |

---

## Why 6502?

The MOS 6502 powered the Apple II, Commodore 64, and NES. It's:
- Simple enough to implement completely
- Complex enough to be meaningful
- Well-documented with known-correct behavior
- A cultural touchstone

But TriX is not about retro computing. The 6502 is chosen because it's a perfect test case for deterministic frozen computation.

---

## What This Is NOT

- This is NOT the TriX architecture
- This is NOT how you build neural networks with TriX
- This is NOT the limit of what frozen shapes can do

The 6502 needs 16 shapes. Language models might need 1,600. The paradigm is the same.

---

## Files

| File | Description |
|------|-------------|
| `frozen_6502_standalone.c` | Self-contained implementation |
| `include/alu6502.h` | Header with APU integration |

---

*"The 6502 is the hello world of frozen computation."*
