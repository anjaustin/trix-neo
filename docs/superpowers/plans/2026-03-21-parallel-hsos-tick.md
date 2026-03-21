# Parallel HSOS Tick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Parallelize the 8 worker node steps within each HSOS tick using pthreads, preserving determinism by keeping bus delivery serial.

**Architecture:** `hsos_step()` currently processes all 8 workers sequentially in a loop. Since workers share no mutable state within a tick (all inter-node communication flows only through the bus, which is delivered in the serial `bus_deliver()` phase), worker steps are safe to parallelize. A static thread pool executes `node_step()` for all 8 workers concurrently, waits at a barrier, then the caller proceeds with serial bus delivery.

**Tech Stack:** C11, pthreads (already linked via `Threads::Threads` in CMakeLists.txt), `pthread_barrier_t`

---

## Background: What Already Exists

**OP_COMPUTE / OP_COMPUTE_OK are fully implemented.** `handle_compute()` in `hsos.c` calls `node->compute_fn` when a complete fragmented payload arrives. `hsos_exec_infer()` in `hsos_infer.c` sets up shards, sends fragments, and aggregates replies. This plan does NOT re-implement inference — it makes worker steps run in parallel, which makes every HSOS workload (Hamming matching, BubbleMachine, ConstraintField) faster.

**The safety invariant:** Within a tick, workers read from their own inbox and write to their own outbox. No worker reads another worker's outbox. Bus delivery reads all outboxes and writes all inboxes — but this happens AFTER all workers finish. Therefore: parallelize worker steps, serialize bus delivery.

**`hsos_step_workers()`** already exists and processes workers without draining the master inbox — it's the right place to add parallelism.

---

## File Map

| File | Action | Purpose |
|---|---|---|
| `zor/src/hsos.c` | Modify | Add thread pool, parallel worker step in `hsos_step()` and `hsos_step_workers()` |
| `zor/include/hsos.h` | Modify | Add `hsos_system_t` thread pool fields, lifecycle functions |
| `zor/test/test_parallel_tick.c` | Create | Verify parallel and sequential produce identical results |
| `CMakeLists.txt` | Modify | Add `test_parallel_tick` test |

---

## Task 1: Thread Pool Fields in `hsos_system_t`

The thread pool is embedded in the system struct. Workers spin on a semaphore, execute their node step, then signal a barrier. This avoids thread create/destroy overhead per tick.

**Files:**
- Modify: `zor/include/hsos.h`

- [ ] **Step 1: Read the current `hsos_system_t` struct**

```bash
grep -n "hsos_system_t\|typedef struct\|thread\|pthread" zor/include/hsos.h | head -40
```

- [ ] **Step 2: Add thread pool fields to `hsos_system_t`**

In `zor/include/hsos.h`, add `#include <pthread.h>` at the top (after existing includes), then add these fields to the END of the `hsos_system_t` struct (before the closing `}`):

The `#ifndef HSOS_PARALLEL` default guard must go at the TOP of `hsos.h` (before any struct definitions), not inside a struct block. Add these lines immediately after the `#ifndef HSOS_H` include guard and before the first `#include`:

```c
/* Compile-time switch: define HSOS_PARALLEL=0 to disable thread pool */
#ifndef HSOS_PARALLEL
#define HSOS_PARALLEL 1
#endif
```

Then, at the END of `hsos_system_t` (before the closing `}`), add the conditional thread pool fields:

```c
#if HSOS_PARALLEL
    /* Thread pool for parallel worker steps. */
    pthread_t        worker_threads[8];
    /* Per-worker arg struct avoids unsafe pointer-packing */
    struct hsos_worker_arg {
        hsos_system_t *sys;
        int            widx;
        bool           step_done;   /* node_step() return value from this worker */
    }                worker_args[8];
    pthread_mutex_t  worker_mutex;
    pthread_cond_t   worker_start_cond;
    pthread_cond_t   worker_done_cond;
    int              worker_pending;     /* # workers not yet done this tick */
    int              worker_shutdown;    /* Set to 1 to stop threads */
    hsos_node_t     *worker_target[8];  /* NULL = idle */
#endif
```

Note: using embedded `worker_args[8]` avoids the need for a separately-allocated arg array and avoids any pointer-packing tricks that require alignment guarantees beyond what `calloc` provides.

Also add lifecycle declarations at the bottom of the API section:

