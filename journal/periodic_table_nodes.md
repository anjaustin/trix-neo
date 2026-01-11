# The Periodic Table of Compute Shapes — NODES

*Lincoln Manifold Method: Nodes phase*
*Crystallizing the structure.*

---

## Node 1: The Five Primordials

After reduction, five irreducible operations remain:

```
┌─────────────────────────────────────────────────────────────┐
│                    THE FIVE PRIMORDIALS                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   ADD     (+)      The accumulator                          │
│   MUL     (×)      The scaler                               │
│   EXP     (e^x)    The grower                               │
│   MAX     (⌈)      The selector                             │
│   CONST   (k)      The anchor                               │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

Everything else derives from these.

**Key insight:** Logic gates (NAND, XOR, etc.) are NOT primordial. They are polynomial compounds of ADD, MUL, and CONST.

---

## Node 2: The Derivation Hierarchy

```
Level 0: PRIMORDIALS
         └── ADD, MUL, EXP, MAX, CONST

Level 1: ELEMENTALS (1-2 primordials)
         └── AND=MUL, OR=ADD+MUL, NOT=ADD+MUL, ReLU=MAX

Level 2: COMPOUNDS (3-5 primordials)
         └── XOR, Sigmoid, Tanh, SQRT

Level 3: MOLECULES (compositions of compounds)
         └── Full-adder, Hamming, Attention

Level 4: TISSUES (repeated molecules)
         └── Ripple-carry, Transformer block

Level 5: ORGANS (functional systems)
         └── ALU, Encoder, Decoder

Level 6: ORGANISMS (complete architectures)
         └── 6502, Transformer, NGP
```

This is the **biological** metaphor:
- Atoms → Elements → Molecules → Tissues → Organs → Organisms

---

## Node 3: The Kingdom Structure

Geocadesia's Seven Kingdoms map to **functional categories**:

| Kingdom | Primary Primordials | Role |
|---------|---------------------|------|
| Logic | ADD, MUL, CONST | Boolean decisions |
| Arithmetic | ADD, MUL, CONST | Numerical computation |
| Activation | EXP, MAX, MUL | Nonlinearity |
| Normalization | ADD, MUL, EXP | Distribution shaping |
| Pooling | ADD, MAX | Aggregation |
| Linear | ADD, MUL | Projection |
| Attention | ADD, MUL, EXP, MAX | Dynamic routing |

**Key insight:** Kingdoms are not arbitrary. They cluster by which primordials dominate.

---

## Node 4: The Period Structure

Shapes within a kingdom vary by **complexity**:

| Period | Characteristic | Examples |
|--------|----------------|----------|
| 1 | Single primordial | AND, ADD, MAX, ReLU |
| 2 | Two primordials | XOR, SUB, LeakyReLU |
| 3 | Three+ primordials | Sigmoid, LayerNorm |
| T | Transcendental (EXP/LOG) | GELU, Softmax, SQRT |

**Key insight:** Period reflects computational cost. Higher period = more gates.

---

## Node 5: The Polynomial Unification

In TriX's continuous [0,1] domain, logic shapes are **polynomials**:

```
AND(a,b) = ab                     degree 2
OR(a,b)  = a + b - ab             degree 2
XOR(a,b) = a + b - 2ab            degree 2
NOT(a)   = 1 - a                  degree 1
NAND     = 1 - ab                 degree 2
NOR      = 1 - a - b + ab         degree 2
XNOR     = 1 - a - b + 2ab        degree 2
```

All degree ≤ 2. This is the **logic polynomial family**.

For higher-degree polynomials, you get more complex elementals.

**Key insight:** Polynomial degree is a natural "period" measure for algebraic shapes.

---

## Node 6: The Platonic Shapes

Some shapes are mathematically distinguished:

| Shape | Platonic Property |
|-------|-------------------|
| **XOR** | Self-inverse: XOR(XOR(a,b),b) = a |
| **ADD** | Forms a group: associative, identity, inverses |
| **MUL** | Forms a monoid; distributes over ADD |
| **EXP** | Unique: f'(x) = f(x), f(0) = 1 |
| **SIGMOID** | Canonical CDF; integrates to logistic |
| **SOFTMAX** | Maximum entropy under constraints |
| **NAND** | Universal: builds all boolean functions |

These are the **noble gases** — stable, fundamental, natural.

---

## Node 7: The Count

How many shapes exist at each level?

| Level | Count | Notes |
|-------|-------|-------|
| Primordials | 5 | Irreducible |
| Elementals | ~15-20 | 1-2 primordials |
| Compounds | ~30-50 | Named shapes in use |
| Molecules | ~100s | Common building blocks |
| Tissues | ~1000s | Architectural patterns |
| Organs | Unbounded | Design space |
| Organisms | Unbounded | Complete systems |

Geocadesia catalogs ~30 shapes (elementals + compounds). This is the **working vocabulary** — not all possible shapes, but the useful ones.

Like chemistry: millions of compounds, but organic chemistry focuses on carbon-based.

---

## Node 8: The Hardware Implication

If five primordials generate all shapes, hardware needs:

```
REQUIRED IN SILICON:
├── Adder        (ADD)
├── Multiplier   (MUL)
├── Exp LUT      (EXP)  ← or approximation
├── Comparator   (MAX)
└── Constants    (CONST) ← ROM or hardwired
```

Everything else is **compound**, built from these.

This matches NGP design:
- 53K gates for 30 shapes
- But 5 core units + routing

**Key insight:** The primordials define the **instruction set** of frozen compute.

---

## Node Map

```
                    ┌─────────────────────┐
                    │   FIVE PRIMORDIALS  │
                    │  ADD MUL EXP MAX K  │
                    └──────────┬──────────┘
                               │
         ┌─────────────────────┼─────────────────────┐
         │                     │                     │
         ▼                     ▼                     ▼
┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐
│    KINGDOMS     │  │     PERIODS     │  │    PLATONIC     │
│ (by function)   │  │  (by complexity)│  │    (by math)    │
│                 │  │                 │  │                 │
│ Logic           │  │ 1: Simple       │  │ XOR             │
│ Arithmetic      │  │ 2: Compound     │  │ ADD             │
│ Activation      │  │ 3: Complex      │  │ EXP             │
│ Normalization   │  │ T: Transcend.   │  │ SOFTMAX         │
│ Pooling         │  │                 │  │ NAND            │
│ Linear          │  │                 │  │                 │
│ Attention       │  │                 │  │                 │
└────────┬────────┘  └────────┬────────┘  └────────┬────────┘
         │                    │                    │
         └────────────────────┼────────────────────┘
                              │
                              ▼
                    ┌─────────────────────┐
                    │  PERIODIC TABLE OF  │
                    │   COMPUTE SHAPES    │
                    │                     │
                    │ Rows: Period        │
                    │ Cols: Kingdom       │
                    │ Atoms: Primordials  │
                    └─────────────────────┘
```

---

*Nodes crystallized.*
*The structure is clear.*
