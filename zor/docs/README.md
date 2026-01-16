# TriX Documentation

*The theoretical foundations of frozen computation.*

---

## Core Documents

### [The 5 Primes](THE_5_PRIMES.md)

The irreducible atoms of frozen computation.

```
ADD     (+)      accumulate
MUL     (×)      scale
EXP     (e^x)    grow
MAX     (⌈)      select
CONST   (k)      anchor
```

Every frozen shape derives from these five primordials. NAND is not elemental — it's a compound of ADD, MUL, and CONST.

### [Periodic Table of Compute Shapes](PERIODIC_TABLE.md)

A taxonomy of frozen computation.

```
           Logic    Arith    Activ    Norm     Pool     Linear
Period 1   AND      ADD      ReLU     —        MAX      DOT
Period 2   XOR      SUB      LReLU    Mean     AVG      MatMul
Period 3   XNOR     DIV      Sigmoid  Var      —        Conv
Period T   —        SQRT     GELU     LNorm    Argmax   Attn
```

Kingdoms organize by function. Periods organize by complexity. Position predicts properties.

### [Addressable Intelligence](ADDRESSABLE_INTELLIGENCE.md)

A new paradigm: Data addresses computation.

```
Traditional:  Program → fetches → Data → produces → Output
Addressable:  Data → addresses → Computation → emits → Response
```

Intelligence is not a process to run. Intelligence is a place to navigate to.

---

## The Trilogy

These three documents form a unified theory:

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   THE 5 PRIMES          The atoms                           │
│        ↓                                                    │
│   PERIODIC TABLE        The structure                       │
│        ↓                                                    │
│   ADDRESSABLE           The paradigm                        │
│   INTELLIGENCE                                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

1. **The 5 Primes** define the irreducible operations
2. **The Periodic Table** organizes shapes by function and complexity
3. **Addressable Intelligence** shows how data navigates the shape space

Together, they describe **frozen computation** — deterministic, verifiable, trustworthy.

---

## Implementation Documents

### [CfC + EntroMorph](CFC_ENTROMORPH.md)

Integration of Closed-form Continuous-time neural networks with the TriX evolution engine.

- CfC cells in pure C (206 ns latency)
- EntroMorph genetic algorithm (65.8M mutations/sec)
- ShapeFabric zero-copy evolution

### [Zit Detection](ZIT_DETECTION.md)

Anomaly detection via phase-locked tracking.

- **Dual-tau architecture**: Fast tracker + slow anchor
- **The core insight**: "The CfC's slowness IS its sensitivity"
- **396-byte** detector with r > 0.97 correlation

### [Soft Chips](SOFT_CHIPS.md)

Portable frozen computation units for deployment.

### [Engineering](ENGINEERING.md)

Complete buildable specification: data structures, APIs, hardware targets.

### [Architecture](ARCHITECTURE.md)

System architecture from theory to metal.

---

## Key Insights

### Shapes are Discovered, Not Invented

The ~30 shapes in Geocadesia are not arbitrary. They are the natural compounds of the 5 Primes that serve useful computational functions.

### Logic Gates are Not Elemental

```
NAND(a,b) = 1 - ab = ADD(1, MUL(-1, MUL(a,b)))
```

In the polynomial view, logic gates are molecules built from deeper atoms.

### The Hierarchy

```
Level 0: PRIMORDIALS    5 atoms
Level 1: ELEMENTALS     ~15 shapes (1-2 primes)
Level 2: COMPOUNDS      ~30 shapes (3+ primes)
Level 3: MOLECULES      ~100s (compositions)
Level 4: TISSUES        ~1000s (patterns)
Level 5: ORGANS         Unbounded (subsystems)
Level 6: ORGANISMS      Unbounded (architectures)
```

### Hardware Implications

Five primordials → Five hardware units:
- Adder (ADD)
- Multiplier (MUL)
- Exp approximator (EXP)
- Comparator (MAX)
- Constants (CONST)

Everything else is composition.

---

## Related

- **Geocadesia** — The shape catalog (`/workspace/ZOR/trixc/forge/shapes/geocadesia/`)
- **NGP** — Neural Geometric Processor hardware architecture
- **Zit Detector** — The routing mechanism: `Zit = popcount(S ⊕ input) < θ`

---

## Philosophy

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   "Five Primes. Everything else is composition."            │
│                                                             │
│   "Position in the table predicts properties."              │
│                                                             │
│   "Intelligence is not a process to run.                    │
│    Intelligence is a place to address."                     │
│                                                             │
│   "The shapes are frozen.                                   │
│    The routing is learned.                                  │
│    The trust is absolute."                                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

*"It's all in the reflexes."*
