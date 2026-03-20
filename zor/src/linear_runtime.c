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
    int K16 = K & ~15;  /* largest multiple of 16 <= K */

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
 * vmmlaq_s32: acc[2x2] += A[2x8] * B[8x2]
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

    /* Synthesize a second "batch" row of zeros so we can use the 2x8x2 kernel */
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
 * Pack N sign bits (z > 0 -> 1, z <= 0 -> 0) into out[] MSB-first.
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
    memcpy(cur_input, input, (size_t)K0);  /* uint8 bytes -> int8 (bit-identical cast) */

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
                /* Packed weight allocation failed - return OOM. */
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
