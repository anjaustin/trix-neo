# HSOS Step-Change — REFLECT

*Phase 3 of Lincoln Manifold Method. Patterns and leverage. 2026-03-19.*

---

## The Pattern Under Everything

Every blocker is downstream of a single missed primitive: **message
fragmentation.**

The payload cap is 10 bytes. Input is 64 bytes. Without reassembly, nothing
that requires carrying real data moves. Not OP_COMPUTE. Not extended replay.
Not a dynamic record buffer (which needs to communicate its state). The system
was designed as a control-message bus — short opcodes with short arguments.
The moment you try to use it as a data bus, the seams show.

The fragmentation primitives are *already in the flag byte*:
`MSG_FLAG_FRAGMENT`, `MSG_FLAG_LAST_FRAG`. They were designed in. The
reassembly logic just wasn't written. This isn't a missing insight — it's a
missing function.

**Fix fragmentation first. Everything else unblocks.**

---

## The Core Insight

> HSOS inference is not about speed. It's about making inference an
> observable, auditable, replayable event — not a black-box function call.

This resolves Tension A. The complexity of the implementation path (fragment
reassembly → shard routing → record buffer expansion → replay fix) is
justified *because* provenance is the value. If provenance were not the
value, you'd never build any of this — you'd just run runtime.c and be done.
The implementation complexity is load-bearing. It is the product.

---

## Assumption Challenges

**Assumption: "Parallel tick is the performance story."**
Challenged. Parallel tick adds complexity without changing the certification
posture. At small N (the most common case for HSOS in its initial deployment),
parallel inference is slower than serial because of message overhead. The
performance gain only materializes at large N. Defer parallel tick until there
is a concrete signature count that justifies it. Don't build it speculatively.

**Assumption: "Sharding must be balanced across all 8 workers."**
Challenged. Static balanced sharding is fragile. A better model: shard to
however many workers have signatures. If N = 3, use 3 workers. Worker
assignment is a load-time decision made by the master, not a compile-time
constant. The fabric directory already tracks worker capabilities — this is
exactly what `CAP_NEURAL_ALU` and `CAP_NEURAL_CMP` were for.

**Assumption: "Typed protocol contracts are premature."**
Partially challenged. Full model-checker integration is premature. But writing
the contracts *during* OP_COMPUTE implementation — as machine-readable
`HSOS_ASSERT` macros in debug builds — costs almost nothing and creates the
foundation for formal verification later. The cost of retrofit is higher than
the cost of inclusion. Write them now.

**Assumption: "Replay is good enough for the certification claim."**
Challenged and confirmed as a real bug. Replay injecting directly into inboxes
is an approximation. For a system positioning itself for DO-178C or ISO 26262,
"approximation" is disqualifying. The fix is: route replay messages through
`bus_deliver()` with a replay-mode flag that suppresses the re-recording of
replayed messages. One flag, one conditional. Low cost, high correctness value.

---

## Where Real Leverage Lives

**Leverage point 1: Fragmentation reassembly.**
One function, ~50 lines. Unblocks everything. Highest ROI of any single
change.

**Leverage point 2: Replay faithfulness fix.**
One flag in `bus_deliver()`. ~10 lines. Turns the certification claim from
"we have replay" to "we have verified replay." Disproportionate value for its
cost.

**Leverage point 3: Dynamic worker assignment at shard time.**
Rather than assuming all 8 workers hold shards, the master assigns shards
only to workers with sufficient capability and non-zero signature count.
Solves the small-N problem without a separate fallback path. The hybrid
routing concern (Tension C) dissolves if shard assignment is dynamic — the
system naturally uses only the workers it needs.

**Leverage point 4: OP_COMPUTE handler + constraint field as the template.**
The constraint field's OP_DOMAIN_DELTA propagation loop is structurally
identical to what HSOS inference needs. The implementation path is: read
hsos_constraint_propagate(), generalize the broadcast-collect-aggregate
pattern, implement it for OP_COMPUTE. Don't design from scratch.

---

## Tensions Resolved

**Tension A (Provenance value vs. implementation complexity):**
Resolved. The complexity is the point. The implementation path is correct.
Sequence it: fragmentation → record buffer → replay fix → OP_COMPUTE handler
→ shard assignment → contracts. Don't shortcut any of these.

**Tension B (Parallel performance vs. serial correctness):**
Resolved by deferral. Parallel tick is not part of the step-change. It is a
future optimization gated on measuring actual inference throughput at scale.
Build it when there is a number that demands it.

**Tension C (Hybrid routing vs. architectural purity):**
Resolved by dynamic shard assignment. If the master assigns shards only to
workers with non-zero signature counts, the system handles N < 8 gracefully
without a separate fallback. Architectural purity is preserved.

**Tension D (Typed contracts now vs. later):**
Resolved. Write them now as debug-mode assertions. Machine-readable,
zero production cost, foundation for future formal verification. Include
during OP_COMPUTE implementation, not before or after.

---

## What the Step-Change Actually Is

It is not "implement OP_COMPUTE."

It is: **make inference a first-class HSOS computation with intrinsic
provenance, faithful replay, and dynamic worker assignment** — delivered
as a sequence of four self-contained, testable changes.

The step-change is structural. It closes the gap between runtime.c and hsos.c.
Everything else is downstream.