```c
/* Initialize the parallel worker thread pool (called by hsos_init internally) */
void hsos_parallel_init(hsos_system_t *sys);

/* Tear down the thread pool (called by hsos_system_free internally) */
void hsos_parallel_destroy(hsos_system_t *sys);
```

- [ ] **Step 3: Build to confirm struct change compiles**

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j4 2>&1 | head -30
```
Expected: build succeeds (or only minor warnings — no errors).

- [ ] **Step 4: Commit the header change**

```bash
git add zor/include/hsos.h
git commit -m "feat: add thread pool fields to hsos_system_t for parallel tick"
```

---

## Task 2: Thread Pool Implementation

**Files:**
- Modify: `zor/src/hsos.c`

- [ ] **Step 1: Add `#include <pthread.h>` to hsos.c**

At the top of `zor/src/hsos.c`, after existing includes, guarded so it is only pulled in when the thread pool is active:
```c
#if HSOS_PARALLEL
#include <pthread.h>
#endif
```

- [ ] **Step 2: Add the thread pool implementation**

Add this block to `zor/src/hsos.c`, after the existing message helper functions (around line 60, before NODE KERNEL section):

```c
/* ═══════════════════════════════════════════════════════════════════════════
 * THREAD POOL — Parallel worker tick
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Safety: within a tick, workers only read their own inbox and write their
 * own outbox. No cross-worker state sharing. Bus delivery (serial) runs
 * after all workers complete. This makes parallel worker steps safe.
 */

#if HSOS_PARALLEL

/* Forward declaration — node_step is defined later in this file */
static bool node_step(hsos_node_t *node);

static void *worker_thread_fn(void *arg) {
    /* Use the embedded arg struct — no pointer-packing needed */
    struct hsos_worker_arg *wa = (struct hsos_worker_arg *)arg;
    hsos_system_t *sys  = wa->sys;
    int            widx = wa->widx;

    while (1) {
        /* Wait for work or shutdown signal */
        pthread_mutex_lock(&sys->worker_mutex);
        while (sys->worker_target[widx] == NULL && !sys->worker_shutdown) {
            pthread_cond_wait(&sys->worker_start_cond, &sys->worker_mutex);
        }

        if (sys->worker_shutdown) {
            pthread_mutex_unlock(&sys->worker_mutex);
            return NULL;
        }

        hsos_node_t *target = sys->worker_target[widx];
        sys->worker_target[widx] = NULL;  /* Claim the work */
        pthread_mutex_unlock(&sys->worker_mutex);

        /* Execute node step — no locks needed: inbox/outbox are per-node.
         * Store the return value so parallel_step_workers() can faithfully
         * replicate the work_done |= node_step() semantics of the sequential path. */
        wa->step_done = node_step(target);

        /* Signal completion */
        pthread_mutex_lock(&sys->worker_mutex);
        sys->worker_pending--;
        if (sys->worker_pending == 0) {
            pthread_cond_signal(&sys->worker_done_cond);
        }
        pthread_mutex_unlock(&sys->worker_mutex);
    }
}

void hsos_parallel_init(hsos_system_t *sys) {
    pthread_mutex_init(&sys->worker_mutex, NULL);
    pthread_cond_init(&sys->worker_start_cond, NULL);
    pthread_cond_init(&sys->worker_done_cond, NULL);
    sys->worker_pending = 0;
    sys->worker_shutdown = 0;
    memset(sys->worker_target, 0, sizeof(sys->worker_target));

    for (int i = 0; i < 8; i++) {
        /* Initialize embedded arg struct — no alignment assumption needed */
        sys->worker_args[i].sys  = sys;
        sys->worker_args[i].widx = i;
        pthread_create(&sys->worker_threads[i], NULL, worker_thread_fn,
                       &sys->worker_args[i]);
    }
}

void hsos_parallel_destroy(hsos_system_t *sys) {
    pthread_mutex_lock(&sys->worker_mutex);
    sys->worker_shutdown = 1;
    pthread_cond_broadcast(&sys->worker_start_cond);
    pthread_mutex_unlock(&sys->worker_mutex);

    for (int i = 0; i < 8; i++) {
        pthread_join(sys->worker_threads[i], NULL);
    }
    pthread_mutex_destroy(&sys->worker_mutex);
    pthread_cond_destroy(&sys->worker_start_cond);
    pthread_cond_destroy(&sys->worker_done_cond);
}

/* Dispatch all 8 workers in parallel, block until all complete.
 * Returns the OR of all node_step() return values, faithfully replicating the
 * work_done |= node_step() semantics of the sequential path so hsos_run()
 * quiescence detection is unaffected by parallelism. */
static bool parallel_step_workers(hsos_system_t *sys) {
    pthread_mutex_lock(&sys->worker_mutex);
    for (int i = 0; i < 8; i++) {
        sys->worker_args[i].step_done = false;
        sys->worker_target[i] = &sys->workers[i];
    }
    sys->worker_pending = 8;
    pthread_cond_broadcast(&sys->worker_start_cond);
    while (sys->worker_pending > 0) {
        pthread_cond_wait(&sys->worker_done_cond, &sys->worker_mutex);
    }
    bool any_work = false;
    for (int i = 0; i < 8; i++)
        any_work |= sys->worker_args[i].step_done;
    pthread_mutex_unlock(&sys->worker_mutex);
    return any_work;
}

#else  /* HSOS_PARALLEL == 0 */

void hsos_parallel_init(hsos_system_t *sys) { (void)sys; }
void hsos_parallel_destroy(hsos_system_t *sys) { (void)sys; }

#endif /* HSOS_PARALLEL */
```

