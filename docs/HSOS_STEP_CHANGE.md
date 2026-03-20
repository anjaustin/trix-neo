# HSOS Step-Change Analysis

> What the Hollywood Squares OS is, where its ceiling is, and what the
> next architectural leap looks like.
>
> Written 2026-03-19.

---

## Current State of HSOS

### What It Is

HSOS (`zor/src/hsos.c`, 1,063 lines) is a deterministic message-passing
microkernel. It runs a fixed topology: one master node (node 0) and eight
worker nodes (nodes 1–8), connected by a star-topology message bus.

Every computation in the system — arithmetic, sorting, constraint solving —
is expressed as a message. Every message is 16 bytes, fixed format. Every
execution is observable, recordable, and replayable.

### The 16-Byte Frame — The Atom of the System

```
┌────┬────┬────┬────┬────┬────┬────────────────────────┐
│ 00 │ 01 │ 02 │ 03 │ 04 │ 05 │ 06-0F (10 bytes)        │
│type│seq │src │dst │len │flag│ payload                 │
└────┴────┴────┴────┴────┴────┴────────────────────────┘
```

All state transitions, all results, all errors are carried in this frame.
If it happened, it's a message. If it's a message, it can be recorded and
replayed.

### Opcode Namespace

| Range  | Domain               |
|--------|----------------------|
| `0x0x` | Control (PING/PONG/RESET/HALT) |
| `0x1x` | Execution (EXEC/EXEC_OK/EXEC_ERR) |
| `0x2x` | Memory (LOAD/DUMP) |
| `0x3x` | Diagnostics (STATUS/TRACE/ROUTE) |
| `0x4x` | Neural/Compute — **defined, not implemented** |
| `0x5x` | Compare-Swap (BubbleMachine) |
| `0x6x` | Constraint Field (CSP) |

### Node Architecture

Each `hsos_node_t` contains:
- **Inbox + outbox** — two ring buffers, depth 16 (power-of-2 modular arithmetic)
- **256-byte scratchpad** — intentionally small, embedded-first
- **Sequence counters** — `seq_tx` / `seq_rx` for ordering
- **Load metric** — inbox depth × 16, scaled 0–255
- **State machine** — OFFLINE → IDLE → BUSY → IDLE (or ERROR / HALTED)

### The Tick — Unit of Deterministic Time

`hsos_step()` is the heartbeat. Each tick is exactly:

```
1. node_step(master)           master processes one inbox message
2. node_step(workers[0..7])    each worker processes one inbox message
3. bus_deliver(sys)            all outboxes flushed, messages routed
4. fabric_update_directory()   load/status snapshot updated
```

Determinism is enforced by sequential execution within a tick. No threads,
no PRNG, no wall clock. Same initial state + same message sequence = same
result, always.

### The Bus — Star Topology with Broadcast

`bus_deliver()` routes per tick:
- Master outbox → workers (unicast by `dst`, or broadcast via
  `MSG_FLAG_BROADCAST`)
- Worker outbox → any node, directly (no master relay required)
- Dropped messages (offline target) counted in `bus_dropped` — observable,
  not silent

### The ALU — 12 Message-Dispatched Operations

Workers execute arithmetic via `OP_EXEC` messages:

| Op         | Behavior                                  |
|------------|-------------------------------------------|
| ADD        | 8-bit add, carry in flags byte            |
| SUB        | 8-bit sub, borrow in flags byte           |
| CMP        | Returns 0 / 1 / 255 (eq / gt / lt)       |
| AND/OR/XOR | Bitwise                                   |
| NOT        | Bitwise complement of `a`                 |
| SHIFT_L/R  | Shift by `b & 7`                          |
| PEEK       | `result = memory[a]`                      |
| POKE       | `memory[a] = b`                           |
| COPY       | `memory[b] = memory[a]`                   |

All operations return `OP_EXEC_OK` with result + flags to the sender.

### The BubbleMachine — Odd-Even Transposition Sort

Distributed sort over 8 worker nodes using odd-even transposition:

**Even phase:** pairs (1,2), (3,4), (5,6), (7,8) compare-swap simultaneously
**Odd phase:** pairs (2,3), (4,5), (6,7) compare-swap simultaneously
Repeat until a full even+odd cycle produces zero swaps.

**CSWAP protocol:**
```
A holds val_a (lower node), B holds val_b (higher node, in memory[0])

A → B:  OP_CSWAP(val_a, ascending=1)
B:      should_swap = (my_val < their_val)   // ascending order
        if swap:    B takes val_a, replies with val_b
        if no swap: B keeps val_b, replies with val_a unchanged
A:      always writes returned payload[0] to memory[0]
```

Self-correcting: A always accepts what comes back. No global lock, no
coordination. Each pair is fully independent per phase.

