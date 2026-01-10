# Nodes of Interest: TriX Visualization Quest

Extracted from RAW phase. The grain of the wood.

---

## Node 1: The Glassbox Advantage

TriX can be visualized because every step is interpretable. Traditional neural nets can't do this - their computation is opaque. This is a unique selling point we're not exploiting.

**Why it matters:** Visualization isn't a nice-to-have, it's proof of the paradigm. If you can show every step, you've proven there's no black box.

---

## Node 2: Terminal-First Philosophy

Visualization in terminal (ASCII/ANSI) rather than GUI. Aligns with TriX's "no dependencies" ethos.

**Why it matters:** A GUI visualization would betray the philosophy. Terminal works everywhere - SSH, embedded, any platform.

**Tension:** Terminal constraints (80x24, limited graphics) vs desire to show complex flows.

---

## Node 3: The 6502 as Teaching Tool

The 6502 is small enough to visualize completely. 8 bits, 16 shapes, finite state. Human-scale complexity.

**Why it matters:** You can't visualize a transformer meaningfully. But you CAN watch carry propagate through 8 full adders. That's teachable.

---

## Node 4: The "Aha Moment"

The moment someone sees `XOR(0,1) = 0 + 1 - 2*0*1 = 1` compute correctly - that's when they GET it.

**Why it matters:** We're not trying to impress. We're trying to illuminate. The visualization succeeds when it creates understanding.

---

## Node 5: Progressive Complexity

Four levels emerged:
1. **Static** - Show shapes as diagrams
2. **Trace** - Step through one computation
3. **Flow** - Watch data move through shapes
4. **Train** - Watch routing emerge over time

**Why it matters:** Crawl, walk, run. Don't overwhelm. Each level builds on the last.

**Tension with Node 4:** The aha moment might come at level 2, not level 4. Don't over-engineer.

---

## Node 6: Python for Visualization

Core TriX stays C. Visualization can be Python - it's scaffolding, not load-bearing.

**Why it matters:** Python is quick to prototype, has good terminal libraries (rich, curses). We can iterate fast.

**Tension:** Do we eventually want pure C visualization too? For embedded demos?

---

## Node 7: The Sidecar Architecture

Two processes: Trainer emits events, Visualizer consumes and renders.

**Why it matters:** Separation of concerns. Visualization doesn't slow training.

**Tension with Node 5:** Sidecar is level 4 thinking. We're starting at level 1-2. Premature?

---

## Node 8: What to Show vs What to Hide

Temptation to visualize EVERYTHING. But that's noise.

**Why it matters:** Clarity requires selection. We need to find the essential frames.

**Key question:** What's the minimum visualization that creates the aha moment?

---

## Node 9: The Polynomial vs The Circuit

Two ways to show XOR:
- As math: `a + b - 2ab`
- As circuit: gates connected with wires

**Why it matters:** Different audiences. Math people want the polynomial. Hardware people want the circuit. Beginners want... both? Neither?

---

## Node 10: Color and Accessibility

ANSI colors can enhance (positive=green, negative=red, carry=yellow). But not all terminals support them.

**Why it matters:** We want beauty but also universality.

**Tension:** Design for lowest common denominator (no color) or progressive enhancement?

---

## Node 11: Interactivity

Step-by-step (press key to advance) vs animated (auto-play with timing).

**Why it matters:** Learning requires control. But demos benefit from flow.

**Resolution:** Support both. Default to interactive, flag for animation.

---

## Node 12: The Entry Point

Where does someone START? What's the first thing they see?

**Why it matters:** First impressions. If the entry point is confusing, they leave.

**Candidate:** A single XOR operation. The simplest possible frozen shape. "Hello, XOR."

---

## Summary of Tensions

1. Terminal constraints vs visual richness
2. Simplicity vs completeness
3. Python scaffolding vs C purity
4. Sidecar elegance vs immediate utility
5. Show everything vs show essentials
6. Math representation vs circuit representation
7. Color beauty vs universal accessibility
8. Interactive control vs animated flow
