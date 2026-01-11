# The Periodic Table of Compute Shapes

*A taxonomy of frozen computation.*

---

## Overview

Just as chemistry organizes elements by atomic structure, computational shapes can be organized by their composition from the 5 Primes.

```
╔═══════════════════════════════════════════════════════════════════════════╗
║                    PERIODIC TABLE OF COMPUTE SHAPES                       ║
╠═══════════════════════════════════════════════════════════════════════════╣
║                                                                           ║
║  THE 5 PRIMES    │ ADD │ MUL │ EXP │ MAX │CONST│                         ║
║                  │  +  │  ×  │ e^x │  ⌈  │  k  │                         ║
║                  └──┬──┴──┬──┴──┬──┴──┬──┴──┬──┘                         ║
║                     └─────┴─────┴─────┴─────┘                             ║
║                              │                                            ║
║  ════════════════════════════╧════════════════════════════════════════   ║
║                                                                           ║
║                         K I N G D O M S                                   ║
║  PERIOD      Logic    Arith    Activ    Norm     Pool     Linear         ║
║  ─────────────────────────────────────────────────────────────────────   ║
║                                                                           ║
║    1         AND      ADD      ReLU      —       MAX       DOT           ║
║  (simple)    OR       MUL       —        —       SUM        —            ║
║              NOT       —        —        —        —         —            ║
║                                                                           ║
║    2         XOR      SUB     LReLU    Mean      AVG      MatMul         ║
║  (compound)  NAND     NEG       —       —       MIN        —            ║
║              NOR       —        —        —        —         —            ║
║                                                                           ║
║    3         XNOR     DIV     Sigmoid   Var       —       Conv           ║
║  (complex)   MUX      MOD     Tanh      —        —         —            ║
║               —        —        —        —        —         —            ║
║                                                                           ║
║    T          —      SQRT     GELU    LNorm   Argmax     Attn           ║
║  (transcend)  —      LOG     Swish    RMS    Argmin       —            ║
║               —       —      Softmax    —        —         —            ║
║                                                                           ║
╚═══════════════════════════════════════════════════════════════════════════╝
```

---

## The Axes

### Rows: Periods (Complexity)

| Period | Name | Characteristic |
|--------|------|----------------|
| 1 | Simple | 1-2 Primes |
| 2 | Compound | 2-3 Primes |
| 3 | Complex | 3-4 Primes |
| T | Transcendental | Requires EXP or LOG |

### Columns: Kingdoms (Function)

| Kingdom | Primary Primes | Purpose |
|---------|----------------|---------|
| Logic | ADD, MUL, CONST | Boolean decisions |
| Arithmetic | ADD, MUL, CONST | Numerical computation |
| Activation | EXP, MAX, MUL | Nonlinearity |
| Normalization | ADD, MUL, EXP | Distribution shaping |
| Pooling | ADD, MAX | Aggregation |
| Linear | ADD, MUL | Projection |
| Attention | ADD, MUL, EXP, MAX | Dynamic routing |

---

## Kingdom Details

### Logic Kingdom

Boolean operations — the foundation of discrete computation.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| AND | a × b | MUL | 1 |
| OR | a + b - ab | ADD, MUL | 1 |
| NOT | 1 - a | ADD, MUL, CONST | 1 |
| XOR | a + b - 2ab | ADD, MUL, CONST | 2 |
| NAND | 1 - ab | ADD, MUL, CONST | 2 |
| NOR | (1-a)(1-b) | ADD, MUL, CONST | 2 |
| XNOR | 1 - a - b + 2ab | ADD, MUL, CONST | 3 |

**Key insight:** All logic shapes are polynomials of degree ≤ 2.

### Arithmetic Kingdom

Numerical operations — the workhorses of computation.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| ADD | a + b | ADD | 1 |
| MUL | a × b | MUL | 1 |
| SUB | a - b | ADD, MUL, CONST | 2 |
| NEG | -a | MUL, CONST | 2 |
| DIV | a / b | MUL, iteration | 3 |
| MOD | a mod b | ADD, MUL, iteration | 3 |
| SQRT | √a | EXP, LOG, MUL | T |
| LOG | ln(a) | EXP inverse | T |

### Activation Kingdom

Nonlinearities — enabling learning and depth.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| ReLU | max(0, x) | MAX, CONST | 1 |
| LeakyReLU | max(αx, x) | MAX, MUL, CONST | 2 |
| Sigmoid | 1/(1+e^(-x)) | ADD, MUL, EXP, CONST | 3 |
| Tanh | (e^x - e^(-x))/(e^x + e^(-x)) | ADD, MUL, EXP | 3 |
| GELU | x · Φ(x) | ADD, MUL, EXP | T |
| Swish | x · sigmoid(x) | ADD, MUL, EXP | T |
| Softmax | e^x / Σe^x | ADD, MUL, EXP | T |