Convergence: N/2 even+odd cycles = 4 cycles for 8 elements. Capped at 10.

### The Constraint Field — CSP via Message-Passing

Each worker represents one cell in a constraint satisfaction problem. Domain
is a 16-bit bitset:

```
DOMAIN_FULL = 0x01FF = 0b0000000111111111
              bit k set = value (k+1) is still possible
```

**Propagation algorithm (arc consistency, AC-1):**
1. Find any singleton domain (exactly one bit set = value determined)
2. Send `OP_DOMAIN_DELTA` to all neighbors with that value's bit as removal mask
3. Neighbors apply `domain &= ~remove_mask`
4. Repeat until quiescent (no changes = fixed point)

**Every elimination is logged** as `hsos_prop_event_t`:
```c
{ tick, cell, removed_mask, reason_cell, domain_before, domain_after }
```

`hsos_constraint_why(cf, cell)` prints a full human-readable causal chain
of every elimination that led to a cell's current state. Provenance is
intrinsic, not bolted on.

### Recording & Replay

```c
hsos_start_recording(sys);   // record all bus traffic
// ... run workload ...
hsos_stop_recording(sys);    // returns message count (max 256)
hsos_replay(sys);            // reset nodes, reinject messages, re-run
```

Every message through `bus_deliver()` is copied to `record_buffer[256]`.
Replay resets all nodes via `OP_RESET`, injects the recorded messages
directly into node inboxes, and runs to completion.

**Known limitation:** record buffer is fixed at 256 messages in the struct.
Long workloads truncate silently. Needs a dynamic buffer for production.

---

## The Current Ceiling

HSOS is a **simulated** distributed system. The "distribution" is an
abstraction — all nodes run sequentially inside a single `hsos_step()` call
on one CPU. Determinism is guaranteed by single-threading, not by the
protocol.

This creates a structural gap. Three systems coexist in the repo that do not
talk to each other:

```
runtime.c     real inference, tight loop, no provenance, no traceability
hsos.c        distributed computation, full provenance, no inference
tools/        builds chips, knows nothing about either at runtime
```

The gap is not a bug. It is the next problem to solve.

---

## The Step-Change: Unified HSOS Inference

### The Signal Already in the Code

The opcode table in `hsos.h` defines:

```c
OP_COMPUTE      = 0x40,     /* Neural compute */
OP_COMPUTE_OK   = 0x41,     /* Compute success */
```

Both opcodes are **defined but never implemented**. They exist in the enum.
They have no handler in `node_step()`. This is where the system was heading.

### The Architecture

```
Input (64 bytes)
      │
      ▼  OP_COMPUTE broadcast
   Master
  (node 0)
   ├──────────────────────────────────────────────┐
   │               │               │              │
   ▼               ▼               ▼              ▼
Worker 1       Worker 2       Worker 3  ...  Worker 8
sigs[0..N/8]  sigs[N/8..]    ...              ...
popcount64()  popcount64()
   │               │
   ▼  OP_COMPUTE_OK (best local {match, distance, threshold, label})
   └───────────────────────────────────────────────┘
      │
   Master aggregates → global minimum distance wins
      │
      ▼
trix_result_t  +  trace_ref → tick range in HSOS trace buffer
```

Each worker holds a **shard of the signature space**. The master broadcasts
the input. Workers run local Hamming distance matching against their shard
using the existing `popcount64()` SIMD implementation. They reply with their
best candidate. Master picks the global minimum. The entire execution lives
in the message log.

### What This Changes

**Inference becomes legible.**

Today: "matched `label_foo` with distance 12."

After: "worker 3 found `label_foo` at distance 12 (tick 47), beating
worker 5's `label_bar` at distance 18 (tick 47), master selected worker
3's result at tick 48." Every decision has a causal chain. Every chain
is replayable.

**Inference becomes scalable.**

Signature sets are no longer bounded by single-core matching speed. Each
worker handles N/8 signatures in parallel. Adding signatures = adding
shards. Shards can be pinned to hardware cores.

**Inference becomes parallel-safe.**

Once inference is message-based, `hsos_step()` can run node steps in
parallel. Nodes share no mutable state — only the bus matters, and bus
delivery is already a single serial phase. Within a tick, nodes with no
messages to each other can execute simultaneously. That's a safe-parallelism
property the architecture already has. It is not yet exploited.

**The "addressable intelligence" claim becomes real.**

Right now that phrase lives in a philosophy document. Distributed inference
via HSOS nodes is literally addressable: you send an input to a known
address (the fabric), the fabric routes it to the right workers, workers
match against their address range of the signature space, results converge
at the master. The address *is* the computation.

### What Needs to Be Built

