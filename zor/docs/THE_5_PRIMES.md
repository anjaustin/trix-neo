# The 5 Primes

*The irreducible atoms of frozen computation.*

---

## Definition

The **5 Primes** are the five primordial operations from which all frozen shapes derive:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│                           THE 5 PRIMES                                  │
│                                                                         │
│      ADD          MUL          EXP          MAX         CONST           │
│       +            ×           e^x           ⌈            k             │
│                                                                         │
│   accumulate     scale        grow        select       anchor           │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

| Prime | Symbol | Operation | Role |
|-------|--------|-----------|------|
| **ADD** | + | a + b | Accumulation. The foundation of arithmetic. |
| **MUL** | × | a × b | Scaling. The foundation of geometry. |
| **EXP** | e^x | e^x | Growth. The transcendental bridge. |
| **MAX** | ⌈ | max(a, b) | Selection. The source of discontinuity. |
| **CONST** | k | 0, 1, -1, e, π | Anchoring. The fixed points. |

---

## Why These Five?

### ADD — The Accumulator

Addition is irreducible. You cannot build addition from other operations without iteration.

- Defines the natural numbers (Peano axioms)
- Provides the group structure (associative, commutative, identity, inverses)
- Foundation of all counting, aggregation, translation

### MUL — The Scaler

Multiplication is irreducible in dataflow. While theoretically repeated addition, in practice it requires dedicated implementation.

- Defines the ring structure (distributes over addition)
- Foundation of all scaling, projection, area
- AND gate in logic: `AND(a,b) = a × b`

### EXP — The Grower

The exponential function is the unique function equal to its own derivative: f'(x) = f(x), f(0) = 1.

- Cannot be expressed as a polynomial
- Foundation of all smooth nonlinearity (sigmoid, softmax, GELU)
- Bridge between linear and nonlinear worlds

### MAX — The Selector

MAX introduces discontinuity. It cannot be expressed as a polynomial or smooth function.

- Foundation of comparison and ordering
- Source of ReLU and all piecewise functions
- Enables discrete selection in continuous space

### CONST — The Anchor

Constants are the fixed reference points.

- 0: The additive identity
- 1: The multiplicative identity
- -1: The negation factor
- e, π: The transcendental constants

---

## The Derivation Principle

**Every frozen shape is a composition of the 5 Primes.**

### Logic Shapes

```
NOT(a)     = 1 - a               = ADD(1, MUL(-1, a))
AND(a,b)   = ab                  = MUL(a, b)
OR(a,b)    = a + b - ab          = ADD(a, ADD(b, MUL(-1, MUL(a,b))))
XOR(a,b)   = a + b - 2ab         = ADD(a, ADD(b, MUL(-2, MUL(a,b))))
NAND(a,b)  = 1 - ab              = ADD(1, MUL(-1, MUL(a,b)))
NOR(a,b)   = (1-a)(1-b)          = MUL(ADD(1, MUL(-1,a)), ADD(1, MUL(-1,b)))
XNOR(a,b)  = 1 - a - b + 2ab     = ADD(1, ADD(MUL(-1,a), ADD(MUL(-1,b), MUL(2,MUL(a,b)))))
```

**Key insight:** NAND is not elemental. In the polynomial view, logic gates are compounds of ADD, MUL, and CONST.

### Arithmetic Shapes

```
SUB(a,b)   = a - b               = ADD(a, MUL(-1, b))
NEG(a)     = -a                  = MUL(-1, a)
DIV(a,b)   = a / b               = MUL(a, RECIP(b))  [RECIP via iteration]
SQRT(a)    = a^0.5               = EXP(MUL(0.5, LOG(a)))
```

### Activation Shapes

