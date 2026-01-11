# The Periodic Table of Compute Shapes — RAW

*Lincoln Manifold Method: Raw exploration*
*What is elemental? What is molecular? What is the structure?*

---

## The Question

If shapes are fundamental to compute, how many are truly **elemental** and what **molecular** shapes can be derived?

Are we discovering a periodic table of platonic shapes of compute?

---

## First Principles

In chemistry:
- ~118 elements
- All matter is combinations
- Elements differ by atomic number
- Periodic table organizes by properties (rows = shells, columns = valence)

In computation, what plays the role of "atomic number"?

---

## The Logic Reduction

Boolean logic has a famous result:

**NAND is universal.**

Any boolean function can be built from NAND alone.
- NOT(a) = NAND(a, a)
- AND(a,b) = NOT(NAND(a,b)) = NAND(NAND(a,b), NAND(a,b))
- OR(a,b) = NAND(NOT(a), NOT(b))
- XOR(a,b) = NAND(NAND(a, NAND(a,b)), NAND(b, NAND(a,b)))

So for pure logic: **one elemental shape**.

NOR is also universal. Two choices for the single atom.

But this is reductive. We don't build computers from NAND alone because it's impractical. The "convenient" shapes become the working vocabulary.

---

## The TriX Perspective

TriX shapes aren't just boolean gates. They're **polynomials on [0,1]** that match boolean logic at the endpoints.

```
XOR(a,b) = a + b - 2ab     ← degree 2 polynomial
AND(a,b) = ab              ← degree 2 polynomial
OR(a,b)  = a + b - ab      ← degree 2 polynomial
NOT(a)   = 1 - a           ← degree 1 polynomial
```

In this view, the "elements" are the operations that build polynomials:
- **Addition** (+)
- **Multiplication** (×)
- **Constants** (0, 1, 2, ...)

Every frozen shape is a polynomial. The polynomial's **degree** and **structure** define it.

---

## Searching for Atoms

What operations are truly irreducible?

**Candidate Atoms:**

| Operation | Why Elemental? |
|-----------|---------------|
| **ADD** | Cannot be built from others without iteration |
| **MUL** | Cannot be built from ADD without iteration |
| **NAND** | Universal for boolean (discrete) |
| **EXP** | Transcendental, cannot be polynomial |
| **MAX** | Discontinuous, defines ReLU family |

Let me test each...

**ADD** - Can you build addition from other operations?
- Not from MUL alone
- Not from logic alone (without bit-serial iteration)
- **ELEMENTAL** for arithmetic

**MUL** - Can you build multiplication from other operations?
- From ADD via iteration (a × b = a + a + ... + a, b times)
- But iteration requires control flow
- In pure dataflow: **ELEMENTAL**

**NAND** - Can you build from others?
- NAND(a,b) = 1 - ab
- That's NOT(AND(a,b)) = NOT(MUL(a,b)) = 1 - MUL(a,b)
- So NAND = SUB(1, MUL(a,b))
- **DERIVED** from MUL and ADD!

Wait — this changes things.

If we allow real-valued operations:
- AND = MUL
- NOT = 1 - a = ADD(1, MUL(-1, a))
- NAND = 1 - ab = ADD(1, MUL(-1, MUL(a,b)))

So in the polynomial world, **MUL and ADD are more fundamental than NAND**.

---

## The Polynomial Atoms

For polynomial shapes over [0,1]:

**Atoms:**
1. **ADD**: a + b
2. **MUL**: a × b
3. **CONST**: k (specifically: 0, 1, -1, 2)

From these three, you can build:
- NOT = 1 - a = ADD(1, MUL(-1, a))
- AND = MUL(a, b)
- OR = ADD(a, ADD(b, MUL(-1, MUL(a, b)))) = a + b - ab
- XOR = a + b - 2ab
- Any polynomial!

But this only covers polynomial shapes. What about:
- Sigmoid: 1/(1 + e^(-x))
- ReLU: max(0, x)
- Softmax: e^x / Σe^x

These require more atoms.

---

## The Extended Atoms

