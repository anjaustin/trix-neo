/*
 * bench_linear.c — SDOT / SMMLA / fp32 throughput benchmark
 *
 * Measures GOP/s for K=512, N=512 single-vector MatVec.
 * Tests portable, SDOT (ARM64), and SMMLA (ARMv8.6) paths.
 *
 * Build: cmake target bench_linear (linked to TriX::Runtime)
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
            for (int i = 0; i < 8; i++) blk[i]   = src[(p*2)   * k + b*8 + i];
            for (int i = 0; i < 8; i++) blk[8+i] = src[(p*2+1) * k + b*8 + i];
        }
}

static void matvec_smmla_single(int k, int n,
                                  const int8_t *x, const int8_t *W_packed, int32_t *y) {
    int kb = k / 8, np = n / 2;
    /* Second row of the 2x8 activation block is zero (single-vector inference) */
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

/* ── Benchmark runners ───────────────────────────────────────────────────── */

static double bench_fp32(void) {
    float *W = malloc(N * K * sizeof(float));
    float *x = malloc(K * sizeof(float));
    float *y = malloc(N * sizeof(float));
    for (int i = 0; i < N * K; i++) W[i] = (i % 3 == 0) ? 1.0f : (i % 3 == 1) ? -1.0f : 0.0f;
    for (int i = 0; i < K; i++) x[i] = (float)(i & 0x7f);
    double t0 = now_sec();
    for (int it = 0; it < ITERS; it++) matvec_fp32(K, N, x, W, y);
    double elapsed = now_sec() - t0;
    /* Anti-DCE sink: force compiler to observe y[] writes */
    volatile float fsink = 0.0f;
    for (int i = 0; i < N; i++) fsink += y[i];
    (void)fsink;
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
    /* Anti-DCE sink: force compiler to observe y[] writes */
    volatile int32_t sink = 0;
    for (int i = 0; i < N; i++) sink ^= y[i];
    (void)sink;
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
    /* Anti-DCE sink: force compiler to observe y[] writes */
    volatile int32_t sink = 0;
    for (int i = 0; i < N; i++) sink ^= y[i];
    (void)sink;
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
    /* Anti-DCE sink: force compiler to observe y[] writes */
    volatile int32_t sink = 0;
    for (int i = 0; i < N; i++) sink ^= y[i];
    (void)sink;
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
    printf("%-20s %10.1f %10s\n", "fp32", fp32_gops, "1.0x");

    double portable_gops = bench_portable();
    printf("%-20s %10.1f %10.1fx\n", "int8-portable", portable_gops, portable_gops / fp32_gops);

#if defined(__ARM_FEATURE_DOTPROD)
    double sdot_gops = bench_sdot();
    printf("%-20s %10.1f %10.1fx\n", "int8-sdot", sdot_gops, sdot_gops / fp32_gops);
#else
    printf("%-20s %10s %10s\n", "int8-sdot", "N/A", "N/A");
#endif

#if defined(__ARM_FEATURE_MATMUL_INT8)
    double smmla_gops = bench_smmla();
    printf("%-20s %10.1f %10.1fx\n", "int8-smmla", smmla_gops, smmla_gops / fp32_gops);
#else
    printf("%-20s %10s %10s\n", "int8-smmla", "N/A", "N/A");
#endif

    printf("\nNote: K=N=512, ternary weights {-1,0,+1}, batch=1 single-vector inference\n");
    return 0;
}