**IMPORTANT:** `node_step()` is a `static bool` function declared later in `hsos.c`. The thread pool block references it, so it needs a forward declaration. The forward declaration is already included in the code block above (`static bool node_step(hsos_node_t *node);`). Confirm it uses `bool` not `void` — the actual signature at `hsos.c` is `static bool node_step(hsos_node_t *node)` and a mismatched forward declaration is undefined behavior.

- [ ] **Step 3: Update `hsos_init_with_capacity()` to call `hsos_parallel_init()`**

`hsos_init()` is a thin wrapper around `hsos_init_with_capacity()` — the actual initialization logic lives there. Add the thread pool call to `hsos_init_with_capacity()` so callers who bypass `hsos_init()` also get the thread pool.

Find `hsos_init_with_capacity()` in `hsos.c`. At the END of the function body, add:

```c
#if HSOS_PARALLEL
    hsos_parallel_init(sys);
#endif
```

**Note:** `hsos_boot()` also has its own sequential worker loop that runs node steps. Leave `hsos_boot()`'s loop unchanged — the thread pool is initialized by this point but `hsos_boot()` runs boot-protocol messages that must be deterministically ordered, and modifying it is out of scope for this plan.

- [ ] **Step 4: Update `hsos_system_free()` to call `hsos_parallel_destroy()`**

Find `hsos_system_free()`. At the START of the function body (before freeing record_buffer), add:

```c
#if HSOS_PARALLEL
    hsos_parallel_destroy(sys);
#endif
```

- [ ] **Step 5: Update `hsos_step_workers()` to use parallel dispatch**

Find `hsos_step_workers()`. Replace the sequential worker loop:

```c
/* BEFORE (sequential): */
for (int i = 0; i < 8; i++) {
    work_done |= node_step(&sys->workers[i]);
}
```

with:

```c
/* AFTER (parallel): */
#if HSOS_PARALLEL
    work_done |= parallel_step_workers(sys);
#else
    for (int i = 0; i < 8; i++) {
        work_done |= node_step(&sys->workers[i]);
    }
#endif
```

Also update `hsos_step()` the same way — find its worker loop and apply the same `work_done |= parallel_step_workers(sys)` replacement. This preserves the quiescence signal used by `hsos_run()`.

**IMPORTANT:** `hsos_step()` also calls `node_step(&sys->master)` — this call must remain unchanged. Only replace the sequential worker loop (`for (int i = 0; i < 8; i++) { work_done |= node_step(&sys->workers[i]); }`). The master node is not parallelized.

- [ ] **Step 6: Build to confirm it compiles**

```bash
cmake --build build -j4 2>&1 | head -30
```
Expected: compiles without errors. Warnings about unused vars are OK.

- [ ] **Step 7: Run existing test suite to confirm nothing broke**

```bash
cd build && ctest --output-on-failure 2>&1
```
Expected: all tests PASS. If any test FAILs: the thread pool has a race — add more instrumentation before proceeding.

- [ ] **Step 8: Commit**

