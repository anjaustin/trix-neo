# HSOS Native Inference (OP_COMPUTE) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement `hsos_exec_infer()` — distributed Hamming distance inference over HSOS worker nodes — so that any inference result carries an HSOS trace reference proving exactly how it was computed.

**Architecture:** A new bridge layer (`hsos_infer.c`) connects the existing runtime inference engine (`runtime.c`) to the HSOS messaging kernel (`hsos.c`). The master node shards the chip's signatures across workers, broadcasts the 64-byte input to each worker via fragmented `OP_COMPUTE` messages sent from master→worker through the bus, collects per-worker best-match results via `OP_COMPUTE_OK`, and aggregates to a global minimum. Worker nodes execute local popcount via a callback (`compute_fn`) injected by the bridge, keeping `hsos.c` free of runtime dependencies. A new `hsos_step_workers()` API lets the bridge run workers + bus delivery without master's dispatch draining `OP_COMPUTE_OK` replies before they can be read.

**Tech Stack:** C11, HSOS messaging kernel (`hsos.h`/`hsos.c`), TriX runtime (`runtime.h`/`runtime.c`), CMake `add_trix_test()`, clang with ASAN.

---

## Background: What Already Exists

Before touching anything, read these files:

- `zor/include/hsos.h` — full HSOS API; note `hsos_node_t`, `hsos_system_t`, `hsos_send_fragmented()`, `HSOS_FRAG_BUF_SIZE=64`, `OP_COMPUTE=0x40`, `OP_COMPUTE_OK=0x41`, `HSOS_PAYLOAD_MAX=10`
- `zor/src/hsos.c` — `node_step()` dispatch is where `handle_compute()` gets wired; `hsos_send_fragmented()` already implemented; `hsos_init()` already allocates dynamic record buffer
- `zor/include/trixc/runtime.h` — `trix_chip_t`, `trix_result_t`, `trix_infer()`, `trix_signature()`, `trix_label()`, `trix_threshold()`, `trix_info()`
- `zor/test/test_runtime.c` — how tests are structured: `main()`, `printf`-based pass/fail, write `.trix` to `/tmp`, load, infer
- `CMakeLists.txt` lines 90–98 — `TRIX_RUNTIME_SOURCES` is the exact list fed to `add_library(trix_runtime_static ...)` and `add_library(trix_runtime_shared ...)`; `TriX::Runtime` is an alias for `trix_runtime_static`. **`hsos.c` is not in this list yet.**

**Key constraints:**
- `hsos.c` must not `#include` anything from `trixc/` — the bridge owns the dependency crossing
- `OP_COMPUTE_OK` replies land in master's inbox via `bus_deliver()`. `hsos_step()` calls `node_step(&sys->master)` which drains that inbox before the bridge can read it. The bridge must use `hsos_step_workers()` (new, workers + bus only) during collection so master's inbox is never pre-consumed.
- The bridge sends inputs from master→worker through the bus: `hsos_send_fragmented(&sys->master, OP_COMPUTE, worker_id, input, 64)` — not from the worker struct directly.

---

## File Map

| Action | Path | Responsibility |
|--------|------|----------------|
| Modify | `zor/include/hsos.h` | Add `hsos_compute_result_t` typedef + `compute_fn`/`compute_ctx` to `hsos_node_t`; add `hsos_step_workers()` declaration |
| Modify | `zor/src/hsos.c` | Add `handle_compute()`, wire `OP_COMPUTE`/`OP_COMPUTE_OK` in `node_step()`, implement `hsos_step_workers()` |
| Modify | `zor/include/trixc/runtime.h` | Add `trace_tick_start`/`trace_tick_end` to `trix_result_t` |
| Create | `zor/include/trixc/hsos_infer.h` | Public API: `hsos_exec_infer()` |
| Create | `zor/src/hsos_infer.c` | Bridge: shard setup, fragmented broadcast (master→worker), aggregation via `hsos_step_workers()`, worker callback |
| Modify | `CMakeLists.txt` | Add `zor/src/hsos.c` and `zor/src/hsos_infer.c` to `TRIX_RUNTIME_SOURCES`; add `test_hsos_infer` |
| Create | `zor/test/test_hsos_infer.c` | All tests for this feature |

