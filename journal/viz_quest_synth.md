# Synthesis: TriX Visualization Quest

The clean cut. The wood splits here.

---

## The Claim

**Visualization is proof, not polish.**

TriX's glassbox nature means every computation can be shown. The visualization doesn't explain TriX - it IS TriX, made visible.

---

## Architecture

```
┌─────────────────────────────────────────────────────────┐
│                    VISUALIZATION STACK                   │
├─────────────────────────────────────────────────────────┤
│                                                          │
│   Level 4: viz_train.py    ──── Training observer        │
│        ▲                         (sidecar, real-time)    │
│        │                                                 │
│   Level 3: viz_flow.py     ──── Data flow animation      │
│        ▲                         (ripple carry, bits)    │
│        │                                                 │
│   Level 2: viz_trace.py    ──── Single-step trace  ◄─── MVP
│        ▲                         (input→route→compute)   │
│        │                                                 │
│   Level 1: viz_shapes.py   ──── Static diagrams          │
│                                  (polynomials, circuits) │
│                                                          │
├─────────────────────────────────────────────────────────┤
│   viz_core.py              ──── Shared rendering utils   │
│                                  (boxes, colors, tables) │
└─────────────────────────────────────────────────────────┘
```

---

## Implementation Plan

### Phase 1: Foundation (viz_core.py)

Terminal rendering primitives:
- `Box`: Draw boxes with titles
- `Table`: Aligned columns
- `Color`: ANSI with graceful degradation
- `clear()`, `pause()`, `animate()`

Design principles:
- 80-column max width (configurable)
- Monochrome-first, color-enhanced
- UTF-8 box drawing characters

### Phase 2: MVP (viz_trace.py)

Single operation trace with four frames:
1. **INPUT**: Show operands
2. **ROUTE**: Show shape selection (for ALU ops)
3. **COMPUTE**: Show formula evaluation step-by-step
4. **OUTPUT**: Show result

Supported operations (start small):
- `xor a b` - The hello world
- `and a b`, `or a b`, `not a` - Logic gates
- `add a b` - Full adder (shows carry)
- `alu OP a b` - 6502 ALU operation

Interaction:
- Default: step-by-step (Enter to advance)
- `--auto`: timed animation
- `--instant`: just show final state

### Phase 3: Static Reference (viz_shapes.py)

Educational diagrams (no execution):
- All 16 frozen shapes with polynomials
- Full adder circuit diagram
- Ripple carry structure
- 6502 routing table

Printable. Study material.

### Phase 4: Flow Animation (viz_flow.py)

Watch bits flow:
- 8-bit ripple adder
- Carry propagation visualized
- Bit-by-bit animation

### Phase 5: Training Observer (viz_train.py)

Sidecar architecture:
- TriX trainer writes events to pipe/file
- Visualizer reads and renders
- Real-time routing table updates
- Loss curves in ASCII

---

## File Structure

```
/workspace/trix/
├── zor/                    # Pure C (unchanged)
└── viz/                    # Python visualization
    ├── viz_core.py         # Rendering primitives
    ├── viz_trace.py        # MVP: single-op trace
    ├── viz_shapes.py       # Static diagrams
    ├── viz_flow.py         # Flow animation
    ├── viz_train.py        # Training observer
    └── README.md           # Usage guide
```

---

## MVP Specification: viz_trace.py

### Usage

```bash
# Hello XOR
python viz/viz_trace.py xor 0 1

# Logic gates
python viz/viz_trace.py and 1 1
python viz/viz_trace.py or 0 1
python viz/viz_trace.py not 1

# Full adder
python viz/viz_trace.py add 1 1 0    # a=1, b=1, cin=0

# 6502 ALU
python viz/viz_trace.py alu ADC 42 13
python viz/viz_trace.py alu EOR 0x55 0xFF
python viz/viz_trace.py alu ASL 0x80

# Options
python viz/viz_trace.py xor 0 1 --auto      # animated
python viz/viz_trace.py xor 0 1 --instant   # no pause
```

### Output Example (XOR)

```
╔═════════════════════════════════════════╗
║  TRACE: XOR                             ║
╠═════════════════════════════════════════╣
║                                         ║
║  ┌─ INPUT ────────────────────────────┐ ║
║  │  a = 0.0                           │ ║
║  │  b = 1.0                           │ ║
║  └────────────────────────────────────┘ ║
║                                         ║
║  ┌─ FORMULA ──────────────────────────┐ ║
║  │  XOR(a, b) = a + b - 2·a·b         │ ║
║  └────────────────────────────────────┘ ║
║                                         ║
║  ┌─ COMPUTE ──────────────────────────┐ ║
║  │  step 1:  a + b     = 0.0 + 1.0    │ ║
║  │                     = 1.0          │ ║
║  │                                    │ ║
║  │  step 2:  2·a·b     = 2 × 0.0 × 1.0│ ║
║  │                     = 0.0          │ ║
║  │                                    │ ║
║  │  step 3:  subtract  = 1.0 - 0.0    │ ║
║  │                     = 1.0          │ ║
║  └────────────────────────────────────┘ ║
║                                         ║
║  ┌─ OUTPUT ───────────────────────────┐ ║
║  │  result = 1.0                      │ ║
║  │  binary = 1  ✓                     │ ║
║  └────────────────────────────────────┘ ║
║                                         ║
╚═════════════════════════════════════════╝
```

---

## Success Criteria

- [ ] `viz_core.py` renders boxes correctly in 80-col terminal
- [ ] `viz_trace.py xor 0 1` produces clear, correct output
- [ ] All four logic gates work (XOR, AND, OR, NOT)
- [ ] Full adder shows sum and carry
- [ ] 6502 ALU shows routing + execution
- [ ] `--auto` animates smoothly
- [ ] Works without color (`NO_COLOR=1`)
- [ ] Works on Linux, macOS, basic Windows terminal

---

## What We're NOT Building (Yet)

- GUI visualization
- Web interface
- Real-time GPU visualization
- Multi-operation traces
- Diff between operations
- Recording/playback

These can come later. MVP first.

---

## The Mantra

**If you can't show it, you don't understand it.**
**If you can show it, you've proven there's no magic.**

TriX visualization isn't about making things pretty.
It's about making the invisible undeniable.

---

*The shapes are frozen. The math is eternal. Now we make them visible.*
