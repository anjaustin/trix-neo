# TriX Visualization

**Terminal-based visualization for frozen shape computation.**

*"If you can show it, there's no magic."*

---

## Quick Start

```bash
# Hello XOR - the simplest frozen shape
python viz/viz_trace.py xor 0 1

# Logic gates
python viz/viz_trace.py and 1 1
python viz/viz_trace.py or 0 1
python viz/viz_trace.py not 1

# Full adder (single bit)
python viz/viz_trace.py add 1 1 0

# 8-bit ripple carry adder
python viz/viz_trace.py ripple 42 13
```

---

## What You See

Every visualization shows four frames:

1. **INPUT** - What goes in
2. **FORMULA** - The frozen polynomial
3. **COMPUTE** - Step-by-step evaluation
4. **OUTPUT** - The result (with verification)

---

## Files

| File | Description |
|------|-------------|
| `viz_core.py` | Rendering primitives (boxes, colors, formatting) |
| `viz_trace.py` | Single-operation trace (the MVP) |

---

## Supported Operations

### Logic Gates
| Op | Formula | Example |
|----|---------|---------|
| `xor` | `a + b - 2ab` | `viz_trace.py xor 0 1` |
| `and` | `a * b` | `viz_trace.py and 1 1` |
| `or` | `a + b - ab` | `viz_trace.py or 0 1` |
| `not` | `1 - a` | `viz_trace.py not 1` |

### Arithmetic
| Op | Description | Example |
|----|-------------|---------|
| `add` | Full adder (1-bit) | `viz_trace.py add 1 1 0` |
| `ripple` | 8-bit ripple adder | `viz_trace.py ripple 42 13` |

---

## Options

```bash
--step      Step-by-step (default, press Enter to continue)
--auto      Auto-advance with animation
--instant   No pauses, show final result immediately
```

---

## Philosophy

Traditional neural networks are black boxes. You feed in data, magic happens, answers emerge.

TriX is a glass box. Every computation is a frozen polynomial. Every step is visible.

This visualization doesn't explain TriX - it IS TriX, made visible.

---

## Proof of Concept Demos

For application-specific demos (like the 6502 ALU), see `proofs/`:

```bash
# 6502 ALU visualization (demo, not core TriX)
python proofs/6502/viz_6502.py ADC 42 13
python proofs/6502/viz_6502.py EOR 0x55 0xFF
```

---

## Roadmap

- [x] **Level 1**: viz_core.py - Rendering primitives
- [x] **Level 2**: viz_trace.py - Single-step trace (MVP)
- [ ] **Level 3**: viz_shapes.py - Static shape diagrams
- [ ] **Level 4**: viz_flow.py - Animated data flow
- [ ] **Level 5**: viz_train.py - Training observer (sidecar)

---

*The shapes are frozen. The math is eternal. Now you can see it.*