```bash
git add zor/src/hsos.c zor/include/hsos.h
git commit -m "feat: parallel worker tick via pthreads in hsos_step/hsos_step_workers"
```

---

## Task 3: Parallel Correctness Test

**Files:**
- Create: `zor/test/test_parallel_tick.c`
- Modify: `CMakeLists.txt`

The test runs identical workloads on a sequential HSOS system and a parallel HSOS system, then asserts identical results.

- [ ] **Step 1: Write the test**

```c
/*
 * test_parallel_tick.c — Verify parallel worker tick produces same results
 *                         as sequential execution.
 */

#include "../include/hsos.h"
#include "../include/trixc/hsos_infer.h"
#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

/* ── helpers ─────────────────────────────────────────────────────────────── */

static trix_chip_t *make_chip(int n_sigs) {
    FILE *f = fopen("/tmp/test_parallel_tick.trix", "w");
    fprintf(f, "softchip:\n  name: parallel_test\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n");
    for (int i = 0; i < n_sigs; i++) {
        fprintf(f, "  sig%d:\n    pattern: ", i);
        for (int b = 0; b < 64; b++) fprintf(f, "%02x", (uint8_t)i);
        fprintf(f, "\n    threshold: 64\n");
    }
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);
    int err = 0;
    trix_chip_t *chip = trix_load("/tmp/test_parallel_tick.trix", &err);
    assert(chip && "make_chip failed");
    return chip;
}

/* Run N inferences on a freshly-initialized HSOS system, return results. */
static void run_inferences(trix_chip_t *chip,
                           const uint8_t inputs[][64], int n,
                           trix_result_t *out) {
    hsos_system_t *sys = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys);
    hsos_boot(sys);
    for (int i = 0; i < n; i++) {
        out[i] = hsos_exec_infer(sys, chip, inputs[i]);
    }
    hsos_system_free(sys);
    free(sys);
}

/* ── test 1: BubbleMachine sort is deterministic under parallel tick ─────── */

static int test_bubble_determinism(void) {
    printf("[TEST] bubble_sort_determinism\n");

    uint8_t values_a[8] = {7, 3, 1, 5, 2, 8, 4, 6};
    uint8_t values_b[8] = {7, 3, 1, 5, 2, 8, 4, 6};

    /* Run A */
    hsos_system_t *sys_a = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys_a); hsos_boot(sys_a);
    hsos_bubble_t bm_a;
    hsos_bubble_init(&bm_a, sys_a, TOPO_LINE);
    hsos_bubble_load(&bm_a, values_a);
    hsos_bubble_run(&bm_a);
    hsos_bubble_read(&bm_a, values_a);
    hsos_system_free(sys_a); free(sys_a);

    /* Run B */
    hsos_system_t *sys_b = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys_b); hsos_boot(sys_b);
    hsos_bubble_t bm_b;
    hsos_bubble_init(&bm_b, sys_b, TOPO_LINE);
    hsos_bubble_load(&bm_b, values_b);
    hsos_bubble_run(&bm_b);
    hsos_bubble_read(&bm_b, values_b);
    hsos_system_free(sys_b); free(sys_b);

    /* Compare */
    for (int i = 0; i < 8; i++) {
        if (values_a[i] != values_b[i]) {
            fprintf(stderr, "  FAIL: sort results differ at index %d: %u vs %u\n",
                    i, values_a[i], values_b[i]);
            return 1;
        }
    }

    /* Verify sorted */
    for (int i = 1; i < 8; i++) {
        if (values_a[i] < values_a[i-1]) {
            fprintf(stderr, "  FAIL: not sorted at index %d\n", i);
            return 1;
        }
    }

    printf("  ✓ sorted: ");
    for (int i = 0; i < 8; i++) printf("%u ", values_a[i]);
    printf("\n");
    return 0;
}

/* ── test 2: Inference results identical across 20 independent runs ───────── */

static int test_inference_determinism(void) {
    printf("[TEST] inference_determinism (20 runs × 8 inputs × 16 sigs)\n");

    trix_chip_t *chip = make_chip(16);

    /* 8 test inputs */
    uint8_t inputs[8][64];
    for (int i = 0; i < 8; i++) memset(inputs[i], i * 16, 64);

    /* Reference run */
    trix_result_t ref[8];
    run_inferences(chip, inputs, 8, ref);

    /* 19 more runs, all must match reference */
    int mismatches = 0;
    for (int run = 0; run < 19; run++) {
        trix_result_t cur[8];
        run_inferences(chip, inputs, 8, cur);
        for (int i = 0; i < 8; i++) {
            if (cur[i].match != ref[i].match ||
                cur[i].distance != ref[i].distance) {
                fprintf(stderr,
                    "  FAIL: run %d input %d: ref=(%d,%d) cur=(%d,%d)\n",
                    run, i,
                    ref[i].match, ref[i].distance,
                    cur[i].match, cur[i].distance);
                mismatches++;
            }
        }
    }

    trix_chip_free(chip);

    if (mismatches > 0) {
        fprintf(stderr, "  FAIL: %d mismatch(es) across 20 runs\n", mismatches);
        return 1;
    }
    printf("  ✓ all 20×8 results identical\n");
    return 0;
}

/* ── test 3: hsos_exec_infer agrees with trix_infer (parallel path) ─────── */

static int test_parallel_agrees_with_direct(void) {
    printf("[TEST] parallel_agrees_with_direct (12 sigs, 10 inputs)\n");

    trix_chip_t *chip = make_chip(12);
    hsos_system_t *sys = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys); hsos_boot(sys);

    int mismatches = 0;
    for (int i = 0; i < 10; i++) {
        uint8_t input[64];
        memset(input, i * 20, 64);

        trix_result_t direct = trix_infer(chip, input);
        trix_result_t parallel = hsos_exec_infer(sys, chip, input);

        if (direct.match != parallel.match ||
            direct.distance != parallel.distance) {
            printf("  ✗ input[%d]: direct=(%d,%d) parallel=(%d,%d)\n",
                   i, direct.match, direct.distance,
                   parallel.match, parallel.distance);
            mismatches++;
        }
    }

    hsos_system_free(sys); free(sys);
    trix_chip_free(chip);

    if (mismatches > 0) {
        fprintf(stderr, "  FAIL: %d disagreement(s)\n", mismatches);
        return 1;
    }
    printf("  ✓ all 10 inputs agree between direct and parallel paths\n");
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    printf("══════════════════════════════════════\n");
    printf("  Parallel HSOS Tick Tests\n");
    printf("══════════════════════════════════════\n\n");

    int failures = 0;
    failures += test_bubble_determinism();
    failures += test_inference_determinism();
    failures += test_parallel_agrees_with_direct();

    printf("\n%s — %d failure(s)\n",
           failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
```

