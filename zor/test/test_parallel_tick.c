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

    printf("  sorted: ");
    for (int i = 0; i < 8; i++) printf("%u ", values_a[i]);
    printf("\n");
    return 0;
}

/* ── test 2: Inference results identical across 20 independent runs ───────── */

static int test_inference_determinism(void) {
    printf("[TEST] inference_determinism (20 runs x 8 inputs x 16 sigs)\n");

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
    printf("  all 20x8 results identical\n");
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
            printf("  FAIL input[%d]: direct=(%d,%d) parallel=(%d,%d)\n",
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
    printf("  all 10 inputs agree between direct and parallel paths\n");
    return 0;
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(void) {
    printf("======================================\n");
    printf("  Parallel HSOS Tick Tests\n");
    printf("======================================\n\n");

    int failures = 0;
    failures += test_bubble_determinism();
    failures += test_inference_determinism();
    failures += test_parallel_agrees_with_direct();

    printf("\n%s -- %d failure(s)\n",
           failures == 0 ? "PASS" : "FAIL", failures);
    return failures;
}