---

## Task 1: Kernel Changes — Callback Infrastructure + `hsos_step_workers()`

**Files:**
- Modify: `zor/include/hsos.h`
- Modify: `zor/src/hsos.c`

### What and Why

Three things happen here:

1. `hsos_compute_result_t` — the 10-byte packed struct that fits in `payload[]` exactly, carrying match/distance/threshold/label-prefix from worker to master.
2. `compute_fn` / `compute_ctx` on `hsos_node_t` — the bridge injects a callback + shard context before dispatching; `handle_compute()` calls it without knowing about `trix_chip_t`.
3. `hsos_step_workers()` — steps workers + bus delivery only, skipping `node_step(&sys->master)`. The bridge uses this during reply collection so master's inbox is not drained before the bridge reads it.

- [ ] **Step 1.1: Add `hsos_compute_result_t` typedef to `hsos.h`**

After `#include <stdbool.h>`, add:

```c
/* Result struct carried in OP_COMPUTE_OK payload.
 * Packed to guarantee sizeof == HSOS_PAYLOAD_MAX (10). */
typedef struct __attribute__((packed)) {
    int16_t match;      /* Signature index, -1 = no match */
    int16_t distance;   /* Hamming distance */
    int16_t threshold;  /* Threshold used */
    char    label[4];   /* First 3 chars of label + null (see strncpy note) */
} hsos_compute_result_t;

_Static_assert(sizeof(hsos_compute_result_t) == 10,
               "hsos_compute_result_t must fit in HSOS_PAYLOAD_MAX");

typedef void (*hsos_compute_fn_t)(const uint8_t *input, uint8_t input_len,
                                   void *ctx,
                                   hsos_compute_result_t *out);
```

- [ ] **Step 1.2: Add `compute_fn` and `compute_ctx` to `hsos_node_t` in `hsos.h`**

After `bool frag_active;`, add:

```c
    /* Compute callback — set by bridge, called by handle_compute() */
    hsos_compute_fn_t compute_fn;   /* NULL = no compute capability */
    void             *compute_ctx;  /* Opaque shard context for compute_fn */
```

- [ ] **Step 1.3: Declare `hsos_step_workers()` in `hsos.h`**

In the System Control API section, after `hsos_step()`:

```c
/* Execute one tick for workers + bus only — master inbox is NOT drained.
 * Use during OP_COMPUTE_OK collection so replies remain in master inbox. */
bool hsos_step_workers(hsos_system_t *sys);
```

- [ ] **Step 1.4: Implement `hsos_step_workers()` in `hsos.c`**

After `hsos_step()`:

```c
bool hsos_step_workers(hsos_system_t *sys) {
    bool work_done = false;

    sys->tick++;

    for (int i = 0; i < 8; i++) {
        work_done |= node_step(&sys->workers[i]);
    }

    bus_deliver(sys);
    fabric_update_directory(sys);
    sys->fabric.tick = sys->tick;

    return work_done;
}
```

- [ ] **Step 1.5: Add `handle_compute()` to `hsos.c`**

After `handle_cswap_ok()`, before `handle_domain_ops()`:

```c
static void handle_compute(hsos_node_t *node, const hsos_msg_t *msg) {
    hsos_msg_t reply;
    msg_clear(&reply);
    reply.type = OP_COMPUTE_OK;
    reply.dst  = msg->src;
    reply.seq  = msg->seq;
    reply.len  = sizeof(hsos_compute_result_t);

    hsos_compute_result_t result;
    memset(&result, 0, sizeof(result));
    result.match    = -1;
    result.distance = 512;

    if (node->compute_fn && node->frag_len == HSOS_FRAG_BUF_SIZE) {
        node->compute_fn(node->frag_buf, node->frag_len,
                         node->compute_ctx, &result);
    } else {
        node->errors++;
    }

    memcpy(reply.payload, &result, sizeof(result));
    hsos_send(node, &reply);
}
```