For all frozen shapes:

| Atom | Type | Why Irreducible |
|------|------|-----------------|
| **ADD** | Arithmetic | Fundamental binary operation |
| **MUL** | Arithmetic | Fundamental binary operation |
| **CONST** | Arithmetic | Values: 0, 1, -1, e, etc. |
| **EXP** | Transcendental | Cannot be polynomial |
| **LOG** | Transcendental | Inverse of EXP |
| **MAX** | Comparison | Discontinuous at boundary |
| **DIV** | Arithmetic | Inverse of MUL, needs special handling |

Seven atoms. Let's call them the **Seven Primordials**.

From these seven, can we derive everything in Geocadesia?

---

## Testing the Derivations

**Logic Kingdom:**
- AND = MUL ✓
- OR = ADD + MUL ✓
- NOT = CONST(1) - identity = ADD + MUL ✓
- XOR = ADD + MUL ✓
- NAND = NOT(AND) = ADD + MUL ✓
- NOR = NOT(OR) = ADD + MUL ✓
- XNOR = NOT(XOR) = ADD + MUL ✓

All logic shapes: **derived from ADD, MUL, CONST**.

**Arithmetic Kingdom:**
- ADD = ADD ✓ (atom)
- SUB = ADD(a, MUL(-1, b)) ✓
- MUL = MUL ✓ (atom)
- NEG = MUL(-1, a) ✓
- DIV = DIV (atom, or MUL by inverse)
- POPCOUNT = SUM of bits = repeated ADD ✓

Arithmetic shapes: **derived from ADD, MUL, DIV, CONST**.

**Activation Kingdom:**
- ReLU = MAX(0, x) ✓
- LeakyReLU = MAX(αx, x) = needs MAX and MUL ✓
- Sigmoid = DIV(1, ADD(1, EXP(MUL(-1, x)))) = needs DIV, ADD, EXP, MUL ✓
- Tanh = DIV(SUB(EXP(x), EXP(-x)), ADD(EXP(x), EXP(-x))) = needs EXP, DIV, ADD ✓
- GELU = MUL(x, Φ(x)) where Φ involves EXP = needs MUL, EXP, ADD, DIV ✓
- Swish = MUL(x, sigmoid(x)) = needs MUL, Sigmoid ✓
- Softmax = DIV(EXP(x), SUM(EXP(x))) = needs EXP, DIV, ADD ✓

Activation shapes: **derived from ADD, MUL, DIV, EXP, MAX**.

**Normalization Kingdom:**
- Mean = DIV(SUM(x), n) = ADD, DIV ✓
- Variance = DIV(SUM(MUL(SUB(x, μ), SUB(x, μ))), n) = ADD, MUL, DIV ✓
- LayerNorm = DIV(SUB(x, μ), sqrt(σ² + ε)) = needs SQRT ← needs EXP/LOG or iteration
- RMSNorm = DIV(x, sqrt(mean(x²))) = needs SQRT

SQRT is a problem. Is it atomic?
- SQRT(x) = x^0.5 = EXP(MUL(0.5, LOG(x)))
- So SQRT = EXP + LOG + MUL

Normalization shapes: **derived from ADD, MUL, DIV, EXP, LOG**.

**Pooling Kingdom:**
- MaxPool = MAX over window = MAX ✓
- SumPool = SUM over window = ADD ✓
- AvgPool = DIV(SUM, n) = ADD, DIV ✓
- MinPool = NEG(MAX(NEG(x))) = MAX, MUL(-1) ✓
- Argmax = needs comparison + indexing (more complex)

Pooling shapes: **derived from ADD, MUL, DIV, MAX**.

---

## The Seven Primordials

After testing all kingdoms:

