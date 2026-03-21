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
#include "hsos.h"
#include "trixc/hsos_infer.h"
#include "trixc/runtime.h"

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

    /* -- Direct (single-thread trix_infer) -- */
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

    /* -- HSOS parallel -- */
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

    printf("\n  Ratio (direct/parallel):  %.2fx\n",
           ms_direct / ms_parallel);

    hsos_system_free(sys);
    free(sys);
    trix_chip_free(chip);
    return 0;
}