- [ ] **Step 1.6: Wire `OP_COMPUTE` and `OP_COMPUTE_OK` into `node_step()` dispatch**

In the switch statement, after `case OP_CSWAP_OK`:

```c
        case OP_COMPUTE:    handle_compute(node, &msg); break;
        case OP_COMPUTE_OK: /* Collected by bridge via hsos_recv(master) */ break;
```

- [ ] **Step 1.7: Compile `hsos.c` in isolation to verify**

```bash
clang -std=c11 -Wall -Wextra -I zor/include -c zor/src/hsos.c -o /tmp/hsos.o
```

Expected: zero errors. Pre-existing `trace_add unused` warning is acceptable.

- [ ] **Step 1.8: Commit**

```bash
git add zor/include/hsos.h zor/src/hsos.c
git commit -m "hsos: add compute_fn callback, handle_compute(), hsos_step_workers()"
```

---

## Task 2: Extend `trix_result_t` + Create Stub Bridge Header

**Files:**
- Modify: `zor/include/trixc/runtime.h`
- Create: `zor/include/trixc/hsos_infer.h` (stub — real implementation in Task 4)

### Why the stub header first

The test file (`test_hsos_infer.c`) includes `hsos_infer.h`. Without the header the tests won't compile at all. Create a minimal stub now so tests compile but fail to link — that's the correct TDD failure mode.

- [ ] **Step 2.1: Add trace tick fields to `trix_result_t`**

In `zor/include/trixc/runtime.h`, change:

```c
typedef struct {
    int match;
    int distance;
    int threshold;
    const char* label;
} trix_result_t;
```

to:

```c
typedef struct {
    int match;              /* Signature index (-1 = no match) */
    int distance;           /* Hamming distance to best match */
    int threshold;          /* Threshold used */
    const char* label;      /* Human-readable label (NULL if no match) */
    uint32_t trace_tick_start; /* HSOS tick when inference began (0 = N/A) */
    uint32_t trace_tick_end;   /* HSOS tick when result was selected (0 = N/A) */
} trix_result_t;
```

- [ ] **Step 2.2: Verify `trix_infer()` zero-initializes the new fields**

Open `zor/src/runtime.c` and find the `trix_result_t` return inside `trix_infer()`. Confirm it initializes all fields. If it uses a partial initializer like `{best_index, best_distance, best_threshold, best_label}`, add explicit zeros:

```c
trix_result_t result = {best_index, best_distance, best_threshold,
                         best_label, 0, 0};
```

- [ ] **Step 2.3: Create stub `hsos_infer.h`**

```c
/*
 * hsos_infer.h — Bridge between HSOS messaging kernel and TriX inference
 */

#ifndef TRIXC_HSOS_INFER_H
#define TRIXC_HSOS_INFER_H

#include "runtime.h"
#include "../hsos.h"

#ifdef __cplusplus
extern "C" {
#endif

trix_result_t hsos_exec_infer(hsos_system_t *sys,
                               trix_chip_t   *chip,
                               const uint8_t  input[64]);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_HSOS_INFER_H */
```

- [ ] **Step 2.4: Commit**

```bash
git add zor/include/trixc/runtime.h zor/include/trixc/hsos_infer.h
git commit -m "runtime: add trace ticks to trix_result_t; stub hsos_infer.h"
```

---

## Task 3: Write Failing Tests + Update CMakeLists

**Files:**
- Create: `zor/test/test_hsos_infer.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 3.1: Create `zor/test/test_hsos_infer.c`**

```c
/*
 * test_hsos_infer.c — Tests for HSOS native inference (OP_COMPUTE)
 */

#include "../include/hsos.h"
#include "../include/trixc/hsos_infer.h"
#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* ── helpers ──────────────────────────────────────────────────────────────── */

