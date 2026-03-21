# Ternary SMMLA End-to-End Integration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Wire linear-layer weight execution into `trix_infer()` so that `.trix` chips with `linear:` sections transform raw input bytes through SDOT/SMMLA MatVec layers before Hamming matching, and benchmark throughput against fp32.

**Architecture:** A new `linear_runtime.c` bridge reads weights loaded by `trix_load()`, applies SDOT (ARM64) or portable MatVec per layer, sign-bit-quantizes the final layer output to 64 bytes, then returns control to the existing Hamming engine. `trix_infer()` gains a conditional pre-processing step: when `num_linear_layers > 0`, call `trix_exec_linear()` before signature matching.

**Tech Stack:** C11, ARM NEON `vdotq_s32` (SDOT, `__ARM_FEATURE_DOTPROD`), `vmmlaq_s32` (SMMLA, `__ARM_FEATURE_MATMUL_INT8`), CLOCK_MONOTONIC benchmark, existing CMake.

---

## File Map

| File | Action | Purpose |
|------|--------|---------|
| `zor/include/trixc/chip_private.h` | **Create** | Shared `struct trix_chip` definition (weight fields added) |
| `zor/src/runtime.c` | **Modify** | Include chip_private.h; load weights in `trix_load()`; free in `trix_chip_free()` |
| `zor/include/trixc/linear_runtime.h` | **Create** | Public API: `trix_exec_linear()` |
| `zor/src/linear_runtime.c` | **Create** | Portable + SDOT + SMMLA MatVec, sign binarize, `trix_exec_linear()` |
| `zor/test/test_linear_runtime.c` | **Create** | Tests: matvec correctness, binarize, weight loading, end-to-end infer |
| `CMakeLists.txt` | **Modify** | Add `linear_runtime.c` to sources; add `test_linear_runtime` test target; add `bench_linear` |
| `tools/test/bench_linear.c` | **Create** | SDOT vs fp32 vs SMMLA throughput benchmark |

---

## Data Flow

```
uint8_t input[64]
    ↓ cast to int8_t*
Layer 0:  int8_t[K] → SDOT/SMMLA → int32_t[N0]
    ↓ clamp to int8_t [-127, 127]
Layer 1:  int8_t[N0] → SDOT/SMMLA → int32_t[N1]
    ↓ (for last layer only)
sign-bit quantize: z > 0 → 1, z ≤ 0 → 0  →  uint8_t[64] (512 bits)
    ↓
Hamming matching (existing trix_infer engine)
```

**Constraints enforced at load time:**
- `last_layer.output_dim == chip->state_bits` (must equal 512)
- `first_layer.input_dim <= 64` (input is always 64 bytes)
- `first_layer.input_dim % 4 == 0` (SDOT alignment)

---

## Task 1: Private chip struct + weight storage

**Files:**
- Create: `zor/include/trixc/chip_private.h`
- Modify: `zor/src/runtime.c` (struct def removed, trix_load weight loading added, trix_chip_free weight freeing added)

- [ ] **Step 1: Create `zor/include/trixc/chip_private.h`**

```c
/*
 * chip_private.h — Internal trix_chip struct definition
 *
 * Shared between runtime.c and linear_runtime.c.
 * NOT part of the public API.
 */

#ifndef TRIXC_CHIP_PRIVATE_H
#define TRIXC_CHIP_PRIVATE_H

#include <stdint.h>

#define CHIP_MAX_SIGNATURES   128
#define CHIP_STATE_BYTES       64
#define CHIP_MAX_LINEAR_LAYERS 16

struct trix_chip {
    /* Chip metadata */
    char name[64];
    char version[16];
    int state_bits;

    /* Signatures */
    uint8_t signatures[CHIP_MAX_SIGNATURES][CHIP_STATE_BYTES];
    int thresholds[CHIP_MAX_SIGNATURES];
    int shapes[CHIP_MAX_SIGNATURES];
    char labels[CHIP_MAX_SIGNATURES][64];
    int num_signatures;

    /* Shapes */
    int num_shapes;

    /* Linear layers */
    int num_linear_layers;
    int layer_input_dim[CHIP_MAX_LINEAR_LAYERS];
    int layer_output_dim[CHIP_MAX_LINEAR_LAYERS];
    int8_t *layer_weights[CHIP_MAX_LINEAR_LAYERS];      /* raw [N×K] row-major */
    int8_t *layer_weights_packed[CHIP_MAX_LINEAR_LAYERS]; /* I8MM [N/2, K/8, 16] */

    /* Mode */
    int mode;
    char default_label[64];
};

#endif /* TRIXC_CHIP_PRIVATE_H */
```

- [ ] **Step 2: Update `zor/src/runtime.c` — replace inline struct with chip_private.h**

**First, find all occurrences of the old constant names (line numbers may have shifted):**

```bash
grep -n "MAX_SIGNATURES\|STATE_BYTES" zor/src/runtime.c
```

Remove the `struct trix_chip { ... };` block (currently around lines 169-191) and the two `#define MAX_SIGNATURES`/`#define STATE_BYTES` lines (currently around lines 21-22).

Add this include after the existing includes (around line 16):

```c
#include "../include/trixc/chip_private.h"
```

**Keep** the existing `#include "../../tools/include/softchip.h"` — it is still needed for `softchip_parse()`.

Search-replace every remaining occurrence:
- `MAX_SIGNATURES` → `CHIP_MAX_SIGNATURES`
- `STATE_BYTES` → `CHIP_STATE_BYTES`

Use the grep output from above to verify no occurrences are missed.

- [ ] **Step 3: Add weight loading in `trix_load()` — after the existing signature copy loop (after line 242)**