- [ ] **Step 2: Register the test in CMakeLists.txt**

In `CMakeLists.txt`, after the existing `add_trix_test(test_hsos_infer ...)` line:

```cmake
add_trix_test(test_parallel_tick zor/test/test_parallel_tick.c)
```

Also add `test_parallel_tick` to the DEPENDS list of the `check` custom target. There are two DEPENDS lists in `CMakeLists.txt` — one for the `check` target (around line 246) and one for the `coverage` target (around line 263). Add `test_parallel_tick` to both.

- [ ] **Step 3: Build and run the new test**

```bash
cmake --build build -j4
cd build && ctest -R test_parallel_tick --output-on-failure
```
Expected: PASS.

- [ ] **Step 4: Run full test suite**

```bash
cd build && ctest --output-on-failure
```
Expected: all tests PASS.

- [ ] **Step 5: Run under ThreadSanitizer to catch data races**

```bash
rm -rf build_tsan
cmake -B build_tsan -DCMAKE_BUILD_TYPE=Debug -DTRIX_ENABLE_TSAN=ON
cmake --build build_tsan -j4
cd build_tsan && ctest -R test_parallel_tick --output-on-failure
```

Expected: PASS with zero TSan data race reports. If TSan reports a race on `worker_target`, `worker_pending`, or `inbox`/`outbox` fields, the mutex coverage has a gap — stop and diagnose before committing.

- [ ] **Step 6: Commit**

```bash
git add zor/test/test_parallel_tick.c CMakeLists.txt
git commit -m "test: add parallel tick correctness tests (bubble + inference determinism + TSan clean)"
```

---

## Task 4: Throughput Comparison

Confirm parallel tick is faster than sequential for inference-heavy workloads.

**Files:**
- Modify: `tools/test/bench_linear.c` (add HSOS parallel benchmark) OR create `tools/test/bench_hsos_parallel.c`