static trix_chip_t *make_chip(int n_signatures) {
    FILE *f = fopen("/tmp/test_hsos_infer.trix", "w");
    fprintf(f, "softchip:\n");
    fprintf(f, "  name: test_hsos_infer\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n");
    for (int i = 0; i < n_signatures; i++) {
        fprintf(f, "  sig%d:\n", i);
        fprintf(f, "    pattern: ");
        /* Signature i: byte value i repeated 64 times */
        for (int b = 0; b < 64; b++) fprintf(f, "%02x", (uint8_t)i);
        fprintf(f, "\n");
        fprintf(f, "    threshold: 32\n");
    }
    fprintf(f, "inference:\n  mode: nearest\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load("/tmp/test_hsos_infer.trix", &err);
    assert(chip && "make_chip: trix_load failed");
    return chip;
}

static hsos_system_t *make_system(void) {
    hsos_system_t *sys = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys);
    hsos_boot(sys);
    return sys;
}

static void teardown(hsos_system_t *sys, trix_chip_t *chip) {
    hsos_system_free(sys);
    free(sys);
    trix_chip_free(chip);
}

/* ── test 1: basic match ──────────────────────────────────────────────────── */

static int test_basic_match(void) {
    printf("[TEST] basic_match\n");

    trix_chip_t *chip = make_chip(1);
    hsos_system_t *sys = make_system();

    /* Input identical to sig0 (byte 0x00 repeated) → distance 0 */
    uint8_t input[64];
    memset(input, 0x00, 64);

    trix_result_t r = hsos_exec_infer(sys, chip, input);

    assert(r.match == 0 && "expected sig0 to match");
    assert(r.distance == 0 && "expected zero distance for identical input");
    printf("  ✓ match=%d distance=%d\n", r.match, r.distance);

    teardown(sys, chip);
    return 0;
}

/* ── test 2: no match ─────────────────────────────────────────────────────── */

static int test_no_match(void) {
    printf("[TEST] no_match\n");

    trix_chip_t *chip = make_chip(1);
    hsos_system_t *sys = make_system();

    /* sig0 is all 0x00. Input all 0xFF → Hamming distance 512 >> threshold 32 */
    uint8_t input[64];
    memset(input, 0xFF, 64);

    trix_result_t r = hsos_exec_infer(sys, chip, input);

    assert(r.match == -1 && "expected no match for max-distance input");
    printf("  ✓ match=%d (no match as expected)\n", r.match);

    teardown(sys, chip);
    return 0;
}

/* ── test 3: exact agreement with trix_infer ─────────────────────────────── */

static int test_exact_agreement(void) {
    printf("[TEST] exact_agreement (8 sigs, 10 inputs)\n");

    trix_chip_t *chip = make_chip(8);
    hsos_system_t *sys = make_system();

    uint8_t inputs[10][64];
    for (int i = 0; i < 10; i++) {
        memset(inputs[i], i * 25, 64);
    }

    int mismatches = 0;
    for (int i = 0; i < 10; i++) {
        trix_result_t ref  = trix_infer(chip, inputs[i]);
        trix_result_t hsos = hsos_exec_infer(sys, chip, inputs[i]);

        if (ref.match != hsos.match || ref.distance != hsos.distance) {
            printf("  ✗ input[%d]: trix=(%d,%d) hsos=(%d,%d)\n",
                   i, ref.match, ref.distance, hsos.match, hsos.distance);
            mismatches++;
        }
    }

    assert(mismatches == 0 && "hsos_exec_infer must agree with trix_infer");
    printf("  ✓ all 10 inputs agree\n");

    teardown(sys, chip);
    return 0;
}

/* ── test 4: replay produces identical result ─────────────────────────────── */

