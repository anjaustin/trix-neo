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
    fprintf(f, "inference:\n  mode: first_match\n  default: unknown\n");
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

    if (!(r.match == 0)) {
        fprintf(stderr, "  FAIL: expected sig0 to match\n");
        teardown(sys, chip);
        return 1;
    }
    if (!(r.distance == 0)) {
        fprintf(stderr, "  FAIL: expected zero distance for identical input\n");
        teardown(sys, chip);
        return 1;
    }
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

    if (!(r.match == -1)) {
        fprintf(stderr, "  FAIL: expected no match for max-distance input\n");
        teardown(sys, chip);
        return 1;
    }
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

    if (!(mismatches == 0)) {
        fprintf(stderr, "  FAIL: hsos_exec_infer must agree with trix_infer\n");
        teardown(sys, chip);
        return 1;
    }
    printf("  ✓ all 10 inputs agree\n");

    teardown(sys, chip);
    return 0;
}

/* ── test 4: inference is repeatable across consecutive calls ─────────────── */

static int test_repeatability(void) {
    printf("[TEST] repeatability\n");

    trix_chip_t *chip = make_chip(4);
    hsos_system_t *sys = make_system();

    uint8_t input[64];
    memset(input, 0x02, 64);

    /* Run inference twice on the same system — must produce identical results */
    trix_result_t r1 = hsos_exec_infer(sys, chip, input);
    trix_result_t r2 = hsos_exec_infer(sys, chip, input);

    if (r1.match != r2.match || r1.distance != r2.distance) {
        fprintf(stderr, "  FAIL: inference not repeatable: (%d,%d) vs (%d,%d)\n",
                r1.match, r1.distance, r2.match, r2.distance);
        teardown(sys, chip);
        return 1;
    }
    printf("  ✓ r1=(%d,%d) r2=(%d,%d) — repeatable\n",
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

    if (!(r.trace_tick_end >= r.trace_tick_start)) {
        fprintf(stderr, "  FAIL: tick_end must be >= tick_start\n");
        teardown(sys, chip);
        return 1;
    }
    if (!(r.trace_tick_end > 0)) {
        fprintf(stderr, "  FAIL: ticks must have advanced\n");
        teardown(sys, chip);
        return 1;
    }
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

    if (r.trace_tick_start != 0 || r.trace_tick_end != 0) {
        fprintf(stderr, "  FAIL: trix_infer() must leave trace ticks zero (N/A)\n");
        trix_chip_free(chip);
        return 1;
    }
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
    failures += test_repeatability();
    failures += test_trace_ticks();
    failures += test_trix_infer_zero_ticks();

    printf("\n%s — %d failure(s)\n",
           failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
