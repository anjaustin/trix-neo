# Hollywood Squares OS — TriX Implementation

> **"Structure is meaning."**

> **"The wiring determines the behavior.**
> **The messages carry the computation.**
> **The trace tells the story."**

This document describes the TriX implementation of Hollywood Squares OS, a distributed microkernel for semantic computation. Port of [anjaustin/hollywood-squares-os](https://github.com/anjaustin/hollywood-squares-os) to frozen C.

---

## Table of Contents

1. [Core Philosophy](#core-philosophy)
2. [Architecture](#architecture)
3. [Message Protocol](#message-protocol)
4. [Node Kernel](#node-kernel)
5. [Fabric Kernel](#fabric-kernel)
6. [BubbleMachine](#bubblemachine)
7. [ConstraintField](#constraintfield)
8. [API Reference](#api-reference)
9. [Running the Demo](#running-the-demo)

---

## Core Philosophy

Hollywood Squares OS is **NOT** a traditional operating system. It does **NOT** manage:
- CPU time
- Memory pressure
- I/O bandwidth

Instead, it manages:
- **Causality** — Event ordering
- **Message determinism** — Reproducible delivery
- **Semantic execution** — Meaningful computation

### The Key Result

```
Deterministic message passing
+ Bounded local semantics
+ Enforced observability
= Global convergence with inherited correctness
```

### Topology = Algorithm

The same handlers connected with different wiring produce different algorithms:

```
Same handlers + LINE topology = Odd-even transposition sort
Same handlers + RING topology = Circular bubble sort
Same handlers + GRID topology = 2D checkerboard sort
```

**The wiring IS the algorithm.**

---

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    HOLLYWOOD SQUARES OS                      │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│   ┌─────────┐     ┌─────────────────────────────────┐       │
│   │ MASTER  │     │         WORKER NODES            │       │
│   │ (node 0)│     │  ┌───┬───┬───┬───┬───┬───┬───┬───┐     │
│   │         │◄───►│  │ 1 │ 2 │ 3 │ 4 │ 5 │ 6 │ 7 │ 8 │     │
│   │ Fabric  │     │  └───┴───┴───┴───┴───┴───┴───┴───┘     │
│   │ Kernel  │     │         NodeKernel × 8                  │
│   └─────────┘     └─────────────────────────────────┘       │
│        │                        │                            │
│        └────────────────────────┘                            │
│                  MESSAGE BUS                                 │
│           (Star topology, 16-byte frames)                    │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

### Components

| Component | Description |
|-----------|-------------|
| **Master Node** | Node 0, runs FabricKernel for coordination |
| **Worker Nodes** | Nodes 1-8, run NodeKernel for computation |
| **Message Bus** | Star topology router, deterministic delivery |
| **Trace Buffer** | Circular log of system events |

---

## Message Protocol

All communication uses fixed 16-byte message frames.

### Frame Layout

```
Offset  Field    Description
──────────────────────────────────
$00     type     Message opcode
$01     seq      Sequence number
$02     src      Source node ID
$03     dst      Destination node ID
$04     len      Payload length (0-10)
$05     flags    Message flags
$06-$0F payload  Up to 10 bytes data
```

### Opcodes

| Category | Opcode | Value | Description |
|----------|--------|-------|-------------|
| **Control** | NOP | 0x00 | No operation |
| | PING | 0x01 | Connectivity check |
| | PONG | 0x02 | Ping response |
| | RESET | 0x03 | Reset node |
| | HALT | 0x04 | Stop node |
| **Execution** | EXEC | 0x10 | Execute operation |
| | EXEC_OK | 0x11 | Execution success |
| | EXEC_ERR | 0x12 | Execution error |
| **Compare-Swap** | CSWAP | 0x50 | Compare-swap request |
| | CSWAP_OK | 0x51 | Compare-swap response |
| **Constraint** | DOMAIN_GET | 0x60 | Get domain bitset |
| | DOMAIN_SET | 0x61 | Set domain bitset |
| | DOMAIN_DELTA | 0x62 | Remove values |
| | IS_SINGLETON | 0x63 | Check single value |
| | GET_VALUE | 0x64 | Get fixed value |

### Flags

| Flag | Value | Description |
|------|-------|-------------|
| ACK_REQ | 0x01 | Acknowledgment required |
| PRIORITY | 0x02 | High priority |
| FRAGMENT | 0x04 | Part of fragmented message |
| LAST_FRAG | 0x08 | Last fragment |
| BROADCAST | 0x10 | Send to all nodes |

---

## Node Kernel

The NodeKernel runs on every processor (master and workers).

### State Machine

```
OFFLINE ──► IDLE ◄──► BUSY
              │         │
              ▼         ▼
           ERROR     HALTED
```

| State | Value | Description |
|-------|-------|-------------|
| OFFLINE | 0x00 | Inactive, not responding |
| IDLE | 0x01 | Ready for work |
| BUSY | 0x02 | Processing a message |
| ERROR | 0x03 | Fault condition |
| HALTED | 0x04 | Stopped |

### Components

```c
typedef struct {
    /* Identity */
    uint8_t node_id;
    hsos_node_state_t state;

    /* Message queues */
    hsos_msg_t inbox[16];
    hsos_msg_t outbox[16];

    /* Memory (256 bytes) */
    uint8_t memory[256];

    /* Statistics */
    uint32_t msgs_rx, msgs_tx, errors;
    uint8_t load;
} hsos_node_t;
```

### ALU Operations

| Operation | Code | Description |
|-----------|------|-------------|
| ADD | 0x00 | a + b (with carry) |
| SUB | 0x01 | a - b (with borrow) |
| CMP | 0x02 | Compare a and b |
| AND | 0x03 | a & b |
| OR | 0x04 | a \| b |
| XOR | 0x05 | a ^ b |
| NOT | 0x06 | ~a |
| SHIFT_L | 0x07 | a << b |
| SHIFT_R | 0x08 | a >> b |
| PEEK | 0x09 | Read memory[a] |
| POKE | 0x0A | Write b to memory[a] |

---

## Fabric Kernel

The FabricKernel runs only on the master node (node 0).

### Services

| Service | Description |
|---------|-------------|
| **Directory** | Node registry with status, load, capabilities |
| **Router** | Load-aware message routing |
| **Supervisor** | Heartbeat monitoring, health checks |
| **Loader** | Program deployment |

### Routing Algorithm

```c
uint8_t fabric_route_least_loaded(hsos_system_t *sys) {
    // Find idle node with lowest load
    // Fallback: round-robin among idle nodes
}
```

---

## BubbleMachine

Distributed sorting via compare-swap operations.

### The Insight

> "Every comparison is a message. Every swap is traceable.
>  Every execution is replayable."

### Topology: Odd-Even Transposition

```
EVEN phase: pairs (1,2), (3,4), (5,6), (7,8)
ODD phase:  pairs (2,3), (4,5), (6,7)
```

### Compare-Swap Protocol

```
A sends:    CSWAP(value_a, ascending)
B receives: Compares value_a with value_b
            If swap needed: B ← value_a, reply(value_b, swapped=1)
            If no swap:     B keeps value_b, reply(value_a, swapped=0)
A receives: CSWAP_OK(returned_value, swapped)
            A ← returned_value
```

### Example

```
Input:  [64, 25, 12, 22, 11, 90, 42,  7]
Output: [ 7, 11, 12, 22, 25, 42, 64, 90]
Cycles: 5 | Swaps: 18 | Messages: 35
```

### API

```c
hsos_bubble_t bm;
hsos_bubble_init(&bm, sys, TOPO_LINE);
hsos_bubble_load(&bm, input);
hsos_bubble_run(&bm);
hsos_bubble_read(&bm, output);
```

---

## ConstraintField

Distributed CSP solver via constraint propagation.

### The Insight

> "Every elimination has a reason. Every reason is traceable.
>  Every solution is explainable."

> "This system doesn't search for solutions. It relaxes toward them."

### Domain Representation

Each cell maintains a 16-bit bitset where bits 0-8 represent values 1-9:

```c
#define DOMAIN_FULL  0x01FF  /* All 9 values possible */

/* Check if singleton */
if (popcount(domain) == 1) {
    value = find_bit_position(domain) + 1;
}
```

### Propagation Algorithm

```
REPEAT:
    FOR each singleton cell with value V:
        FOR each neighbor N:
            IF N is not singleton AND N contains V:
                Eliminate V from N's domain
                Log elimination event
UNTIL no eliminations occur (fixed point)
```

### Explainability: `why(cell)`

```
═══ WHY cell 7? ═══
Cell 7 = 8

History:
[t= 157] eliminated { 1 } via cell(0)=1
[t= 157] eliminated { 2 } via cell(1)=2
[t= 157] eliminated { 3 } via cell(2)=3
[t= 157] eliminated { 4 } via cell(3)=4
[t= 157] eliminated { 5 } via cell(4)=5
[t= 157] eliminated { 6 } via cell(5)=6
[t= 157] eliminated { 7 } via cell(6)=7
```

### API

```c
hsos_constraint_t cf;
hsos_constraint_init(&cf, sys);
hsos_constraint_set_given(&cf, 0, 3);  /* Cell 0 = 3 */
hsos_constraint_propagate(&cf);
uint8_t value = hsos_constraint_get_value(&cf, 7);
hsos_constraint_why(&cf, 7);
```

---

## API Reference

### System Control

```c
void hsos_init(hsos_system_t *sys);
int hsos_boot(hsos_system_t *sys);
bool hsos_step(hsos_system_t *sys);
int hsos_run(hsos_system_t *sys, int max_ticks);
```

### Execution

```c
int hsos_exec(hsos_system_t *sys, uint8_t node,
              hsos_alu_op_t op, uint8_t a, uint8_t b);
void hsos_broadcast_exec(hsos_system_t *sys,
                         hsos_alu_op_t op, uint8_t a, uint8_t b);
int hsos_route_exec(hsos_system_t *sys, hsos_alu_op_t op,
                    uint8_t a, uint8_t b, uint8_t *node);
```

### Recording & Replay

```c
void hsos_start_recording(hsos_system_t *sys);
int hsos_stop_recording(hsos_system_t *sys);
void hsos_replay(hsos_system_t *sys);
```

### Introspection

```c
hsos_node_state_t hsos_node_status(hsos_system_t *sys, uint8_t node);
bool hsos_ping(hsos_system_t *sys, uint8_t node);
void hsos_stats(hsos_system_t *sys, uint32_t *ticks,
                uint32_t *delivered, uint32_t *dropped);
void hsos_dump_trace(hsos_system_t *sys);
```

---

## Running the Demo

```bash
cd zor
make hsos
```

Output demonstrates:
1. **System Boot** — 8/8 workers online
2. **BubbleMachine** — Distributed sorting
3. **ConstraintField** — CSP solving with explainability
4. **Cascade** — Forced solution via propagation
5. **Recording & Replay** — Deterministic execution

---

## Files

| File | Description |
|------|-------------|
| `include/hsos.h` | Header with all types and API |
| `src/hsos.c` | Complete implementation |
| `examples/09_hsos_demo.c` | Demo program |

---

## Connection to TriX

| HSOS Concept | TriX Equivalent |
|--------------|-----------------|
| Message passing | XOR propagation through Resonance Cube |
| Compare-swap | Zit detection (Hamming distance check) |
| Constraint propagation | Frozen shape activation |
| Node topology | 8×8×8 = 512-bit cube structure |
| Explainability | Deterministic trace |
| "Relaxation" | CfC + EntroMorph evolution |

Both systems share the philosophy:
- **Frozen computation** — Deterministic, verifiable
- **Topology = behavior** — Structure encodes algorithm
- **Full observability** — Every decision traceable

---

## References

1. [anjaustin/hollywood-squares-os](https://github.com/anjaustin/hollywood-squares-os) — Original Python implementation
2. TriX Documentation: THE_5_PRIMES.md, PERIODIC_TABLE.md
3. Resonance Cube journals: `journal/resonance_cube_*.md`

---

*"The field relaxes. Structure is meaning."*

*"It's all in the reflexes."*