```c
    /* Load linear layer weights */
    for (int i = 0; i < spec.num_linear_layers && i < CHIP_MAX_LINEAR_LAYERS; i++) {
        chip->layer_input_dim[i]  = spec.linear_layers[i].input_dim;
        chip->layer_output_dim[i] = spec.linear_layers[i].output_dim;
        chip->layer_weights[i]    = NULL;
        chip->layer_weights_packed[i] = NULL;

        if (spec.linear_layers[i].weights_file[0] == '\0') {
            log_debug("Layer %d: no weights file", i);
            continue;
        }

        int K = spec.linear_layers[i].input_dim;
        int N = spec.linear_layers[i].output_dim;
        size_t weight_bytes = (size_t)N * K;

        FILE *wf = fopen(spec.linear_layers[i].weights_file, "rb");
        if (!wf) {
            log_error("trix_load: cannot open weights '%s'",
                      spec.linear_layers[i].weights_file);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_FILE_NOT_FOUND;
            return NULL;
        }

        chip->layer_weights[i] = (int8_t*)malloc(weight_bytes);
        if (!chip->layer_weights[i]) {
            fclose(wf);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        size_t nread = fread(chip->layer_weights[i], 1, weight_bytes, wf);
        fclose(wf);

        if (nread != weight_bytes) {
            log_error("trix_load: weights file '%s' too short: got %zu, want %zu",
                      spec.linear_layers[i].weights_file, nread, weight_bytes);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_FILE_READ;
            return NULL;
        }

        log_debug("Layer %d: loaded %d×%d weights (%zu bytes)",
                  i, N, K, weight_bytes);
    }

    /* Validate constraints on linear layers */
    if (chip->num_linear_layers > 0) {
        /* First layer input_dim must fit in the 64-byte input and be 4-byte aligned */
        if (chip->layer_input_dim[0] > 64 || chip->layer_input_dim[0] % 4 != 0) {
            log_error("trix_load: layer 0 input_dim=%d must be ≤64 and divisible by 4",
                      chip->layer_input_dim[0]);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_INVALID_DIMENSIONS;
            return NULL;
        }
        /* Last layer output_dim must equal state_bits */
        int last = chip->num_linear_layers - 1;
        if (chip->layer_output_dim[last] != chip->state_bits) {
            log_error("trix_load: last linear layer output_dim=%d != state_bits=%d",
                      chip->layer_output_dim[last], chip->state_bits);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_INVALID_DIMENSIONS;
            return NULL;
        }
    }
```

**Error codes used** (all confirmed in `zor/include/trixc/errors.h`):
- `TRIX_ERROR_FILE_NOT_FOUND` = 100 — weights file missing
- `TRIX_ERROR_FILE_READ` = 101 — weights file too short
- `TRIX_ERROR_OUT_OF_MEMORY` = 500 — malloc failure
- `TRIX_ERROR_INVALID_DIMENSIONS` = 318 — output_dim mismatch or input_dim out of range

- [ ] **Step 4: Add weight freeing in `trix_chip_free()`**

Replace the existing `trix_chip_free()` body (currently just `free(chip)`) with:

```c
void trix_chip_free(trix_chip_t* chip) {
    if (!chip) return;
    log_info("Freeing chip '%s'", chip->name);
    for (int i = 0; i < CHIP_MAX_LINEAR_LAYERS; i++) {
        free(chip->layer_weights[i]);
        free(chip->layer_weights_packed[i]);
    }
    free(chip);
}
```

- [ ] **Step 5: Build to verify it compiles**

```bash
cd /path/to/trix && mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Debug 2>&1 | tail -5
make trix_runtime_static 2>&1 | grep -E "error:|warning:|Built"
```

Expected: no errors (warnings about unused variables OK).

- [ ] **Step 6: Check errors.h for available error codes**

```bash
grep -E "TRIX_ERROR_" zor/include/trixc/errors.h | head -20
```

Use exact error code names in Step 3. If `TRIX_ERROR_FILE_NOT_FOUND` doesn't exist, substitute the closest available code.

- [ ] **Step 7: Commit**

```bash
git add zor/include/trixc/chip_private.h zor/src/runtime.c
git commit -m "feat: add linear layer weight storage and loading to trix_chip_t"
```

---

## Task 2: linear_runtime.c + tests

**Files:**
- Create: `zor/include/trixc/linear_runtime.h`
- Create: `zor/src/linear_runtime.c`
- Create: `zor/test/test_linear_runtime.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write failing tests in `zor/test/test_linear_runtime.c`**

```c
/*
 * test_linear_runtime.c — Tests for linear layer forward pass and binarization
 */

#include "../include/trixc/runtime.h"
#include "../include/trixc/linear_runtime.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

/* ── test 1: portable matvec correctness ─────────────────────────────────── */