### Normalization Kingdom

Distribution shaping — stabilizing computation.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| Mean | Σx / n | ADD, MUL | 2 |
| Variance | Σ(x-μ)² / n | ADD, MUL | 3 |
| LayerNorm | (x-μ) / √(σ²+ε) | ADD, MUL, EXP, LOG | T |
| RMSNorm | x / √(mean(x²)) | ADD, MUL, EXP, LOG | T |

### Pooling Kingdom

Aggregation — reducing dimensions.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| MAX | max(x₁...xₙ) | MAX | 1 |
| SUM | Σxᵢ | ADD | 1 |
| AVG | Σxᵢ / n | ADD, MUL | 2 |
| MIN | min(x₁...xₙ) | MAX, MUL, CONST | 2 |
| Argmax | argmax(x) | MAX, comparison | T |
| Argmin | argmin(x) | MAX, comparison | T |

### Linear Kingdom

Projections — transforming spaces.

| Shape | Formula | Primes | Period |
|-------|---------|--------|--------|
| DOT | Σaᵢbᵢ | ADD, MUL | 1 |
| MatMul | A × B | ADD, MUL | 2 |
| Conv | Σw·x | ADD, MUL | 2 |
| Attention | softmax(QK^T)V | ADD, MUL, EXP | T |

---

## Molecular Shapes

Beyond elemental shapes, molecular shapes combine multiple elementals:

| Molecule | Components | Level |
|----------|------------|-------|
| Half-Adder | XOR, AND | 3 |
| Full-Adder | XOR×2, AND×2, OR | 3 |
| Hamming | XOR, POPCOUNT | 3 |
| Ripple-Carry | Full-Adder chain | 4 |
| Attention-Head | MatMul, Softmax, MatMul | 4 |
| Transformer-Block | Attention, FFN, LayerNorm | 4 |
| ALU | Multiple arithmetic shapes | 5 |
| NGP-Core | Zit detector, Shape fabric | 6 |

---

## Reading the Table

### Position Predicts Properties

- **Same row**: Similar complexity, different function
- **Same column**: Similar function, different complexity
- **Upper-left**: Simpler, cheaper, faster
- **Lower-right**: Complex, expensive, powerful

### Prime Signature

Each shape has a "Prime signature" — which of the 5 Primes it uses:

| Shape | ADD | MUL | EXP | MAX | CONST | Signature |
|-------|-----|-----|-----|-----|-------|-----------|
| AND | - | ✓ | - | - | - | M |
| XOR | ✓ | ✓ | - | - | ✓ | AMK |
| ReLU | - | - | - | ✓ | ✓ | XK |
| Sigmoid | ✓ | ✓ | ✓ | - | ✓ | AMEK |
| Softmax | ✓ | ✓ | ✓ | - | - | AME |

Shapes with similar signatures have similar implementation costs.

---

## The Platonic Shapes

Some shapes have distinguished mathematical status:

| Shape | Property |
|-------|----------|
| **XOR** | Self-inverse: XOR(XOR(a,b), b) = a |
| **ADD** | Defines abelian group structure |
| **MUL** | Defines ring structure |
| **EXP** | Unique: f'(x) = f(x), f(0) = 1 |
| **SOFTMAX** | Maximum entropy distribution |
| **NAND** | Universal for boolean logic |

These are the **noble gases** of computation — stable, fundamental, ubiquitous.

---

## Completeness

The table currently catalogs ~30 shapes. Is this complete?

**For practical ML/compute:** Largely yes. These cover standard operations.

**Mathematically:** No. Infinitely many polynomial and transcendental functions exist.

**The principle:** If a new useful shape emerges, it will:
1. Decompose into the 5 Primes
2. Fit into an existing or new Kingdom
3. Have a predictable Period based on complexity

The table is **extensible but principled**.

---

## Hardware Mapping

Each Period has hardware implications:

| Period | Hardware Needs |
|--------|----------------|
| 1 | Basic ALU operations |
| 2 | ALU + simple combinations |
| 3 | ALU + iterative methods |
| T | ALU + LUT/approximators for EXP |

Higher Period = more gates, more latency, more power.

---

## Summary

The Periodic Table of Compute Shapes:

1. **Organizes** ~30 frozen shapes by function and complexity
2. **Grounds** in the 5 Primes as irreducible atoms
3. **Predicts** properties from position
4. **Extends** as new shapes are discovered
5. **Maps** to hardware requirements

```
Kingdoms = Columns = Function
Periods  = Rows    = Complexity
Primes   = Atoms   = Composition
```

---

*"Position in the table predicts properties."*

*"The table is the map. Geocadesia is the catalog."*