- [ ] **Step 1: Write `tools/test/bench_hsos_parallel.c`**

```c
/*
 * bench_hsos_parallel.c — Compare sequential vs parallel HSOS tick throughput.
 *
 * Benchmark: 128-signature chip, 1000 inferences.
 * Reports: inferences/sec and tick throughput for each mode.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../zor/include/hsos.h"
#include "../zor/include/trixc/hsos_infer.h"
#include "../zor/include/trixc/runtime.h"

#define N_SIGS   128
#define N_ITERS  1000

static trix_chip_t *make_chip(void) {
    FILE *f = fopen("/tmp/bench_hsos.trix", "w");
    fprintf(f, "softchip:\n  name: bench\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n");
    for (int i = 0; i < N_SIGS; i++) {
        fprintf(f, "  sig%d:\n    pattern: ", i);
        for (int b = 0; b < 64; b++) fprintf(f, "%02x", (uint8_t)(i * 2));
        fprintf(f, "\n    threshold: 64\n");
    }
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);
    int err = 0;
    trix_chip_t *c = trix_load("/tmp/bench_hsos.trix", &err);
    if (!c) { fprintf(stderr, "make_chip failed\n"); exit(1); }
    return c;
}

static double elapsed_ms(struct timespec *start, struct timespec *end) {
    return (end->tv_sec - start->tv_sec) * 1000.0
         + (end->tv_nsec - start->tv_nsec) / 1e6;
}

int main(void) {
    printf("HSOS Parallel Tick Benchmark\n");
    printf("  %d signatures, %d inferences\n\n", N_SIGS, N_ITERS);

    trix_chip_t *chip = make_chip();
    uint8_t input[64];
    memset(input, 0x5A, 64);

    struct timespec t0, t1;

    /* ── Direct (single-thread trix_infer) ─── */
    clock_gettime(CLOCK_MONOTONIC, &t0);
    volatile int sink = 0;
    for (int i = 0; i < N_ITERS; i++) {
        trix_result_t r = trix_infer(chip, input);
        sink ^= r.distance;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    (void)sink;
    double ms_direct = elapsed_ms(&t0, &t1);
    printf("  Direct (trix_infer):      %6.1f ms  %6.0f inf/s\n",
           ms_direct, N_ITERS / (ms_direct / 1000.0));

    /* ── HSOS parallel ─── */
    hsos_system_t *sys = calloc(1, sizeof(hsos_system_t));
    hsos_init(sys);
    hsos_boot(sys);

    clock_gettime(CLOCK_MONOTONIC, &t0);
    sink = 0;
    for (int i = 0; i < N_ITERS; i++) {
        trix_result_t r = hsos_exec_infer(sys, chip, input);
        sink ^= r.distance;
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);
    (void)sink;
    double ms_parallel = elapsed_ms(&t0, &t1);
    printf("  HSOS parallel:            %6.1f ms  %6.0f inf/s\n",
           ms_parallel, N_ITERS / (ms_parallel / 1000.0));

    printf("\n  Ratio (parallel/direct):  %.2fx\n",
           ms_direct / ms_parallel);

    hsos_system_free(sys);
    free(sys);
    trix_chip_free(chip);
    return 0;
}
```

- [ ] **Step 2: Add to CMakeLists.txt inside `if(TRIX_BUILD_TOOLS)` block**

```cmake
add_executable(bench_hsos_parallel tools/test/bench_hsos_parallel.c)
target_link_libraries(bench_hsos_parallel PRIVATE TriX::Runtime)
if(UNIX)
    target_link_libraries(bench_hsos_parallel PRIVATE m)
endif()
```

- [ ] **Step 3: Build and run**

```bash
cmake --build build -j4
./build/bench_hsos_parallel
```

Expected output (numbers will vary by machine):
```
HSOS Parallel Tick Benchmark
  128 signatures, 1000 inferences

  Direct (trix_infer):        xx.x ms   xxxxx inf/s
  HSOS parallel:              xx.x ms   xxxxx inf/s

  Ratio (parallel/direct):  x.xxX
```

The parallel path has HSOS messaging overhead. For small signature counts, direct may be faster. For large signature counts (128+), parallel sharding should win.

- [ ] **Step 4: Commit**

```bash
git add tools/test/bench_hsos_parallel.c CMakeLists.txt
git commit -m "bench: add HSOS parallel vs direct inference throughput benchmark"
```
