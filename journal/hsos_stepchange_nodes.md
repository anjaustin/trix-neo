# HSOS Step-Change — NODES

*Phase 2 of Lincoln Manifold Method. Extracted from RAW. 2026-03-19.*

---

## Nodes

**1. The opcodes are reserved but the plumbing is missing.**
OP_COMPUTE and OP_COMPUTE_OK are defined in the enum. No handler exists in
node_step(). No fragmentation reassembly exists. No shard routing exists.
The architectural intent is clear. The concrete implementation has three
separate blockers, not one.

**2. Fragmentation is the hardest and most concrete blocker.**
Max payload is 10 bytes. Input is 64 bytes. Delivery requires 7 fragments
per input. MSG_FLAG_FRAGMENT and MSG_FLAG_LAST_FRAG are defined, but
reassembly is entirely absent. Without fragmentation, OP_COMPUTE cannot
carry its primary argument. This blocks everything else.

**3. The record buffer caps at 256 messages — a structural limit, not a
detail.**
A single inference produces at minimum 9 messages (1 broadcast + 8 replies),
rising to ~65 with full fragmentation. Four inferences exhaust the buffer.
Any production use of HSOS inference requires a dynamic or ring record buffer
before provenance is trustworthy.

**4. Parallel tick safety requires more than "nodes don't share mutable
state."**
System-level counters (sys->tick, bus_delivered, bus_dropped, trace_head,
record_count) are all written during what would become parallel phases. True
parallel execution requires either atomic operations on these counters or
moving all such writes into the serial bus_deliver phase. The node-local
state is safe; the fabric state is not.

**5. Provenance is the unique value proposition — not performance.**
Serial Hamming distance matching is already fast (SIMD popcount, 235 GOP/s
claimed). HSOS inference is not faster for small N. The reason to build it is
the message trace: every comparison is observable, every rejection is logged,
every result is attributable to a specific worker at a specific tick. That's
auditable inference. No other edge inference runtime has this.

**6. Sharding breaks for small signature counts.**
If N < 8, most workers get zero signatures. Zero-signature workers still
receive and process the OP_COMPUTE broadcast, reply with no-match, and
contribute messages with no value. This is strictly worse than serial
matching. A floor is required: below some threshold N_min, fall back to the
tight loop in runtime.c. Hybrid routing is therefore mandatory, not optional.

**7. Replay is not faithfully deterministic — it's a shortcut.**
hsos_replay() injects messages directly into node inboxes, bypassing
bus_deliver(). Replayed messages skip bus recording, bus counters, and any
bus-level logic. If bus_deliver() ever gains routing side effects (priority
reordering, rate limiting, logging), replay produces different internal state
from the original run while producing the same node output. This is a
latent correctness bug. The certification claim depends on replay being exact.

**8. The constraint field already demonstrates the pattern HSOS inference
needs.**
OP_DOMAIN_DELTA is broadcast to neighbors, each neighbor applies an
elimination, replies, and the master loops to fixed point. This is
structurally identical to what OP_COMPUTE should do: broadcast input, collect
local results, pick global best. The constraint field is the prototype.
HSOS inference is the generalization.

**9. Typed protocol contracts are only valuable if they can be checked.**
Comments per opcode describing pre/post conditions have zero enforcement
value. The minimum useful form: runtime assertions in debug builds (stripped
by NDEBUG). The maximum useful form: a model checker (TLA+/SPIN). The
minimum useful form is achievable now. The maximum useful form is a separate
project.

**10. The real connection to close is runtime.c ↔ hsos.c, not a new
feature.**
Both systems work. Neither knows the other exists. The step-change is
structural integration, not a capability addition. Everything else —
parallelism, extended replay, contract verification — is downstream of that
connection being made correctly and cleanly.

**11. There may be a compiler-target direction worth noting but not
pursuing.**
Using HSOS to generate verified chip specs (running the design process
through the constraint field) is a genuinely different direction. It is not
the next step. It is a future research direction that the current architecture
could support. Flag it, don't chase it now.

**12. The tick model may not survive real compute loads inside a worker.**
A worker running popcount64 against N/8 signatures inside a single tick is
fine for small N. As N grows, a single worker step may take significantly
longer than a control message step. The cooperative tick model has no
preemption. One overloaded worker delays the whole tick. This needs a design
decision: bound compute per tick (max signatures per worker per step) or
accept that inference ticks are longer than control ticks.

---

## Tensions

**Tension A: Provenance value vs. implementation complexity.**
The provenance story is the unique differentiator. But achieving trustworthy
provenance requires fixing the record buffer, fixing replay faithfulness, and
implementing fragmentation — all before the first inference message is sent.
The value is real; the path to it is longer than it appears.

**Tension B: Parallel performance vs. serial correctness guarantees.**
Making the tick parallel adds real-world performance on multi-core hardware.
But it requires atomics or restructured counter updates, adding complexity to
a system whose primary value is simplicity and verifiability. More complexity
in the core = harder to certify. The performance gain may not be worth the
certification cost at this stage.

**Tension C: Hybrid routing (serial fallback) vs. architectural purity.**
A clean architecture says: inference always goes through HSOS. A practical
architecture says: for N < threshold, bypass HSOS entirely. The hybrid is
more correct but less pure. The pure version is elegant but broken for small
N. This tension must be resolved explicitly.

**Tension D: Now vs. later for typed contracts.**
Doing contracts now, as part of the initial OP_COMPUTE implementation, costs
time but produces a cleaner foundation. Doing contracts later means
retrofitting them onto code that already has assumptions baked in. The right
time is during implementation, not before or after.

---

## Constraints

- Payload max is 10 bytes. Fragmentation must be implemented before
  OP_COMPUTE works.
- Record buffer is 256 messages. Must be dynamic before provenance is
  production-trustworthy.
- hsos_replay() bypasses bus_deliver(). Must be fixed before replay is
  a certification artifact.
- The 8-worker topology is fixed. Sharding is therefore fixed-width. Any
  signature count that doesn't divide evenly by 8 leaves workers unbalanced.
- Parallel tick requires system-counter changes. Not zero-cost.