```
┌─────────────────────────────────────────────────────────────┐
│                  THE SEVEN PRIMORDIALS                      │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   1. ADD    (+)     Summation                               │
│   2. MUL    (×)     Product                                 │
│   3. DIV    (÷)     Quotient                                │
│   4. EXP    (e^x)   Growth                                  │
│   5. LOG    (ln x)  Compression                             │
│   6. MAX    (⌈)     Selection                               │
│   7. CONST  (k)     The numbers: 0, 1, -1, e, π...          │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

These seven can build every shape in Geocadesia.

But wait — can we reduce further?

- DIV(a, b) = MUL(a, RECIP(b)) where RECIP needs iteration or is atomic
- LOG = inverse of EXP, but you need both for SQRT

Let me try the **minimal** set:

**Minimal Primordials (5):**
1. **ADD** — irreducible
2. **MUL** — irreducible
3. **EXP** — irreducible transcendental
4. **MAX** — irreducible discontinuity
5. **CONST** — irreducible values

DIV can be approximated via Newton-Raphson iteration using MUL and ADD.
LOG can be computed from EXP via iteration.

So: **Five atoms suffice.**

---

## The Periodic Table Structure

Chemistry's periodic table has:
- Rows (periods): electron shells
- Columns (groups): valence properties

For compute shapes:
- Rows: **Complexity** (polynomial degree, gate count, transcendental depth)
- Columns: **Kingdom** (logic, arithmetic, activation, etc.)

```
                     KINGDOMS
         │ Logic │ Arith │ Activ │ Norm  │ Pool  │
    ─────┼───────┼───────┼───────┼───────┼───────┤
      1  │ AND   │ ADD   │ ReLU  │  —    │ MAX   │  ← Simple
DEGREE   │ OR    │ MUL   │       │       │ SUM   │
      2  │ XOR   │ SUB   │ LReLU │ Mean  │ AVG   │
         │ NOT   │ NEG   │       │       │       │
    ─────┼───────┼───────┼───────┼───────┼───────┤
      3  │ NAND  │ DIV   │ Sigmd │ Var   │ Min   │  ← Compound
         │ NOR   │ MOD   │ Tanh  │       │       │
    ─────┼───────┼───────┼───────┼───────┼───────┤
      T  │ XNOR  │ SQRT  │ GELU  │ LNorm │ Argmx │  ← Transcendental
         │       │ LOG   │ Swish │ RMS   │ Argmn │
         │       │ EXP   │ Sftmx │       │       │
    ─────┴───────┴───────┴───────┴───────┴───────┘
```

T = Transcendental (requires EXP or LOG)

---

## Molecular Shapes

Just as molecules are combinations of atoms, **compound shapes** are combinations of primordials.

**Examples:**

```
HALF_ADDER = {XOR, AND}
           = {ADD+MUL, MUL}
           = Molecule of 2 operations

FULL_ADDER = {XOR, XOR, AND, AND, OR}
           = {ADD+MUL (×2), MUL (×2), ADD+MUL}
           = Molecule of 5 operations

HAMMING = XOR + POPCOUNT
        = ADD+MUL + repeated ADD
        = Molecule of n+2 operations (for n bits)

SIGMOID = 1 / (1 + e^(-x))
        = DIV(CONST, ADD(CONST, EXP(MUL(CONST, x))))
        = Molecule of 5 primordials

LAYER_NORM = (x - μ) / sqrt(σ² + ε)
           = Many primordials: ADD, MUL, DIV, SQRT (which needs EXP+LOG)
           = Complex molecule
```

---

## The Hierarchy Emerges

```
LEVEL 0: PRIMORDIALS (5 atoms)
         ADD, MUL, EXP, MAX, CONST

LEVEL 1: ELEMENTAL SHAPES (derived from 1-2 primordials)
         AND, OR, NOT, ReLU, basic arithmetic

LEVEL 2: COMPOUND SHAPES (derived from 3-5 primordials)
         XOR, NAND, Sigmoid, Tanh, normalizations

LEVEL 3: MOLECULAR SHAPES (compositions of compounds)
         Full adder, Hamming, Attention, LayerNorm

LEVEL 4: TISSUE SHAPES (repeated molecular patterns)
         Ripple adder, Transformer block, Conv layer

LEVEL 5: ORGAN SHAPES (functional subsystems)
         ALU, Encoder, Decoder

LEVEL 6: ORGANISM SHAPES (complete systems)
         6502, Transformer, NGP