static int test_replay(void) {
    printf("[TEST] replay_faithfulness\n");

    trix_chip_t *chip = make_chip(4);
    hsos_system_t *sys = make_system();

    uint8_t input[64];
    memset(input, 0x02, 64);

    /* Original run with recording */
    hsos_start_recording(sys);
    trix_result_t r1 = hsos_exec_infer(sys, chip, input);
    hsos_stop_recording(sys);

    /* Replay — resets nodes and re-injects recorded messages through bus */
    hsos_replay(sys);

    /* Run inference again on the replayed system — must match original */
    trix_result_t r2 = hsos_exec_infer(sys, chip, input);

    assert(r1.match == r2.match &&
           r1.distance == r2.distance &&
           "inference on replayed system must match original result");
    printf("  ✓ original=(%d,%d) replayed=(%d,%d)\n",
           r1.match, r1.distance, r2.match, r2.distance);

    teardown(sys, chip);
    return 0;
}

/* ── test 5: trace tick range is populated ───────────────────────────────── */

static int test_trace_ticks(void) {
    printf("[TEST] trace_tick_range\n");

    trix_chip_t *chip = make_chip(2);
    hsos_system_t *sys = make_system();

    uint8_t input[64] = {0};
    trix_result_t r = hsos_exec_infer(sys, chip, input);

    assert(r.trace_tick_end >= r.trace_tick_start &&
           "tick_end must be >= tick_start");
    assert(r.trace_tick_end > 0 && "ticks must have advanced");
    printf("  ✓ trace ticks [%u..%u]\n", r.trace_tick_start, r.trace_tick_end);

    teardown(sys, chip);
    return 0;
}

/* ── test 6: trix_infer trace fields are zero ────────────────────────────── */