#### 1. `handle_compute()` in `hsos.c`

Implement `OP_COMPUTE` and `OP_COMPUTE_OK` handlers.

Worker behavior on receiving `OP_COMPUTE`:
- Payload carries the 64-byte input (fragmented across messages, or via
  an extended payload scheme)
- Worker runs `popcount64()` against each signature in its local shard
- Replies `OP_COMPUTE_OK` with `{shard_best_match, distance, threshold,
  label_idx}` to master

Master behavior on receiving `OP_COMPUTE_OK` from all workers:
- Collect results
- Pick minimum distance
- Return `trix_result_t` with attached `trace_ref`

#### 2. Signature sharding at `trix_load()` time

At load time, partition signatures across workers:
- Worker `i` owns signatures `[i * (N/8) .. (i+1) * (N/8))`
- Master holds full signature data (patterns + labels + thresholds)
- Master sends relevant signature chunks to each worker at boot via
  `OP_LOAD` messages
- Workers store shard metadata in their 256-byte scratchpad

#### 3. Parallel tick

Wrap node execution in `hsos_step()` with a thread pool:

```c
// Within hsos_step(), after bus delivery:
// Safe to parallelize: nodes share no mutable state within a tick.
parallel_for(i in 0..8) {
    node_step(&sys->workers[i]);
}
// Then serial bus delivery phase.
bus_deliver(sys);
```

On ARM: NEON-parallel paths. On x86: AVX2 + thread pool.
Determinism is preserved because bus delivery remains serial and
all inter-node communication flows only through the bus.

#### 4. Provenance in `trix_result_t`

Extend `trix_result_t` with a trace reference:

```c
typedef struct {
    int match;
    int distance;
    int threshold;
    const char* label;
    uint32_t trace_tick_start;   /* HSOS tick when inference began */
    uint32_t trace_tick_end;     /* HSOS tick when result selected */
} trix_result_t;
```

Callers can then pass `trace_tick_start..end` to `hsos_dump_trace()` to
get the full message history of any inference result.

---

## The Secondary Step-Change: Typed Protocol Contracts

Less dramatic but strategically important for certification: replace
ad-hoc payload bytes with formal message contracts.

For each opcode, define the exact preconditions, postconditions, and
invariants as structured comments (and eventually, as machine-checkable
annotations):

```c
/*
 * OP_CSWAP contract:
 *   pre:       payload[0] = value (uint8), payload[1] ∈ {0, 1}
 *   post:      payload[0] = returned_value
 *              payload[1] = 1 if swap occurred, 0 otherwise
 *   invariant: min(their_val, my_val) ends at the lower-numbered node
 *              max(their_val, my_val) ends at the higher-numbered node
 *              (for ascending=1)
 */
```

Verifiable at runtime in debug builds. Statically checkable with a model
checker (TLA+, SPIN). This is what turns "deterministic microkernel" into
"formally verified microkernel" — a categorically different artifact for
safety certification (DO-178C Level A, ISO 26262 ASIL-D).

---

## Implementation Priority

| Step | What | Why |
|------|------|-----|
| 1 | Implement `OP_COMPUTE` / `OP_COMPUTE_OK` | Closes the architecture — the opcodes are already reserved |
| 2 | Signature sharding at `trix_load()` | Enables distributed inference without changing the API |
| 3 | Parallel tick (thread pool in `hsos_step`) | Real performance gain; architecture already supports it |
| 4 | `trace_ref` in `trix_result_t` | Provenance at the API level; callers can inspect any result |
| 5 | Typed protocol contracts | Certification enabler; can be done incrementally per opcode |

---

## Known Gaps to Fix Before Any of This

1. **Record buffer is fixed at 256 messages** — silently truncates. Replace
   with a dynamic ring buffer or a configurable compile-time constant before
   using recording for production inference traces.

2. **`hsos_replay()` does a full node reset** — replay injects messages into
   inboxes directly, bypassing the bus. This means replayed messages skip the
   `bus_delivered` counter and recording logic. Fix: route replay through
   `bus_deliver()` with a replay flag, not directly into inboxes.

3. **No fragmentation for large payloads** — `OP_COMPUTE` needs to carry 64
   bytes of input. The current payload max is 10 bytes. `MSG_FLAG_FRAGMENT`
   and `MSG_FLAG_LAST_FRAG` are already defined in the flag byte; the
   reassembly logic needs to be written.

4. **Constraint field is limited to 8 cells** — tied to the 8-worker
   topology. For larger CSPs, either chain systems or extend to virtual nodes
   (multiple logical cells per physical worker).

---

*Written 2026-03-19. See also: `DIAMOND.md` (repo audit),
`docs/HSOS_LMM.md` (Lincoln Manifold Method applied to step-change ideas).*