static int test_portable_matvec(void) {
    printf("[TEST] portable_matvec\n");

    /*
     * K=4, N=2
     * W = [[1, 0, -1, 1],   (row 0)
     *       [0, 1,  1, -1]]  (row 1)
     * x = [1, 2, 3, 4]
     * y[0] = 1*1 + 0*2 + (-1)*3 + 1*4 = 2
     * y[1] = 0*1 + 1*2 + 1*3 + (-1)*4 = 1
     */
    int8_t W[8] = {1, 0, -1, 1, 0, 1, 1, -1};
    int8_t x[4] = {1, 2, 3, 4};
    int32_t y[2] = {0};

    linear_matvec_portable(4, 2, x, W, y);

    if (y[0] != 2) {
        fprintf(stderr, "FAIL portable_matvec: y[0]=%d want 2\n", y[0]);
        return 1;
    }
    if (y[1] != 1) {
        fprintf(stderr, "FAIL portable_matvec: y[1]=%d want 1\n", y[1]);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 2: sign binarize ───────────────────────────────────────────────── */

static int test_sign_binarize(void) {
    printf("[TEST] sign_binarize\n");

    /*
     * 8 values → 1 byte (MSB first):
     * z = [1, -1, 5, -3, 0, 2, -1, 7]
     * bits (> 0 → 1): [1, 0, 1, 0, 0, 1, 0, 1] → 0b10100101 = 0xA5
     */
    int32_t z[8] = {1, -1, 5, -3, 0, 2, -1, 7};
    uint8_t out[1] = {0};

    linear_sign_binarize(z, 8, out);

    if (out[0] != 0xA5) {
        fprintf(stderr, "FAIL sign_binarize: got 0x%02x want 0xA5\n", out[0]);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 3: end-to-end weight load + trix_exec_linear ───────────────────── */

static int test_exec_linear_end_to_end(void) {
    printf("[TEST] exec_linear_end_to_end\n");

    /*
     * K=4, N=512 (state_bits=512)
     * W[0][0] = 1, all other weights = 0
     * x = [1, 0, 0, 0]
     * y[0] = 1 > 0 → bit 1
     * y[1..511] = 0 ≤ 0 → bit 0
     * Expected out[0] = 0x80, out[1..63] = 0x00
     */

    /* Write weight file: 512 rows × 4 columns = 2048 bytes */
    const char *wfile = "/tmp/test_linear_weights.bin";
    int8_t *W = calloc(512 * 4, 1);
    if (!W) { fprintf(stderr, "FAIL alloc\n"); return 1; }
    W[0] = 1;  /* W[0][0] = 1 */
    FILE *f = fopen(wfile, "wb");
    if (!f) { free(W); fprintf(stderr, "FAIL open weights file\n"); return 1; }
    fwrite(W, 1, 512 * 4, f);
    fclose(f);
    free(W);

    /* Write .trix file */
    const char *tfile = "/tmp/test_linear_chip.trix";
    f = fopen(tfile, "w");
    if (!f) { fprintf(stderr, "FAIL open trix file\n"); return 1; }
    fprintf(f, "softchip:\n  name: test_linear\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "linear:\n");
    fprintf(f, "  embed:\n");
    fprintf(f, "    input_dim: 4\n");
    fprintf(f, "    output_dim: 512\n");
    fprintf(f, "    weights: %s\n", wfile);
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {1, 0, 0, 0};  /* rest zero-initialized */
    uint8_t out[64] = {0};

    int rc = trix_exec_linear(chip, input, out);
    trix_chip_free(chip);

    if (rc != 0) {
        fprintf(stderr, "FAIL trix_exec_linear returned %d\n", rc);
        return 1;
    }
    if (out[0] != 0x80) {
        fprintf(stderr, "FAIL out[0]=0x%02x want 0x80\n", out[0]);
        return 1;
    }
    for (int i = 1; i < 64; i++) {
        if (out[i] != 0) {
            fprintf(stderr, "FAIL out[%d]=0x%02x want 0x00\n", i, out[i]);
            return 1;
        }
    }
    printf("  PASS\n");
    return 0;
}

/* ── test 4: zero linear layers returns error ─────────────────────────────── */

static int test_exec_linear_no_layers(void) {
    printf("[TEST] exec_linear_no_layers\n");

    /* Build a chip with no linear layers */
    const char *tfile = "/tmp/test_nolayers.trix";
    FILE *f = fopen(tfile, "w");
    fprintf(f, "softchip:\n  name: nolayers\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n  sig0:\n    pattern: ");
    for (int i = 0; i < 64; i++) fprintf(f, "00");
    fprintf(f, "\n    threshold: 0\n");
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {0};
    uint8_t out[64]   = {0};
    int rc = trix_exec_linear(chip, input, out);
    trix_chip_free(chip);

    /* Should return non-zero (error: no layers) */
    if (rc == 0) {
        fprintf(stderr, "FAIL: expected error on zero layers, got 0\n");
        return 1;
    }
    printf("  PASS\n");
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    failures += test_portable_matvec();
    failures += test_sign_binarize();
    failures += test_exec_linear_end_to_end();
    failures += test_exec_linear_no_layers();
    if (failures == 0) {
        printf("All linear_runtime tests passed.\n");
    } else {
        printf("%d linear_runtime test(s) FAILED.\n", failures);
    }
    return failures;
}
```

- [ ] **Step 2: Add `test_linear_runtime` to `CMakeLists.txt` (inside `if(TRIX_BUILD_TESTS)` block)**

After the existing `add_trix_test(test_hsos_infer ...)` line (~line 213), add:

```cmake
    add_trix_test(test_linear_runtime zor/test/test_linear_runtime.c)
```

Also add `test_linear_runtime` to the `check` target's DEPENDS list.

- [ ] **Step 3: Run test to verify it fails**

```bash
cd build && cmake .. -DCMAKE_BUILD_TYPE=Debug && make test_linear_runtime 2>&1 | tail -5
```

Expected: FAIL — `linear_matvec_portable`, `linear_sign_binarize`, `trix_exec_linear` undefined.

- [ ] **Step 4: Create `zor/include/trixc/linear_runtime.h`**

```c
/*
 * linear_runtime.h — Linear layer forward pass for TriX
 *
 * Bridges linear MatVec layers to the Hamming inference engine.
 * Internal functions exposed for testing only.
 */

#ifndef TRIXC_LINEAR_RUNTIME_H
#define TRIXC_LINEAR_RUNTIME_H

#include <stdint.h>
#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run all linear layers on input, produce 64-byte binary output.
 *
 * Requires chip->num_linear_layers > 0 and weights loaded.
 * Returns TRIX_OK on success, error code otherwise.
 *
 * @param chip   Chip handle (must have linear layers loaded)
 * @param input  Raw input bytes; first layer uses layer_input_dim[0] bytes
 * @param out    64-byte sign-bit-quantized output (512 bits)
 */
int trix_exec_linear(const trix_chip_t *chip, const uint8_t *input, uint8_t out[64]);

/* Exposed for unit testing */
void linear_matvec_portable(int K, int N, const int8_t *x, const int8_t *W, int32_t *y);
void linear_sign_binarize(const int32_t *z, int N, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_LINEAR_RUNTIME_H */
```

- [ ] **Step 5: Create `zor/src/linear_runtime.c`**

```c
/*
 * linear_runtime.c — Linear layer forward pass for TriX
 *
 * Pipeline: int8 MatVec (SDOT/SMMLA or portable C)
 *           → inter-layer int8 clamp
 *           → final layer sign-bit binarize → uint8_t[64]
 */

#include "../include/trixc/linear_runtime.h"
#include "../include/trixc/chip_private.h"
#include "../include/trixc/errors.h"
#include "../include/trixc/logging.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

/* ── Portable fallback MatVec ─────────────────────────────────────────────── */

/*
 * Compute y[n] = sum_k( x[k] * W[n*K + k] ) for n in [0, N).
 * W is row-major: row n starts at W + n*K.
 */
void linear_matvec_portable(int K, int N, const int8_t *x, const int8_t *W, int32_t *y) {
    for (int n = 0; n < N; n++) {
        int32_t acc = 0;
        const int8_t *row = W + (size_t)n * K;
        for (int k = 0; k < K; k++) {
            acc += (int32_t)x[k] * (int32_t)row[k];
        }
        y[n] = acc;
    }
}

/* ── SDOT MatVec (ARM64 with __ARM_FEATURE_DOTPROD) ─────────────────────── */

#if defined(__ARM_FEATURE_DOTPROD)
/*
 * vdotq_s32: acc[i] += a[i*4+0]*b[i*4+0] + ... + a[i*4+3]*b[i*4+3], i=0..3
 * Process K in chunks of 16 int8 (= 4 vdotq lanes × 4 elements).
 * Requires K % 16 == 0; remainder handled portably.
 */
static void matvec_sdot(int K, int N, const int8_t *x, const int8_t *W, int32_t *y) {
    int K16 = K & ~15;  /* largest multiple of 16 ≤ K */

    for (int n = 0; n < N; n++) {
        const int8_t *row = W + (size_t)n * K;
        int32x4_t acc = vdupq_n_s32(0);

        for (int k = 0; k < K16; k += 16) {
            int8x16_t va = vld1q_s8(x   + k);
            int8x16_t vb = vld1q_s8(row + k);
            acc = vdotq_s32(acc, va, vb);
        }

        /* Sum 4 lanes */
        int32_t sum = vaddvq_s32(acc);

        /* Handle remainder portably */
        for (int k = K16; k < K; k++) {
            sum += (int32_t)x[k] * (int32_t)row[k];
        }
        y[n] = sum;
    }
}
#endif /* __ARM_FEATURE_DOTPROD */

/* ── SMMLA MatVec (ARM64 with __ARM_FEATURE_MATMUL_INT8) ────────────────── */

#if defined(__ARM_FEATURE_MATMUL_INT8)
/*
 * vmmlaq_s32: acc[2×2] += A[2×8] * B[8×2]
 * Weights must be pre-packed into [N/2, K/8, 16] layout.
 * Requires N % 2 == 0, K % 8 == 0.
 */
static void pack_i8mm(int K, int N, const int8_t *src, int8_t *dst) {
    int K_blocks = K / 8;
    int N_pairs  = N / 2;
    for (int np = 0; np < N_pairs; np++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            int8_t *block = dst + (np * K_blocks + kb) * 16;
            /* Row n */
            for (int i = 0; i < 8; i++)
                block[i]     = src[(np * 2)     * K + kb * 8 + i];
            /* Row n+1 */
            for (int i = 0; i < 8; i++)
                block[8 + i] = src[(np * 2 + 1) * K + kb * 8 + i];
        }
    }
}

static void matvec_smmla(int K, int N, const int8_t *x, const int8_t *W_packed, int32_t *y) {
    int K_blocks = K / 8;
    int N_pairs  = N / 2;

    /* Synthesize a second "batch" row of zeros so we can use the 2×8×2 kernel */
    int8_t *x_zero = calloc(K, 1);
    if (!x_zero) {
        /* Fallback: won't happen in practice for small K, but be safe */
        linear_matvec_portable(K, N, x, W_packed, y);  /* wrong layout, but safe */
        return;
    }

    for (int np = 0; np < N_pairs; np++) {
        const int8_t *w_ptr = W_packed + np * K_blocks * 16;
        int32x4_t acc = vdupq_n_s32(0);

        for (int kb = 0; kb < K_blocks; kb++) {
            int8x16_t a_block = vcombine_s8(
                vld1_s8(x      + kb * 8),
                vld1_s8(x_zero + kb * 8)
            );
            int8x16_t w_block = vld1q_s8(w_ptr);
            w_ptr += 16;
            acc = vmmlaq_s32(acc, a_block, w_block);
        }

        /* acc[0] and acc[1] = results for row 0 (our actual x) */
        y[np * 2]     = vgetq_lane_s32(acc, 0);
        y[np * 2 + 1] = vgetq_lane_s32(acc, 1);
    }

    free(x_zero);
}
#endif /* __ARM_FEATURE_MATMUL_INT8 */

/* ── Sign binarize ───────────────────────────────────────────────────────── */

/*
 * Pack N sign bits (z > 0 → 1, z ≤ 0 → 0) into out[] MSB-first.
 * out must be at least ceil(N/8) bytes, zeroed by caller.
 */
void linear_sign_binarize(const int32_t *z, int N, uint8_t *out) {
    for (int i = 0; i < N; i++) {
        if (z[i] > 0) {
            out[i / 8] |= (uint8_t)(1u << (7 - (i % 8)));
        }
    }
}

/* ── Clamp int32 to int8 ─────────────────────────────────────────────────── */

static void clamp_to_int8(const int32_t *in, int8_t *out, int N) {
    for (int i = 0; i < N; i++) {
        int32_t v = in[i];
        if (v >  127) v =  127;
        if (v < -127) v = -127;
        out[i] = (int8_t)v;
    }
}

/* ── Choose matvec implementation ────────────────────────────────────────── */

typedef void (*matvec_fn)(int K, int N, const int8_t *x, const int8_t *W, int32_t *y);

static matvec_fn choose_matvec(void) {
#if defined(__ARM_FEATURE_MATMUL_INT8)
    return matvec_smmla;  /* uses packed weights */
#elif defined(__ARM_FEATURE_DOTPROD)
    return matvec_sdot;
#else
    return linear_matvec_portable;
#endif
}

/* ── Public API ──────────────────────────────────────────────────────────── */

int trix_exec_linear(const trix_chip_t *chip, const uint8_t *input, uint8_t out[64]) {
    if (!chip || !input || !out) return TRIX_ERROR_NULL_POINTER;
    if (chip->num_linear_layers <= 0) return TRIX_ERROR_NOT_IMPLEMENTED;

    static matvec_fn matvec = NULL;
    if (!matvec) matvec = choose_matvec();

    /* Working buffers: one input int8 buffer + one output int32 buffer */
    int max_N = 0;
    for (int i = 0; i < chip->num_linear_layers; i++) {
        if (chip->layer_output_dim[i] > max_N) max_N = chip->layer_output_dim[i];
    }

    int32_t *y = malloc((size_t)max_N * sizeof(int32_t));
    if (!y) return TRIX_ERROR_OUT_OF_MEMORY;

    /* First layer input: cast input bytes to int8 */
    int K0 = chip->layer_input_dim[0];
    int8_t *cur_input = malloc((size_t)K0);
    if (!cur_input) { free(y); return TRIX_ERROR_OUT_OF_MEMORY; }
    memcpy(cur_input, input, (size_t)K0);  /* uint8 bytes → int8 (bit-identical cast) */

    for (int i = 0; i < chip->num_linear_layers; i++) {
        int K = chip->layer_input_dim[i];
        int N = chip->layer_output_dim[i];
        const int8_t *W = chip->layer_weights[i];

#if defined(__ARM_FEATURE_MATMUL_INT8)
        /* SMMLA needs packed weights; pack on first use */
        if (!chip->layer_weights_packed[i] && W) {
            /* Cast away const: we own the chip's packed buffer. */
            trix_chip_t *mchip = (trix_chip_t *)(uintptr_t)chip;
            mchip->layer_weights_packed[i] = malloc((size_t)(N / 2) * (K / 8) * 16);
            if (!mchip->layer_weights_packed[i]) {
                /* Packed weight allocation failed — cannot run SMMLA.
                 * Return OOM rather than silently calling matvec_smmla
                 * with the wrong (unpacked) layout. */
                free(cur_input);
                free(y);
                return TRIX_ERROR_OUT_OF_MEMORY;
            }
            pack_i8mm(K, N, W, mchip->layer_weights_packed[i]);
            W = mchip->layer_weights_packed[i];
        } else if (chip->layer_weights_packed[i]) {
            W = chip->layer_weights_packed[i];
        }
#endif

        if (!W) {
            free(cur_input);
            free(y);
            return TRIX_ERROR_NOT_IMPLEMENTED;
        }

        memset(y, 0, (size_t)N * sizeof(int32_t));
        matvec(K, N, cur_input, W, y);

        if (i < chip->num_linear_layers - 1) {
            /* Not last layer: clamp to int8 for next layer */
            int next_K = chip->layer_input_dim[i + 1];
            int8_t *next_input = malloc((size_t)next_K);
            if (!next_input) {
                free(cur_input);
                free(y);
                return TRIX_ERROR_OUT_OF_MEMORY;
            }
            clamp_to_int8(y, next_input, N);
            free(cur_input);
            cur_input = next_input;
        }
    }

    /* Last layer: sign binarize into 64 bytes */
    int last_N = chip->layer_output_dim[chip->num_linear_layers - 1];
    memset(out, 0, 64);
    linear_sign_binarize(y, last_N, out);

    free(cur_input);
    free(y);
    return TRIX_OK;
}
```

**Memory safety note on `cur_input`:** The `free(cur_input)` at the end is always correct. When `num_linear_layers > 1`, the final `cur_input` points to the `next_input` allocated in the last inter-layer transition — the original malloc from line ~681 was freed inside the loop. When `num_linear_layers == 1`, the loop's `if` branch is never entered, so `cur_input` still points to the original malloc. Do NOT add a "double-free fix" here — the logic is correct as written.

**Note on SMMLA const-cast:** The `const trix_chip_t*` parameter conflicts with lazy-packing the weight buffer on first call. Two options: (a) do the cast as shown, (b) pack at `trix_load()` time. Option (b) is cleaner — move the `pack_i8mm` call into `trix_load()` in Task 1's weight loading loop when `__ARM_FEATURE_MATMUL_INT8` is defined. The const-cast is fine for a first pass since it's an internal optimization, but document the ugliness.

- [ ] **Step 6: Add `linear_runtime.c` to `TRIX_RUNTIME_SOURCES` in `CMakeLists.txt`**

In `CMakeLists.txt`, find `set(TRIX_RUNTIME_SOURCES` (~line 90) and add:

```cmake
    zor/src/linear_runtime.c
```

- [ ] **Step 7: Build and run tests**

```bash
cd build && cmake .. && make test_linear_runtime && ./test_linear_runtime
```

Expected: all 4 tests pass.

If SDOT/SMMLA tests fail on non-ARM, that's expected — portable fallback handles it.

- [ ] **Step 8: Commit**

```bash
git add zor/include/trixc/linear_runtime.h zor/src/linear_runtime.c \
        zor/test/test_linear_runtime.c CMakeLists.txt
git commit -m "feat: add linear_runtime.c with SDOT/portable MatVec and sign binarize"
```

---

## Task 3: Wire `trix_exec_linear()` into `trix_infer()`

**Files:**
- Modify: `zor/src/runtime.c`

- [ ] **Step 1: Add failing test for linear-layer inference to `test_linear_runtime.c`**

Add this test before `main()`:

```c
/* ── test 5: trix_infer calls linear layers automatically ─────────────────── */

static int test_infer_with_linear_layers(void) {
    printf("[TEST] infer_with_linear_layers\n");

    /* Same chip as test 3: K=4, N=512, W[0][0]=1 */
    const char *wfile = "/tmp/test_linear_weights.bin";
    /* File already created by test 3; recreate to be safe */
    int8_t *W = calloc(512 * 4, 1);
    if (!W) return 1;
    W[0] = 1;
    FILE *f = fopen(wfile, "wb");
    if (!f) { free(W); return 1; }
    fwrite(W, 1, 512 * 4, f);
    fclose(f);
    free(W);

    /* .trix with one linear layer + one signature matching expected output */
    const char *tfile = "/tmp/test_infer_linear.trix";
    f = fopen(tfile, "w");
    fprintf(f, "softchip:\n  name: infer_linear\n  version: 1.0.0\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "linear:\n  embed:\n");
    fprintf(f, "    input_dim: 4\n    output_dim: 512\n    weights: %s\n", wfile);
    fprintf(f, "signatures:\n  match:\n    pattern: ");
    /* out[0]=0x80, out[1..63]=0x00 */
    fprintf(f, "80");
    for (int i = 1; i < 64; i++) fprintf(f, "00");
    fprintf(f, "\n    threshold: 0\n");
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
    fclose(f);

    int err = 0;
    trix_chip_t *chip = trix_load(tfile, &err);
    if (!chip) {
        fprintf(stderr, "FAIL trix_load error=%d\n", err);
        return 1;
    }

    uint8_t input[64] = {1, 0, 0, 0};
    trix_result_t result = trix_infer(chip, input);
    trix_chip_free(chip);

    if (result.match != 0) {
        fprintf(stderr, "FAIL match=%d want 0\n", result.match);
        return 1;
    }
    if (result.distance != 0) {
        fprintf(stderr, "FAIL distance=%d want 0\n", result.distance);
        return 1;
    }
    printf("  PASS\n");
    return 0;
}
```

Add `failures += test_infer_with_linear_layers();` to `main()`.

- [ ] **Step 2: Run test to verify it fails**

```bash
cd build && make test_linear_runtime && ./test_linear_runtime 2>&1 | grep -E "FAIL|PASS|infer_with"
```

Expected: FAIL — trix_infer doesn't call exec_linear yet.

- [ ] **Step 3: Add `#include` for linear_runtime.h in `runtime.c`**

After the existing includes (~line 16) in `zor/src/runtime.c`, add:

```c
#include "../include/trixc/linear_runtime.h"
```

- [ ] **Step 4: Modify `trix_infer()` in `runtime.c` to call `trix_exec_linear()`**

In `trix_infer()` (~line 297), insert the linear layer pre-processing step **after the `!chip || !input` NULL check but BEFORE the `chip->num_signatures <= 0` early return** — the `effective_input` pointer must be in scope for the matching loop that follows. Exact location: between lines 303 and 305 in the current file (verify with `grep -n "num_signatures" zor/src/runtime.c`).

```c
    /* Apply linear layers if present */
    uint8_t linear_out[64];
    const uint8_t *effective_input = input;

    if (chip->num_linear_layers > 0) {
        int lin_rc = trix_exec_linear(chip, input, linear_out);
        if (lin_rc != TRIX_OK) {
            result.label = "error";
            return result;
        }
        effective_input = linear_out;
    }
```

Then update the popcount call in the matching loop (line ~319):

```c
        int dist = popcount64(effective_input, chip->signatures[i]);
```

Do the same for `trix_infer_all()` — add this block before the `for` loop in that function:

```c
    /* Apply linear layers if present */
    uint8_t linear_out_all[64];
    const uint8_t *effective_input = input;

    if (chip->num_linear_layers > 0) {
        int lin_rc = trix_exec_linear(chip, input, linear_out_all);
        if (lin_rc != TRIX_OK) return 0;
        effective_input = linear_out_all;
    }
```

Then replace `input` with `effective_input` in the `popcount64` call inside the loop.

**⚠️ Two-part edit — both are required.** If you add the `effective_input` block but forget to update the `popcount64` call, the linear layer output is silently discarded and raw input bytes are matched against signatures, producing wrong results with no compile-time error.

- [ ] **Step 5: Run tests**

```bash
cd build && make test_linear_runtime test_runtime && ./test_linear_runtime && ./test_runtime
```

Expected: all tests pass.

- [ ] **Step 6: Run full test suite**

```bash
cd build && ctest --output-on-failure 2>&1 | tail -20
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```bash
git add zor/src/runtime.c zor/test/test_linear_runtime.c
git commit -m "feat: wire trix_exec_linear into trix_infer for linear-layer chips"
```

---

## Task 4: SDOT vs fp32 vs SMMLA throughput benchmark

**Files:**
- Create: `tools/test/bench_linear.c`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create `tools/test/bench_linear.c`**

```c
/*
 * bench_linear.c — SDOT / SMMLA / fp32 throughput benchmark
 *
 * Measures GOP/s for K=512, N=512 single-vector MatVec.
 * Tests portable, SDOT (ARM64), and SMMLA (ARMv8.6) paths.
 *
 * Build: add bench_linear to CMakeLists.txt (linked to trix_runtime_static)
 * Run:   ./bench_linear
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdint.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#define K 512
#define N 512
#define ITERS 1000

/* Ops per MatVec: 2 * N * K (multiply + accumulate) */
#define OPS_PER_ITER ((double)(2) * N * K)

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* ── fp32 reference ──────────────────────────────────────────────────────── */

static void matvec_fp32(int k, int n,
                         const float *x, const float *W, float *y) {
    for (int i = 0; i < n; i++) {
        float acc = 0.0f;
        const float *row = W + (size_t)i * k;
        for (int j = 0; j < k; j++) acc += x[j] * row[j];
        y[i] = acc;
    }
}

/* ── int8 portable ───────────────────────────────────────────────────────── */

static void matvec_int8_portable(int k, int n,
                                  const int8_t *x, const int8_t *W, int32_t *y) {
    for (int i = 0; i < n; i++) {
        int32_t acc = 0;
        const int8_t *row = W + (size_t)i * k;
        for (int j = 0; j < k; j++) acc += (int32_t)x[j] * (int32_t)row[j];
        y[i] = acc;
    }
}

/* ── SDOT (ARM64) ────────────────────────────────────────────────────────── */

#if defined(__ARM_FEATURE_DOTPROD)
static void matvec_sdot(int k, int n,
                         const int8_t *x, const int8_t *W, int32_t *y) {
    int k16 = k & ~15;
    for (int i = 0; i < n; i++) {
        const int8_t *row = W + (size_t)i * k;
        int32x4_t acc = vdupq_n_s32(0);
        for (int j = 0; j < k16; j += 16) {
            acc = vdotq_s32(acc, vld1q_s8(x + j), vld1q_s8(row + j));
        }
        int32_t s = vaddvq_s32(acc);
        for (int j = k16; j < k; j++) s += (int32_t)x[j] * (int32_t)row[j];
        y[i] = s;
    }
}
#endif

/* ── SMMLA (ARMv8.6-a) ───────────────────────────────────────────────────── */

#if defined(__ARM_FEATURE_MATMUL_INT8)
static void pack_i8mm(int k, int n, const int8_t *src, int8_t *dst) {
    int kb = k / 8, np = n / 2;
    for (int p = 0; p < np; p++)
        for (int b = 0; b < kb; b++) {
            int8_t *blk = dst + (p * kb + b) * 16;
            for (int i = 0; i < 8; i++) blk[i]     = src[(p*2)   * k + b*8 + i];
            for (int i = 0; i < 8; i++) blk[8+i]   = src[(p*2+1) * k + b*8 + i];
        }
}

static void matvec_smmla_single(int k, int n,
                                  const int8_t *x, const int8_t *W_packed, int32_t *y) {
    int kb = k / 8, np = n / 2;
    /* Second row of the 2×8 activation block is zero (single-vector inference) */
    static const int8_t zero[K] = {0};
    for (int p = 0; p < np; p++) {
        const int8_t *wp = W_packed + p * kb * 16;
        int32x4_t acc = vdupq_n_s32(0);
        for (int b = 0; b < kb; b++) {
            int8x16_t a = vcombine_s8(vld1_s8(x    + b*8), vld1_s8(zero + b*8));
            acc = vmmlaq_s32(acc, a, vld1q_s8(wp));
            wp += 16;
        }
        y[p*2]   = vgetq_lane_s32(acc, 0);
        y[p*2+1] = vgetq_lane_s32(acc, 1);
    }
}
#endif

/* ── Benchmark runner ────────────────────────────────────────────────────── */

typedef struct {
    const char *name;
    double gops;
    double speedup;
} BenchResult;

static double bench_fp32(void) {
    float *W = malloc(N * K * sizeof(float));
    float *x = malloc(K * sizeof(float));
    float *y = malloc(N * sizeof(float));
    for (int i = 0; i < N * K; i++) W[i] = (i % 3 == 0) ? 1.0f : (i % 3 == 1) ? -1.0f : 0.0f;
    for (int i = 0; i < K; i++) x[i] = (float)(i & 0x7f);
    double t0 = now_sec();
    for (int it = 0; it < ITERS; it++) matvec_fp32(K, N, x, W, y);
    double elapsed = now_sec() - t0;
    free(W); free(x); free(y);
    return (OPS_PER_ITER * ITERS) / elapsed / 1e9;
}

static double bench_portable(void) {
    int8_t *W = malloc(N * K);
    int8_t *x = malloc(K);
    int32_t *y = malloc(N * sizeof(int32_t));
    for (int i = 0; i < N * K; i++) W[i] = (i % 3 == 0) ? 1 : (i % 3 == 1) ? -1 : 0;
    for (int i = 0; i < K; i++) x[i] = (int8_t)(i & 0x7f);
    double t0 = now_sec();
    for (int it = 0; it < ITERS; it++) matvec_int8_portable(K, N, x, W, y);
    double elapsed = now_sec() - t0;
    free(W); free(x); free(y);
    return (OPS_PER_ITER * ITERS) / elapsed / 1e9;
}

#if defined(__ARM_FEATURE_DOTPROD)
static double bench_sdot(void) {
    int8_t *W = malloc(N * K);
    int8_t *x = malloc(K);
    int32_t *y = malloc(N * sizeof(int32_t));
    for (int i = 0; i < N * K; i++) W[i] = (i % 3 == 0) ? 1 : (i % 3 == 1) ? -1 : 0;
    for (int i = 0; i < K; i++) x[i] = (int8_t)(i & 0x7f);
    double t0 = now_sec();
    for (int it = 0; it < ITERS; it++) matvec_sdot(K, N, x, W, y);
    double elapsed = now_sec() - t0;
    free(W); free(x); free(y);
    return (OPS_PER_ITER * ITERS) / elapsed / 1e9;
}
#endif

#if defined(__ARM_FEATURE_MATMUL_INT8)
static double bench_smmla(void) {
    int8_t *W = malloc(N * K);
    int8_t *Wp = malloc((N/2) * (K/8) * 16);
    int8_t *x  = malloc(K);
    int32_t *y = malloc(N * sizeof(int32_t));
    for (int i = 0; i < N * K; i++) W[i] = (i % 3 == 0) ? 1 : (i % 3 == 1) ? -1 : 0;
    pack_i8mm(K, N, W, Wp);
    for (int i = 0; i < K; i++) x[i] = (int8_t)(i & 0x7f);
    double t0 = now_sec();
    for (int it = 0; it < ITERS; it++) matvec_smmla_single(K, N, x, Wp, y);
    double elapsed = now_sec() - t0;
    free(W); free(Wp); free(x); free(y);
    return (OPS_PER_ITER * ITERS) / elapsed / 1e9;
}
#endif

int main(void) {
    printf("TriX Linear Layer Throughput Benchmark\n");
    printf("Config: K=%d, N=%d, %d iterations\n\n", K, N, ITERS);
    printf("%-20s %10s %10s\n", "Backend", "GOP/s", "vs fp32");
    printf("%-20s %10s %10s\n", "-------", "-----", "-------");

    double fp32_gops = bench_fp32();
    printf("%-20s %10.1f %10s\n", "fp32", fp32_gops, "1.0×");

    double portable_gops = bench_portable();
    printf("%-20s %10.1f %10.1f×\n", "int8-portable", portable_gops, portable_gops / fp32_gops);

#if defined(__ARM_FEATURE_DOTPROD)
    double sdot_gops = bench_sdot();
    printf("%-20s %10.1f %10.1f×\n", "int8-sdot", sdot_gops, sdot_gops / fp32_gops);
#else
    printf("%-20s %10s %10s\n", "int8-sdot", "N/A", "N/A");
#endif

#if defined(__ARM_FEATURE_MATMUL_INT8)
    double smmla_gops = bench_smmla();
    printf("%-20s %10.1f %10.1f×\n", "int8-smmla", smmla_gops, smmla_gops / fp32_gops);
#else
    printf("%-20s %10s %10s\n", "int8-smmla", "N/A", "N/A");
#endif

    printf("\nNote: K=N=512, ternary weights {-1,0,+1}, batch=1 single-vector inference\n");
    return 0;
}
```

- [ ] **Step 2: Add `bench_linear` to `CMakeLists.txt`**

Inside `if(TRIX_BUILD_TOOLS)` block (after the `trix` executable), add:

```cmake
    # Throughput benchmark
    add_executable(bench_linear tools/test/bench_linear.c)
    target_link_libraries(bench_linear PRIVATE TriX::Runtime)
    if(UNIX)
        target_link_libraries(bench_linear PRIVATE m)
    endif()
```

- [ ] **Step 3: Build and run**

```bash
cd build && cmake .. && make bench_linear && ./bench_linear
```

Expected output (Apple M4, ARM64 with SDOT + SMMLA):
```
TriX Linear Layer Throughput Benchmark
Config: K=512, N=512, 1000 iterations

Backend              GOP/s     vs fp32
-------              -----     -------
fp32                  xx.x        1.0×
int8-portable         xx.x        x.x×
int8-sdot             xx.x        x.x×
int8-smmla            xx.x        x.x×
```

Record the actual numbers. The SMMLA / fp32 speedup ratio validates (or corrects) the ~235 GOP/s claim.

- [ ] **Step 4: Commit with benchmark results in commit message**

```bash
git add tools/test/bench_linear.c CMakeLists.txt
git commit -m "feat: add SDOT/SMMLA/fp32 throughput benchmark (bench_linear)"
```

---

## Task 5: ASAN verification

**Files:** no new files — build configuration only.

- [ ] **Step 1: Build with AddressSanitizer**

```bash
cd build && cmake .. -DTRIX_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug && make -j4
```

- [ ] **Step 2: Run full test suite under ASAN**

```bash
ctest --output-on-failure 2>&1 | tee /tmp/asan_results.txt
tail -30 /tmp/asan_results.txt
```

Expected: no ASAN errors, all tests pass.

- [ ] **Step 3: Fix any ASAN findings**

Common issues to look for:
- Heap buffer overflow in `matvec_sdot` if K is not a multiple of 16 and loop bounds are wrong
- Use-after-free in `cur_input` in `trix_exec_linear` (check the `free(cur_input)` / `cur_input = next_input` logic)
- Uninitialized reads in the weight loading loop (calloc vs malloc)

- [ ] **Step 4: Run final clean build and test**

```bash
cmake .. -DTRIX_ENABLE_ASAN=OFF -DCMAKE_BUILD_TYPE=Release && make -j4
ctest --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 5: Commit any ASAN fixes**

```bash
git add -A
git commit -m "fix: ASAN-clean linear runtime (address any findings from sanitizer run)"
```

- [ ] **Step 6: Push**

```bash
git push origin main
```

---

## Quick Reference

| Symbol | Location | Purpose |
|--------|----------|---------|
| `struct trix_chip` | `chip_private.h` | Internal struct (opaque in public API) |
| `CHIP_MAX_LINEAR_LAYERS` | `chip_private.h` | = 16, replaces `MAX_LINEAR_LAYERS` from softchip.h |
| `linear_matvec_portable()` | `linear_runtime.c` | Exposed for tests |
| `linear_sign_binarize()` | `linear_runtime.c` | Exposed for tests |
| `trix_exec_linear()` | `linear_runtime.h` | Public — called by trix_infer() |
| `pack_i8mm()` | `linear_runtime.c` | Internal — packs weights for SMMLA |
| `bench_linear` | `tools/test/bench_linear.c` | Stand-alone benchmark binary |

## Known Constraints

- First layer `input_dim` must be ≤ 64 (chip input is always 64 bytes)
- Last layer `output_dim` must equal `state_bits` (validated at load time)
- SMMLA path requires N % 2 == 0 and K % 8 == 0 (validated at load time or runtime fallback)
- SDOT path handles K not divisible by 16 via portable remainder
- benchmark is batch=1 (single-vector); SMMLA underperforms vs batch≥2 in this mode