```
ReLU(x)    = max(0, x)           = MAX(0, x)
LeakyReLU  = max(αx, x)          = MAX(MUL(α, x), x)
Sigmoid(x) = 1/(1+e^(-x))        = DIV(1, ADD(1, EXP(MUL(-1, x))))
Tanh(x)    = (e^x - e^(-x))/(e^x + e^(-x))
GELU(x)    = x · Φ(x)            = MUL(x, ...)  [involves EXP]
Softmax    = e^x / Σe^x          = DIV(EXP(x), SUM(EXP(x)))
```

---

## The Hierarchy

The 5 Primes generate all computation through hierarchical composition:

```
Level 0: PRIMORDIALS         The 5 Primes
         └── ADD, MUL, EXP, MAX, CONST

Level 1: ELEMENTALS          ~15 shapes
         └── AND, OR, NOT, ReLU, SUB, NEG
         └── Built from 1-2 Primes

Level 2: COMPOUNDS           ~30 shapes
         └── XOR, NAND, Sigmoid, Tanh, LayerNorm
         └── Built from 3+ Primes

Level 3: MOLECULES           ~100s of patterns
         └── Full-adder, Hamming, Attention-head
         └── Compositions of compounds

Level 4: TISSUES             ~1000s of patterns
         └── Ripple-carry, Transformer-block
         └── Repeated molecular structures

Level 5: ORGANS              Unbounded
         └── ALU, Encoder, Decoder
         └── Functional subsystems

Level 6: ORGANISMS           Unbounded
         └── 6502, Transformer, NGP
         └── Complete architectures
```

---

## Hardware Implications

If the 5 Primes generate all shapes, hardware needs only five units:

| Unit | Prime | Implementation |
|------|-------|----------------|
| Adder | ADD | Standard integer/float adder |
| Multiplier | MUL | Standard integer/float multiplier |
| Exp Unit | EXP | LUT or polynomial approximation |
| Comparator | MAX | Standard comparison logic |
| Constants | CONST | ROM or hardwired values |

All 30 shapes in Geocadesia derive from these five units.

The NGP's 53K gates implement compositions of these five primitives.

---

## Mathematical Grounding

The 5 Primes are not arbitrary. They correspond to fundamental mathematical structures:

| Prime | Mathematical Structure |
|-------|------------------------|
| ADD | Group theory (abelian groups) |
| MUL | Ring theory (multiplication in rings) |
| ADD + MUL | Field theory (real numbers) |
| EXP | Analysis (differential equations) |
| MAX | Order theory (lattices) |
| CONST | Constants of nature (e, π, 0, 1) |

This grounding makes the 5 Primes **discovered, not invented**.

---

## Testable Predictions

The 5 Primes framework predicts:

1. **Decomposability**: Any frozen shape can be decomposed into the 5 Primes.

2. **Completeness**: Hardware with these five units can implement any shape.

3. **Complexity correlation**: Shapes using more Primes cost more gates.

4. **Kingdom clustering**: Shapes in the same kingdom share Prime signatures.

5. **Discovery constraint**: Newly discovered useful shapes will compose from the 5 Primes.

---

## Relationship to Geocadesia

Geocadesia catalogs ~30 shapes across Seven Kingdoms.

The 5 Primes explain **why** these shapes exist and **how** they relate:

- **Kingdoms** cluster by dominant Primes
- **Periods** order by compositional complexity
- **Compounds** are traceable derivations from Primes

Geocadesia is the catalog. The 5 Primes are the underlying theory.

---

## Summary

```
┌─────────────────────────────────────────────────────────────────────────┐
│                                                                         │
│   The 5 Primes: ADD, MUL, EXP, MAX, CONST                              │
│                                                                         │
│   • Irreducible: Cannot be built from each other                       │
│   • Complete: Generate all frozen shapes                                │
│   • Grounded: Correspond to mathematical structures                     │
│   • Practical: Map directly to hardware units                          │
│                                                                         │
│   "Five operations. All of frozen computation."                        │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

*"The shapes are not invented. They are discovered."*

*"Five Primes. Everything else is composition."*