static int test_trix_infer_zero_ticks(void) {
    printf("[TEST] trix_infer_zero_trace_ticks\n");

    trix_chip_t *chip = make_chip(1);
    uint8_t input[64] = {0};

    trix_result_t r = trix_infer(chip, input);

    assert(r.trace_tick_start == 0 && r.trace_tick_end == 0 &&
           "trix_infer() must leave trace ticks zero (N/A)");
    printf("  ✓ trace_tick_start=%u trace_tick_end=%u\n",
           r.trace_tick_start, r.trace_tick_end);

    trix_chip_free(chip);
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    printf("══════════════════════════════════════\n");
    printf("  HSOS Native Inference Tests\n");
    printf("══════════════════════════════════════\n\n");

    int failures = 0;
    failures += test_basic_match();
    failures += test_no_match();
    failures += test_exact_agreement();
    failures += test_replay();
    failures += test_trace_ticks();
    failures += test_trix_infer_zero_ticks();

    printf("\n%s — %d failure(s)\n",
           failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
```

- [ ] **Step 3.2: Add `hsos.c`, `hsos_infer.c`, and test to `CMakeLists.txt`**

In `CMakeLists.txt`, change `TRIX_RUNTIME_SOURCES` from:

```cmake
set(TRIX_RUNTIME_SOURCES
    zor/src/errors.c
    zor/src/logging.c
    zor/src/validation.c
    zor/src/thread.c
    zor/src/version.c
    zor/src/runtime.c
    zor/src/metrics.c
)
```

to:

```cmake
set(TRIX_RUNTIME_SOURCES
    zor/src/errors.c
    zor/src/logging.c
    zor/src/validation.c
    zor/src/thread.c
    zor/src/version.c
    zor/src/runtime.c
    zor/src/metrics.c
    zor/src/hsos.c
    zor/src/hsos_infer.c
)
```

Then locate the block of `add_trix_test(...)` calls and add:

```cmake
add_trix_test(test_hsos_infer zor/test/test_hsos_infer.c)
```

Also add `test_hsos_infer` to the `check` target's `DEPENDS` list.

- [ ] **Step 3.3: Verify tests fail to link (correct TDD state)**

```bash
cmake -B build && cmake --build build 2>&1 | grep -E "test_hsos_infer|hsos_exec_infer"
```

Expected: linker error — `hsos_exec_infer` undefined. The test binary fails to link because the function doesn't exist yet. This is the correct state.

- [ ] **Step 3.4: Commit**

```bash
git add zor/test/test_hsos_infer.c CMakeLists.txt
git commit -m "test: failing tests for hsos_exec_infer (TDD); hsos.c added to runtime"
```

---

## Task 4: Implement `hsos_infer.c`

**Files:**
- Create: `zor/src/hsos_infer.c`

### Protocol

```
hsos_exec_infer(sys, chip, input):
  1. Shard: assign ceil(N/active_workers) signatures per online worker
  2. Inject: set worker->compute_fn = hsos_infer_worker_compute
             set worker->compute_ctx = &shard_ctx[i]
  3. Send:   hsos_send_fragmented(&sys->master, OP_COMPUTE, worker_id, input, 64)
             for each assigned worker — routes master→worker through bus
  4. Collect: loop using hsos_step_workers() (NOT hsos_step) so master inbox
              is not drained; read OP_COMPUTE_OK via hsos_recv(&sys->master)
  5. Aggregate: pick minimum distance reply
  6. Return: trix_result_t with trace ticks
```

**Why `hsos_step_workers()` not `hsos_step()`:** `hsos_step()` calls `node_step(&sys->master)`, which drains master's inbox through the dispatch switch. `OP_COMPUTE_OK` is a no-op in the switch — it's consumed and discarded. `hsos_step_workers()` skips master's `node_step`, leaving replies in master's inbox for the bridge to read.

**Why `hsos_send_fragmented(&sys->master, ...)` not `hsos_send_fragmented(w, ...)`:** The bridge runs on the master side. Fragments must enter the bus from master's outbox and be routed to the worker's inbox by `bus_deliver()`. Calling `hsos_send_fragmented(w, ...)` would enqueue into the worker's own outbox — the wrong direction.

- [ ] **Step 4.1: Create `zor/src/hsos_infer.c`**

```c
/*
 * hsos_infer.c — Bridge: HSOS distributed inference
 *
 * Connects hsos_system_t (messaging kernel) with trix_chip_t (inference),
 * making inference a traceable, replayable HSOS computation.
 */

#include <string.h>
#include <stdlib.h>
#include "../include/hsos.h"
#include "../include/trixc/hsos_infer.h"
#include "../include/trixc/runtime.h"

/* ── shard context ─────────────────────────────────────────────────────────── */

typedef struct {
    trix_chip_t *chip;
    int shard_start;
    int shard_count;
} hsos_shard_ctx_t;

/* ── worker compute callback ──────────────────────────────────────────────── */

static void hsos_infer_worker_compute(const uint8_t *input, uint8_t input_len,
                                      void *ctx,
                                      hsos_compute_result_t *out) {
    (void)input_len;
    hsos_shard_ctx_t *shard = (hsos_shard_ctx_t *)ctx;

    out->match     = -1;
    out->distance  = 512;
    out->threshold = 0;
    memset(out->label, 0, sizeof(out->label));

    for (int i = shard->shard_start;
         i < shard->shard_start + shard->shard_count; i++) {

        const uint8_t *sig = trix_signature(shard->chip, i);
        if (!sig) continue;

        int threshold = trix_threshold(shard->chip, i);

        /* Hamming distance via XOR + bit count */
        int dist = 0;
        for (int b = 0; b < 64; b++) {
            uint8_t x = input[b] ^ sig[b];
            while (x) { dist++; x &= x - 1; }  /* Kernighan method */
        }

        if (dist <= threshold && dist < (int)out->distance) {
            out->match     = (int16_t)i;
            out->distance  = (int16_t)dist;
            out->threshold = (int16_t)threshold;

            const char *lbl = trix_label(shard->chip, i);
            if (lbl) {
                /* Copy up to 3 bytes; byte 3 stays 0 from memset above */
                strncpy(out->label, lbl, sizeof(out->label) - 1);
            }
        }
    }
}

/* ── aggregation loop ─────────────────────────────────────────────────────── */

static trix_result_t aggregate_replies(hsos_system_t *sys,
                                        trix_chip_t   *chip,
                                        int            active_workers,
                                        uint32_t       tick_start) {
    trix_result_t best = {-1, 512, 0, NULL, tick_start, 0};
    int replies  = 0;
    int timeout  = 0;

    /* Use hsos_step_workers(), NOT hsos_step() — see architecture note above.
     * hsos_step() would call node_step(master) which consumes OP_COMPUTE_OK
     * replies from master's inbox before we can read them here. */
    while (replies < active_workers && timeout < 500) {
        hsos_step_workers(sys);
        timeout++;

        hsos_msg_t msg;
        while (hsos_recv(&sys->master, &msg)) {
            if (msg.type != OP_COMPUTE_OK) continue;

            hsos_compute_result_t r;
            memcpy(&r, msg.payload, sizeof(r));

            if (r.match >= 0 && (int)r.distance < best.distance) {
                best.match     = r.match;
                best.distance  = r.distance;
                best.threshold = r.threshold;
                best.label     = trix_label(chip, r.match);
            }
            replies++;
        }
    }

    best.trace_tick_end = sys->tick;
    return best;
}

/* ── public API ───────────────────────────────────────────────────────────── */

trix_result_t hsos_exec_infer(hsos_system_t *sys,
                               trix_chip_t   *chip,
                               const uint8_t  input[64]) {
    trix_result_t no_result = {-1, 512, 0, NULL, 0, 0};

    if (!sys || !chip || !input) return no_result;

    const trix_chip_info_t *info = trix_info(chip);
    if (!info || info->num_signatures == 0) return no_result;

    int n = info->num_signatures;
    uint32_t tick_start = sys->tick;

    /* ── 1. Count online workers, cap to signature count ─────────────────── */

    int active = 0;
    for (int i = 0; i < 8; i++) {
        if (sys->workers[i].state != NODE_OFFLINE &&
            sys->workers[i].state != NODE_HALTED) active++;
    }
    if (active == 0) return no_result;
    if (active > n) active = n;  /* No empty shards */

    /* ── 2. Allocate shard contexts ──────────────────────────────────────── */

    hsos_shard_ctx_t *ctxs = calloc(active, sizeof(hsos_shard_ctx_t));
    if (!ctxs) return no_result;

    int per_worker = n / active;
    int remainder  = n % active;
    int sig_idx    = 0;
    int assigned   = 0;

    for (int i = 0; i < 8 && assigned < active; i++) {
        hsos_node_t *w = &sys->workers[i];
        if (w->state == NODE_OFFLINE || w->state == NODE_HALTED) continue;

        int count = per_worker + (assigned < remainder ? 1 : 0);

        ctxs[assigned].chip        = chip;
        ctxs[assigned].shard_start = sig_idx;
        ctxs[assigned].shard_count = count;

        w->compute_fn  = hsos_infer_worker_compute;
        w->compute_ctx = &ctxs[assigned];

        sig_idx  += count;
        assigned++;
    }

    /* ── 3. Send input to each assigned worker (master → worker via bus) ─── */

    for (int i = 0; i < 8; i++) {
        if (!sys->workers[i].compute_fn) continue;
        uint8_t dst = sys->workers[i].node_id;
        /* Send from master's outbox; bus_deliver() routes to worker's inbox */
        hsos_send_fragmented(&sys->master, OP_COMPUTE, dst, input, 64);
    }

    /* Flush master's outbox to worker inboxes before collection loop */
    hsos_step_workers(sys);

    /* ── 4. Collect replies ───────────────────────────────────────────────── */

    trix_result_t result = aggregate_replies(sys, chip, active, tick_start);

    /* ── 5. Clean up ─────────────────────────────────────────────────────── */

    for (int i = 0; i < 8; i++) {
        sys->workers[i].compute_fn  = NULL;
        sys->workers[i].compute_ctx = NULL;
    }
    free(ctxs);

    return result;
}
```

- [ ] **Step 4.2: Build and verify tests link**

```bash
cmake --build build 2>&1 | grep -E "error:|test_hsos_infer"
```

Expected: `test_hsos_infer` builds without errors.

- [ ] **Step 4.3: Run tests**

```bash
cd build && ctest -R test_hsos_infer -V
```

Expected: all 6 tests pass.

**If tests fail**, diagnose by checking:
- `test_basic_match` / `test_exact_agreement` fail: run with `printf` in `handle_compute()` to verify it's being called; check `node->frag_len == 64` at dispatch time
- `test_replay` fails: check that `hsos_replay()` correctly routes through `hsos_step_workers()` during collection — note replay calls `hsos_run()` which uses `hsos_step()`, which is fine since the bridge is not reading during replay
- `test_trace_ticks` fails: verify `sys->tick` is advancing inside `hsos_step_workers()`

- [ ] **Step 4.4: Run full test suite for regressions**

```bash
cd build && ctest --output-on-failure
```

Expected: all previously passing tests still pass.

- [ ] **Step 4.5: Commit**

```bash
git add zor/src/hsos_infer.c
git commit -m "feat: hsos_exec_infer() — distributed inference with OP_COMPUTE

Workers receive 64-byte input via fragmented OP_COMPUTE from master.
Each runs local Hamming distance matching via injected compute_fn.
Bridge uses hsos_step_workers() during collection to avoid master's
node_step() draining OP_COMPUTE_OK replies before they can be read.
Master aggregates replies, picks global minimum, returns trix_result_t
with trace_tick_start/end for HSOS provenance.

Result is bit-identical to trix_infer() for all inputs."
```

---

## Task 5: ASAN Verification + Final Push

- [ ] **Step 5.1: Build with ASAN**

```bash
cmake -B build -DTRIX_ENABLE_ASAN=ON && cmake --build build
cd build && ctest -R test_hsos_infer -V
```

Expected: zero ASAN errors. Watch for:
- Heap-use-after-free on `best.label`: safe — `label` points into chip internals, chip outlives the result in all tests
- Stack buffer overflow in `memcpy(reply.payload, &result, sizeof(result))`: prevented by `_Static_assert` on `hsos_compute_result_t` size

- [ ] **Step 5.2: Final commit and push**

```bash
cd build && ctest --output-on-failure
git push
```

---

## Success Criteria

- [ ] `hsos_exec_infer(sys, chip, input)` returns a `trix_result_t` bit-identical to `trix_infer(chip, input)` for all 10 test inputs across 8 signatures
- [ ] All 6 tests in `test_hsos_infer.c` pass
- [ ] All previously passing tests pass
- [ ] ASAN reports zero errors
- [ ] `result.trace_tick_start` and `result.trace_tick_end` are nonzero after `hsos_exec_infer()`
- [ ] `trix_infer()` trace fields are zero (verified by test 6)
- [ ] `sizeof(hsos_compute_result_t) == 10` (enforced by `_Static_assert`)

---

## Known Risks

| Risk | Mitigation |
|------|-----------|
| Fragment reassembly: handler fires with `frag_len` ≠ 64 | Guard in `handle_compute()`: check `frag_len == HSOS_FRAG_BUF_SIZE`, increment errors and reply no-match if wrong |
| Collection timeout: workers don't reply in 500 ticks | Add `printf` debug in `handle_compute()` to verify it's reached; check that `hsos_send_fragmented` from master is routing to workers |
| Unbalanced shards for N not divisible by active workers | Verified by `remainder` logic: first `remainder` workers get `per_worker + 1` signatures; rest get `per_worker` |
| `best.label` dangling after `ctxs` freed | `best.label` is set via `trix_label(chip, r.match)` which points into chip internals — safe as long as chip outlives the result |

---

*See also: `docs/HSOS_STEP_CHANGE.md`, `journal/hsos_stepchange_synth.md`*