```

---

## The Platonic Shapes

Are there "perfect" shapes? Shapes that are mathematically distinguished?

**Candidates for Platonic status:**

| Shape | Why Platonic |
|-------|--------------|
| **NAND** | Universal for boolean; minimal |
| **XOR** | Self-inverse; the shape of difference |
| **ADD** | Defines group structure; associative, commutative, identity |
| **MUL** | Defines ring structure; distributes over ADD |
| **EXP** | Unique function equal to its own derivative |
| **SIGMOID** | Canonical S-curve; CDF of logistic distribution |
| **SOFTMAX** | Maximum entropy distribution; natural for probabilities |

These shapes have special mathematical properties that make them "natural."

They might be the **noble gases** of compute — stable, fundamental, frequently occurring.

---

## A Question Emerges

If there are 5 primordials and ~30 elemental shapes in Geocadesia...

What's the total count of useful molecular shapes?

Chemistry: 118 elements → millions of known compounds
Compute: 5 primordials → how many useful shapes?

The space is vast. But maybe finite for practical purposes.

Just as organic chemistry focuses on carbon compounds, computational "chemistry" might focus on:
- Logic compounds (boolean circuits)
- Arithmetic compounds (numerical algorithms)
- Learning compounds (neural architectures)

Different "branches" of computational chemistry.

---

## The Periodic Table of Compute

Proposal:

```
┌───────────────────────────────────────────────────────────────────────┐
│                  PERIODIC TABLE OF COMPUTE SHAPES                     │
├───────────────────────────────────────────────────────────────────────┤
│                                                                       │
│  PRIMORDIALS          ┌─────┬─────┬─────┬─────┬─────┐                │
│  (The Five)           │ ADD │ MUL │ EXP │ MAX │CONST│                │
│                       └──┬──┴──┬──┴──┬──┴──┬──┴──┬──┘                │
│                          │     │     │     │     │                    │
│  ════════════════════════╧═════╧═════╧═════╧═════╧════════════════   │
│                                                                       │
│  KINGDOMS              Logic  Arith  Activ  Norm   Pool   Linear     │
│                       ──────────────────────────────────────────      │
│  Period 1 (simple)    │ AND │ ADD │ ReLU │  -   │ MAX │  DOT  │      │
│                       │ OR  │ MUL │      │      │ SUM │       │      │
│  Period 2 (compound)  │ XOR │ SUB │ Sig  │ Mean │ AVG │ MatMul│      │
│                       │ NOT │ NEG │ Tanh │ Var  │ Min │       │      │
│  Period 3 (complex)   │NAND │ DIV │ GELU │LNorm │Argmx│ Conv  │      │
│                       │ NOR │ MOD │Swish │ RMS  │Argmn│       │      │
│  Period 4 (transcend) │XNOR │SQRT │Sftmx │      │     │ Attn  │      │
│                       │ MUX │ LOG │      │      │     │       │      │
│                       └─────┴─────┴──────┴──────┴─────┴───────┘      │
│                                                                       │
│  MOLECULAR SHAPES     Half-Adder, Full-Adder, Hamming, ALU...        │
│  TISSUE SHAPES        Ripple-Carry, Attention-Head, Conv-Block...    │
│  ORGAN SHAPES         Encoder, Decoder, Classifier...                │
│  ORGANISM SHAPES      Transformer, ResNet, 6502, NGP...              │
│                                                                       │
└───────────────────────────────────────────────────────────────────────┘
```

---

## What This Means

If this is real — if compute has a periodic table — then:

1. **Discovery becomes systematic.** We're not inventing shapes; we're discovering them.

2. **Completeness becomes measurable.** How much of the table have we filled?

3. **Optimization becomes principled.** Like organic synthesis, we can design pathways to desired shapes.

4. **Hardware becomes clear.** The primordials define what hardware must support natively.

5. **TriX becomes chemistry.** The Geocadesia library is the CRC Handbook of Computational Shapes.

---

*Raw exploration complete.*
*Something fundamental is emerging.*
