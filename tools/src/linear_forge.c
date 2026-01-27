/*
 * linear_forge.c — TriX Linear Kingdom Circuit Stamper
 *
 * "The SDOT instruction IS the Prime."
 *
 * This file implements the circuit stamping logic for Linear Kingdom shapes.
 * Instead of generating loops, we generate the unrolled instruction stream
 * that an ASIC would execute.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * ARCHITECTURE OVERVIEW
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * The forge generates code for int8 matrix-vector multiplication (matvec):
 *
 *     y[N] = W[N,K] × x[K]     where W is ternary {-1, 0, +1}
 *
 * BACKENDS:
 *
 *   1. C_PORTABLE    - Reference implementation with loops
 *                      Platform: Any
 *                      Performance: ~10 GOP/s
 *
 *   2. NEON_BLOCK_16 - 16 output channels per iteration
 *                      Uses SDOT (4x int8 dot product -> int32)
 *                      Platform: ARM64 with NEON
 *                      Performance: ~189 GOP/s on M4
 *
 *   3. BLOCK8_K64    - 8 output channels × 64 K-elements per iteration
 *                      Larger K-unroll = better prefetch efficiency
 *                      Platform: ARM64 with NEON
 *                      Performance: ~178 GOP/s on M4
 *
 *   4. GHOST_12      - 12 output channels with LDNP (non-temporal loads)
 *                      Experimental streaming variant
 *                      Platform: ARM64 with NEON
 *                      Performance: Varies
 *
 *   5. I8MM_BATCH    - Uses SMMLA instruction (2x8x2 matrix blocks)
 *                      Batched inference (batch size must be multiple of 2)
 *                      Platform: ARMv8.6-a with I8MM extension
 *                      Performance: ~235 GOP/s on M4 (batch=4)
 *
 * WEIGHT PACKING:
 *
 *   Block-16 Layout:
 *     ┌─────────────────────┐
 *     │ W[0,0..15]  (row 0) │  16 weights per row
 *     │ W[1,0..15]  (row 1) │
 *     │ ...                 │
 *     │ W[15,0..15] (row 15)│  16 rows per block
 *     └─────────────────────┘
 *     Block size: 16 × 16 = 256 bytes
 *
 *   Block-8-K64 Layout:
 *     ┌──────────────────────────────────────────┐
 *     │ W[0,0..63]   (row 0, 64 weights)         │
 *     │ W[1,0..63]   (row 1)                     │
 *     │ ...                                      │
 *     │ W[7,0..63]   (row 7)                     │
 *     └──────────────────────────────────────────┘
 *     Block size: 8 × 64 = 512 bytes
 *
 * KEY INSTRUCTIONS:
 *
 *   SDOT (vdotq_s32):
 *     - 4 int8 × 4 int8 → int32, accumulated
 *     - 16 operations per instruction
 *     - The "Prime" of the Linear Kingdom
 *
 *   SMMLA (vsmmla_s32):
 *     - 2×8 int8 × 8×2 int8 → 2×2 int32
 *     - 32 operations per instruction
 *     - Requires ARMv8.6-a I8MM extension
 *
 * PERFORMANCE MODEL:
 *
 *   Throughput = (N × K × 2) / time
 *   where 2 = operations per MAC (multiply + accumulate)
 *
 *   M4 base chip achieves:
 *     - Memory bandwidth: ~100 GB/s
 *     - SDOT throughput: ~200 GOP/s (compute bound)
 *     - Achieved: 178-235 GOP/s (depends on strategy)
 *
 * ═══════════════════════════════════════════════════════════════════════════
 *
 * Copyright 2026 Trix Research
 */

#include "../include/linear_forge.h"
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include <stdio.h>

/* ═══════════════════════════════════════════════════════════════════════════
 * INTERNAL HELPERS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* String buffer for building output */
typedef struct {
    char* buf;
    size_t size;
    size_t pos;
} StringBuffer;

static void sb_init(StringBuffer* sb, char* buf, size_t size) {
    sb->buf = buf;
    sb->size = size;
    sb->pos = 0;
}

static void sb_append(StringBuffer* sb, const char* str) {
    size_t len = strlen(str);
    if (sb->pos + len < sb->size) {
        memcpy(sb->buf + sb->pos, str, len);
        sb->pos += len;
        sb->buf[sb->pos] = '\0';
    }
}

static void sb_printf(StringBuffer* sb, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(sb->buf + sb->pos, sb->size - sb->pos, fmt, args);
    va_end(args);
    if (written > 0 && (size_t)written < sb->size - sb->pos) {
        sb->pos += written;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PORTABLE C BACKEND
 *
 * Reference implementation. Loops, no SIMD. For verification.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_c_portable(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    
    sb_printf(sb, "/* TriX Forged Soft-Chip: %s (C Portable) */\n", name);
    sb_printf(sb, "/* Topology: K=%d, N=%d */\n\n", K, N);
    
    /* Forward declaration */
    sb_printf(sb, "void %s_forward(const float* x, float* y) {\n", name);
    
    /* The loop structure */
    sb_printf(sb, "    for (int n = 0; n < %d; n++) {\n", N);
    
    /* Initialize accumulator with bias if present */
    if (spec->bias) {
        sb_printf(sb, "        float acc = %s_bias[n];\n", name);
    } else {
        sb_printf(sb, "        float acc = 0.0f;\n");
    }
    
    /* Inner loop — the "Prime Match" in scalar form */
    sb_printf(sb, "        for (int k = 0; k < %d; k++) {\n", K);
    sb_printf(sb, "            acc += %s_W[n * %d + k] * x[k];\n", name, K);
    sb_printf(sb, "        }\n");
    
    /* Activation */
    switch (spec->activation) {
        case ACT_NONE:
            sb_printf(sb, "        y[n] = acc;\n");
            break;
        case ACT_RELU:
            sb_printf(sb, "        y[n] = acc > 0.0f ? acc : 0.0f;\n");
            break;
        case ACT_SIGMOID:
            sb_printf(sb, "        y[n] = 1.0f / (1.0f + expf(-acc));\n");
            break;
        case ACT_TANH:
            sb_printf(sb, "        y[n] = tanhf(acc);\n");
            break;
        case ACT_GELU:
            sb_printf(sb, "        y[n] = 0.5f * acc * (1.0f + tanhf(0.7978845608f * (acc + 0.044715f * acc * acc * acc)));\n");
            break;
        case ACT_SOFTMAX:
            sb_printf(sb, "        y[n] = acc; /* softmax applied post-loop */\n");
            break;
    }
    
    sb_printf(sb, "    }\n");
    
    /* Softmax needs a second pass */
    if (spec->activation == ACT_SOFTMAX) {
        sb_printf(sb, "\n    /* Softmax normalization */\n");
        sb_printf(sb, "    float max_val = y[0];\n");
        sb_printf(sb, "    for (int n = 1; n < %d; n++) max_val = y[n] > max_val ? y[n] : max_val;\n", N);
        sb_printf(sb, "    float sum = 0.0f;\n");
        sb_printf(sb, "    for (int n = 0; n < %d; n++) { y[n] = expf(y[n] - max_val); sum += y[n]; }\n", N);
        sb_printf(sb, "    for (int n = 0; n < %d; n++) y[n] /= sum;\n", N);
    }
    
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NEON BLOCK-16 BACKEND
 *
 * The proven 185 GOP/s kernel, parameterized.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_neon_block16(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    int K_blocks = K / 16;
    
    sb_printf(sb, "/* TriX Forged Soft-Chip: %s (NEON Block-16) */\n", name);
    sb_printf(sb, "/* Topology: K=%d, N=%d, Strategy=BLOCK_16 */\n", K, N);
    sb_printf(sb, "/* Expected: ~185 GOP/s on M4 */\n\n");
    
    sb_printf(sb, "#include <arm_neon.h>\n\n");
    
    /* Function signature */
    sb_printf(sb, "void %s_forward_neon(const int8_t* x, int32_t* y) {\n", name);
    sb_printf(sb, "    const int K_blocks = %d;\n", K_blocks);
    sb_printf(sb, "    const int block_stride = 16 * 16;\n\n");
    
    /* Outer loop over output blocks of 16 */
    sb_printf(sb, "    for (int n = 0; n < %d; n += 16) {\n", N);
    sb_printf(sb, "        int nb = n / 16;\n\n");
    
    /* Reset accumulators — THE CIRCUIT RESET */
    sb_printf(sb, "        /* --- RESET ACCUMULATORS (The Field Clears) --- */\n");
    for (int i = 0; i < 16; i++) {
        sb_printf(sb, "        int32x4_t acc%d = vdupq_n_s32(0);\n", i);
    }
    
    /* Setup pointers */
    sb_printf(sb, "\n        const int8_t* a_ptr = x;\n");
    sb_printf(sb, "        const int8_t* w_base = %s_W + nb * K_blocks * block_stride;\n\n", name);
    
    /* Inner loop — THE SPATIAL FOLD */
    sb_printf(sb, "        /* --- SPATIAL FOLD (K dimension) --- */\n");
    sb_printf(sb, "        for (int kb = 0; kb < K_blocks; kb++) {\n");
    sb_printf(sb, "            const int8_t* w_ptr = w_base + kb * block_stride;\n\n");
    
    /* Load activation vector */
    sb_printf(sb, "            /* Load activation pattern */\n");
    sb_printf(sb, "            int8x16_t a = vld1q_s8(a_ptr);\n");
    sb_printf(sb, "            a_ptr += 16;\n\n");
    
    /* Load 16 weight vectors — THE PATTERN TEMPLATES */
    sb_printf(sb, "            /* Load 16 weight patterns (The Templates) */\n");
    for (int i = 0; i < 16; i++) {
        sb_printf(sb, "            int8x16_t w%d = vld1q_s8(w_ptr + %d);\n", i, i * 16);
    }
    sb_printf(sb, "\n");
    
    /* SDOT all 16 — THE PRIME MATCHES */
    sb_printf(sb, "            /* --- PRIME MATCH (SDOT = Pattern Recognition) --- */\n");
    for (int i = 0; i < 16; i++) {
        sb_printf(sb, "            acc%d = vdotq_s32(acc%d, w%d, a);\n", i, i, i);
    }
    
    sb_printf(sb, "        }\n\n");
    
    /* Reduce and store — THE CIRCUIT OUTPUT */
    sb_printf(sb, "        /* --- REDUCE & STORE (Circuit Output) --- */\n");
    for (int i = 0; i < 16; i++) {
        sb_printf(sb, "        y[n + %d] = vaddvq_s32(acc%d);\n", i, i);
    }
    
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NEON BLOCK-8-K64 BACKEND
 *
 * The fastest kernel on M4 base chip (~170 GOP/s).
 * 8 output channels, 64 K-elements per iteration.
 * Larger K-unroll = less loop overhead, better prefetch efficiency.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_neon_block8_k64(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    int K_blocks = K / 64;
    
    sb_printf(sb, "/* TriX Forged Soft-Chip: %s (NEON Block-8-K64) */\n", name);
    sb_printf(sb, "/* Topology: K=%d, N=%d, Strategy=BLOCK8_K64 */\n", K, N);
    sb_printf(sb, "/* Expected: ~170 GOP/s on M4 base */\n\n");
    
    sb_printf(sb, "#include <arm_neon.h>\n\n");
    
    /* Function signature */
    sb_printf(sb, "void %s_forward_neon(const int8_t* x, int32_t* y) {\n", name);
    sb_printf(sb, "    const int K_blocks = %d;\n", K_blocks);
    sb_printf(sb, "    const int block_stride = 8 * 64;  /* 512 bytes per block */\n\n");
    
    /* Outer loop over output blocks of 8 */
    sb_printf(sb, "    for (int n = 0; n < %d; n += 8) {\n", N);
    sb_printf(sb, "        int nb = n / 8;\n\n");
    
    /* Reset accumulators — THE CIRCUIT RESET */
    sb_printf(sb, "        /* --- RESET ACCUMULATORS (8 channels) --- */\n");
    for (int i = 0; i < 8; i++) {
        sb_printf(sb, "        int32x4_t acc%d = vdupq_n_s32(0);\n", i);
    }
    
    /* Setup pointers */
    sb_printf(sb, "\n        const int8_t* a_ptr = x;\n");
    sb_printf(sb, "        const int8_t* w_base = %s_W + nb * K_blocks * block_stride;\n\n", name);
    
    /* Inner loop — THE SPATIAL FOLD */
    sb_printf(sb, "        /* --- SPATIAL FOLD (K dimension, 64 steps per iteration) --- */\n");
    sb_printf(sb, "        for (int kb = 0; kb < K_blocks; kb++) {\n");
    
    /* Prefetch */
    sb_printf(sb, "            /* Prefetch 2 blocks ahead */\n");
    sb_printf(sb, "            if (kb + 2 < K_blocks) {\n");
    sb_printf(sb, "                __builtin_prefetch(a_ptr + 128, 0, 3);\n");
    sb_printf(sb, "                __builtin_prefetch(w_base + (kb + 2) * block_stride, 0, 3);\n");
    sb_printf(sb, "                __builtin_prefetch(w_base + (kb + 2) * block_stride + 256, 0, 3);\n");
    sb_printf(sb, "            }\n\n");
    
    /* Load 64 activations */
    sb_printf(sb, "            /* Load 64 activation elements (4 x 16) */\n");
    for (int i = 0; i < 4; i++) {
        sb_printf(sb, "            int8x16_t a%d = vld1q_s8(a_ptr + %d);\n", i, i * 16);
    }
    sb_printf(sb, "            a_ptr += 64;\n\n");
    
    sb_printf(sb, "            const int8_t* w_block = w_base + kb * block_stride;\n\n");
    
    /* Process all 8 rows — THE PRIME MATCHES */
    sb_printf(sb, "            /* --- PRIME MATCH (8 rows x 4 SDOT each) --- */\n");
    for (int row = 0; row < 8; row++) {
        sb_printf(sb, "            /* Row %d */\n", row);
        for (int i = 0; i < 4; i++) {
            sb_printf(sb, "            {\n");
            sb_printf(sb, "                int8x16_t w = vld1q_s8(w_block + %d * 64 + %d);\n", row, i * 16);
            sb_printf(sb, "                acc%d = vdotq_s32(acc%d, w, a%d);\n", row, row, i);
            sb_printf(sb, "            }\n");
        }
    }
    
    sb_printf(sb, "        }\n\n");
    
    /* Reduce and store — THE CIRCUIT OUTPUT */
    sb_printf(sb, "        /* --- REDUCE & STORE (Circuit Output) --- */\n");
    for (int i = 0; i < 8; i++) {
        sb_printf(sb, "        y[n + %d] = vaddvq_s32(acc%d);\n", i, i);
    }
    
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * NEON GHOST-12 BACKEND
 *
 * Uses LDNP for non-temporal loads. 12 output channels.
 * Experimental — may be slower than Block-8-K64 due to Apple's smart prefetchers.
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_neon_ghost12(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    
    sb_printf(sb, "/* TriX Forged Soft-Chip: %s (NEON Ghost-12) */\n", name);
    sb_printf(sb, "/* Topology: K=%d, N=%d, Strategy=GHOST_STREAM_12 */\n", K, N);
    sb_printf(sb, "/* Uses LDNP for non-temporal weight loads */\n\n");
    
    sb_printf(sb, "#include <arm_neon.h>\n\n");
    
    sb_printf(sb, "void %s_forward_ghost(const int8_t* x, int32_t* y) {\n", name);
    
    /* Outer loop over output blocks of 12 */
    sb_printf(sb, "    for (int n = 0; n < %d; n += 12) {\n", N);
    
    /* Emit inline assembly for Ghost-Stream pattern */
    sb_printf(sb, "        __asm__ volatile (\n");
    
    /* Reset 12 accumulators */
    sb_printf(sb, "            /* --- RESET ACCUMULATORS --- */\\n\"\n");
    for (int i = 0; i < 12; i++) {
        sb_printf(sb, "            \"movi v%d.4s, #0 \\n\"\n", i);
    }
    
    /* K loop setup */
    sb_printf(sb, "            \"mov x10, #%d \\n\" /* K counter */\n", K);
    sb_printf(sb, "            \"1: \\n\" /* Loop label */\n");
    
    /* Load 4 activation streams (LD4 deinterleaves) */
    sb_printf(sb, "            \"ld4 {v12.16b, v13.16b, v14.16b, v15.16b}, [%%[a]], #64 \\n\"\n");
    
    /* For each of 4 streams, process 12 outputs in pairs */
    for (int stream = 0; stream < 4; stream++) {
        sb_printf(sb, "            /* Stream %d */\\n\"\n", stream);
        for (int pair = 0; pair < 6; pair++) {
            int acc_a = pair * 2;
            int acc_b = pair * 2 + 1;
            int w_reg = 16 + pair * 2;
            
            /* Ghost load (LDNP) */
            sb_printf(sb, "            \"ldnp q%d, q%d, [%%[w]], #32 \\n\"\n", w_reg, w_reg + 1);
            
            /* Prime matches (SDOT) */
            sb_printf(sb, "            \"sdot v%d.4s, v%d.16b, v%d.16b \\n\"\n", 
                     acc_a, 12 + stream, w_reg);
            sb_printf(sb, "            \"sdot v%d.4s, v%d.16b, v%d.16b \\n\"\n", 
                     acc_b, 12 + stream, w_reg + 1);
        }
    }
    
    /* Loop maintenance */
    sb_printf(sb, "            \"subs x10, x10, #64 \\n\"\n");
    sb_printf(sb, "            \"b.gt 1b \\n\"\n");
    
    /* Store results */
    sb_printf(sb, "            /* --- STORE --- */\\n\"\n");
    for (int i = 0; i < 12; i += 4) {
        sb_printf(sb, "            \"st1 {v%d.4s, v%d.4s, v%d.4s, v%d.4s}, [%%[d]], #64 \\n\"\n",
                 i, i+1, i+2, i+3);
    }
    
    /* ASM constraints */
    sb_printf(sb, "            : [a] \"+r\" (x), [w] \"+r\" (%s_W), [d] \"+r\" (y)\n", name);
    sb_printf(sb, "            :\n");
    sb_printf(sb, "            : \"x10\", ");
    for (int i = 0; i < 28; i++) {
        sb_printf(sb, "\"v%d\"%s", i, i < 27 ? ", " : "");
    }
    sb_printf(sb, ", \"memory\"\n");
    sb_printf(sb, "        );\n");
    
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * I8MM BATCH BACKEND (SMMLA)
 *
 * Uses ARM I8MM extension for batched inference.
 * SMMLA processes 2x8x2 blocks: C[2x2] += A[2x8] * B[8x2]
 *
 * Key insight: For batch>1, we can amortize weight loads across multiple
 * input vectors. SMMLA gives us 2 outputs per instruction (vs 1 for SDOT).
 *
 * OPTIMIZED LAYOUT (I8MM packed):
 *   Weights pre-packed as [N/2, K/8, 16] for direct vector loads
 *   Each 16-byte block contains 2 output columns × 8 K elements
 *
 * Performance target: ~400 GOP/s for batch=4+ on M4
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_i8mm_batch(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    int K_blocks = K / 8;
    int N_pairs = N / 2;
    
    sb_printf(sb, "/* TriX Forged Soft-Chip: %s (I8MM Batch Optimized) */\n", name);
    sb_printf(sb, "/* Topology: K=%d, N=%d, Strategy=I8MM_BATCH */\n", K, N);
    sb_printf(sb, "/* SMMLA: C[2x2] += A[2x8] * B[8x2] per instruction */\n");
    sb_printf(sb, "/* Weights packed as [N/2, K/8, 16] for vector loads */\n");
    sb_printf(sb, "/* Expected: ~300 GOP/s for batch>=4 on M4 */\n\n");
    
    sb_printf(sb, "#include <arm_neon.h>\n\n");
    
    /* Emit weight packing helper */
    sb_printf(sb, "/*\n");
    sb_printf(sb, " * Pack row-major weights [N, K] to I8MM layout [N/2, K/8, 16]\n");
    sb_printf(sb, " * Each 16-byte block: [W[n][k:k+8], W[n+1][k:k+8]]\n");
    sb_printf(sb, " */\n");
    sb_printf(sb, "static void %s_pack_i8mm(\n", name);
    sb_printf(sb, "    const int8_t* src,\n");
    sb_printf(sb, "    int8_t* dst\n");
    sb_printf(sb, ") {\n");
    sb_printf(sb, "    for (int np = 0; np < %d; np++) {\n", N_pairs);
    sb_printf(sb, "        for (int kb = 0; kb < %d; kb++) {\n", K_blocks);
    sb_printf(sb, "            int8_t* block = dst + (np * %d + kb) * 16;\n", K_blocks);
    sb_printf(sb, "            /* Copy 8 elements from row n */\n");
    sb_printf(sb, "            for (int i = 0; i < 8; i++) {\n");
    sb_printf(sb, "                block[i] = src[(np * 2) * %d + kb * 8 + i];\n", K);
    sb_printf(sb, "            }\n");
    sb_printf(sb, "            /* Copy 8 elements from row n+1 */\n");
    sb_printf(sb, "            for (int i = 0; i < 8; i++) {\n");
    sb_printf(sb, "                block[8 + i] = src[(np * 2 + 1) * %d + kb * 8 + i];\n", K);
    sb_printf(sb, "            }\n");
    sb_printf(sb, "        }\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
    
    /* 
     * Main batch forward function with packed weights
     */
    sb_printf(sb, "/*\n");
    sb_printf(sb, " * Batched MatMul using I8MM (SMMLA) with packed weights\n");
    sb_printf(sb, " *\n");
    sb_printf(sb, " * x: input matrix [batch_size, %d] row-major\n", K);
    sb_printf(sb, " * W_packed: weights packed via %s_pack_i8mm()\n", name);
    sb_printf(sb, " * y: output matrix [batch_size, %d] row-major\n", N);
    sb_printf(sb, " * batch_size: must be >= 2 and even\n");
    sb_printf(sb, " */\n");
    sb_printf(sb, "void %s_forward_batch_packed(\n", name);
    sb_printf(sb, "    const int8_t* x,\n");
    sb_printf(sb, "    const int8_t* W_packed,\n");
    sb_printf(sb, "    int32_t* y,\n");
    sb_printf(sb, "    int batch_size\n");
    sb_printf(sb, ") {\n");
    sb_printf(sb, "    const int K_blocks = %d;\n", K_blocks);
    sb_printf(sb, "    const int N_pairs = %d;\n\n", N_pairs);
    
    sb_printf(sb, "    /* Process 2 batch rows at a time */\n");
    sb_printf(sb, "    for (int m = 0; m < batch_size; m += 2) {\n");
    sb_printf(sb, "        const int8_t* x_row0 = x + m * %d;\n", K);
    sb_printf(sb, "        const int8_t* x_row1 = x + (m + 1) * %d;\n", K);
    sb_printf(sb, "        int32_t* y_row0 = y + m * %d;\n", N);
    sb_printf(sb, "        int32_t* y_row1 = y + (m + 1) * %d;\n\n", N);
    
    sb_printf(sb, "        /* Process 2 output channels at a time */\n");
    sb_printf(sb, "        for (int np = 0; np < N_pairs; np++) {\n");
    sb_printf(sb, "            int32x4_t acc = vdupq_n_s32(0);\n\n");
    
    sb_printf(sb, "            const int8_t* w_ptr = W_packed + np * K_blocks * 16;\n\n");
    
    sb_printf(sb, "            for (int kb = 0; kb < K_blocks; kb++) {\n");
    sb_printf(sb, "                /* Load 2x8 activation block */\n");
    sb_printf(sb, "                int8x16_t a_block = vcombine_s8(\n");
    sb_printf(sb, "                    vld1_s8(x_row0 + kb * 8),\n");
    sb_printf(sb, "                    vld1_s8(x_row1 + kb * 8)\n");
    sb_printf(sb, "                );\n\n");
    
    sb_printf(sb, "                /* Load packed weight block (already in SMMLA format) */\n");
    sb_printf(sb, "                int8x16_t w_block = vld1q_s8(w_ptr);\n");
    sb_printf(sb, "                w_ptr += 16;\n\n");
    
    sb_printf(sb, "                /* SMMLA: acc[2x2] += a[2x8] * w[8x2] */\n");
    sb_printf(sb, "                acc = vmmlaq_s32(acc, a_block, w_block);\n");
    sb_printf(sb, "            }\n\n");
    
    sb_printf(sb, "            /* Store 2x2 results */\n");
    sb_printf(sb, "            int n = np * 2;\n");
    sb_printf(sb, "            y_row0[n]     = vgetq_lane_s32(acc, 0);\n");
    sb_printf(sb, "            y_row0[n + 1] = vgetq_lane_s32(acc, 1);\n");
    sb_printf(sb, "            y_row1[n]     = vgetq_lane_s32(acc, 2);\n");
    sb_printf(sb, "            y_row1[n + 1] = vgetq_lane_s32(acc, 3);\n");
    sb_printf(sb, "        }\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
    
    /*
     * Highly optimized version: 4 SMMLA per K iteration (2x4 output tile)
     */
    sb_printf(sb, "/*\n");
    sb_printf(sb, " * Highly optimized batch MatMul (2x4 tile, 4 SMMLA per K iter)\n");
    sb_printf(sb, " * Requires N %% 4 == 0\n");
    sb_printf(sb, " */\n");
    sb_printf(sb, "void %s_forward_batch_opt(\n", name);
    sb_printf(sb, "    const int8_t* x,\n");
    sb_printf(sb, "    const int8_t* W_packed,\n");
    sb_printf(sb, "    int32_t* y,\n");
    sb_printf(sb, "    int batch_size\n");
    sb_printf(sb, ") {\n");
    sb_printf(sb, "    const int K_blocks = %d;\n", K_blocks);
    sb_printf(sb, "    const int N_quads = %d;\n\n", N / 4);
    
    sb_printf(sb, "    for (int m = 0; m < batch_size; m += 2) {\n");
    sb_printf(sb, "        const int8_t* x_row0 = x + m * %d;\n", K);
    sb_printf(sb, "        const int8_t* x_row1 = x + (m + 1) * %d;\n", K);
    sb_printf(sb, "        int32_t* y_row0 = y + m * %d;\n", N);
    sb_printf(sb, "        int32_t* y_row1 = y + (m + 1) * %d;\n\n", N);
    
    sb_printf(sb, "        /* Process 4 output channels at a time */\n");
    sb_printf(sb, "        for (int nq = 0; nq < N_quads; nq++) {\n");
    sb_printf(sb, "            int32x4_t acc0 = vdupq_n_s32(0);  /* cols 0,1 */\n");
    sb_printf(sb, "            int32x4_t acc1 = vdupq_n_s32(0);  /* cols 2,3 */\n\n");
    
    sb_printf(sb, "            const int8_t* w_ptr0 = W_packed + (nq * 2) * K_blocks * 16;\n");
    sb_printf(sb, "            const int8_t* w_ptr1 = W_packed + (nq * 2 + 1) * K_blocks * 16;\n\n");
    
    sb_printf(sb, "            for (int kb = 0; kb < K_blocks; kb++) {\n");
    sb_printf(sb, "                /* Load activation block once, reuse for both pairs */\n");
    sb_printf(sb, "                int8x16_t a_block = vcombine_s8(\n");
    sb_printf(sb, "                    vld1_s8(x_row0 + kb * 8),\n");
    sb_printf(sb, "                    vld1_s8(x_row1 + kb * 8)\n");
    sb_printf(sb, "                );\n\n");
    
    sb_printf(sb, "                /* Load 2 weight blocks */\n");
    sb_printf(sb, "                int8x16_t w0 = vld1q_s8(w_ptr0);\n");
    sb_printf(sb, "                int8x16_t w1 = vld1q_s8(w_ptr1);\n");
    sb_printf(sb, "                w_ptr0 += 16;\n");
    sb_printf(sb, "                w_ptr1 += 16;\n\n");
    
    sb_printf(sb, "                acc0 = vmmlaq_s32(acc0, a_block, w0);\n");
    sb_printf(sb, "                acc1 = vmmlaq_s32(acc1, a_block, w1);\n");
    sb_printf(sb, "            }\n\n");
    
    sb_printf(sb, "            /* Store 2x4 results */\n");
    sb_printf(sb, "            int n = nq * 4;\n");
    sb_printf(sb, "            y_row0[n+0] = vgetq_lane_s32(acc0, 0);\n");
    sb_printf(sb, "            y_row0[n+1] = vgetq_lane_s32(acc0, 1);\n");
    sb_printf(sb, "            y_row0[n+2] = vgetq_lane_s32(acc1, 0);\n");
    sb_printf(sb, "            y_row0[n+3] = vgetq_lane_s32(acc1, 1);\n");
    sb_printf(sb, "            y_row1[n+0] = vgetq_lane_s32(acc0, 2);\n");
    sb_printf(sb, "            y_row1[n+1] = vgetq_lane_s32(acc0, 3);\n");
    sb_printf(sb, "            y_row1[n+2] = vgetq_lane_s32(acc1, 2);\n");
    sb_printf(sb, "            y_row1[n+3] = vgetq_lane_s32(acc1, 3);\n");
    sb_printf(sb, "        }\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
    
    /* Legacy version that works with row-major weights (slower but compatible) */
    sb_printf(sb, "/*\n");
    sb_printf(sb, " * Legacy batch MatMul (row-major weights, slower)\n");
    sb_printf(sb, " * Use %s_pack_i8mm() + %s_forward_batch_packed() for best performance\n", name, name);
    sb_printf(sb, " */\n");
    sb_printf(sb, "void %s_forward_batch(\n", name);
    sb_printf(sb, "    const int8_t* x,\n");
    sb_printf(sb, "    int32_t* y,\n");
    sb_printf(sb, "    int batch_size\n");
    sb_printf(sb, ") {\n");
    sb_printf(sb, "    const int8_t* W = %s_W;\n", name);
    sb_printf(sb, "    const int K_blocks = %d;\n\n", K_blocks);
    
    sb_printf(sb, "    for (int m = 0; m < batch_size; m += 2) {\n");
    sb_printf(sb, "        const int8_t* x_row0 = x + m * %d;\n", K);
    sb_printf(sb, "        const int8_t* x_row1 = x + (m + 1) * %d;\n", K);
    sb_printf(sb, "        int32_t* y_row0 = y + m * %d;\n", N);
    sb_printf(sb, "        int32_t* y_row1 = y + (m + 1) * %d;\n\n", N);
    
    sb_printf(sb, "        for (int n = 0; n < %d; n += 2) {\n", N);
    sb_printf(sb, "            int32x4_t acc = vdupq_n_s32(0);\n\n");
    
    sb_printf(sb, "            for (int kb = 0; kb < K_blocks; kb++) {\n");
    sb_printf(sb, "                int k_off = kb * 8;\n");
    sb_printf(sb, "                int8x16_t a_block = vcombine_s8(\n");
    sb_printf(sb, "                    vld1_s8(x_row0 + k_off),\n");
    sb_printf(sb, "                    vld1_s8(x_row1 + k_off)\n");
    sb_printf(sb, "                );\n\n");
    
    sb_printf(sb, "                /* Load weight columns (gather - slow) */\n");
    sb_printf(sb, "                int8x8_t w0 = vld1_s8(W + n * %d + k_off);\n", K);
    sb_printf(sb, "                int8x8_t w1 = vld1_s8(W + (n+1) * %d + k_off);\n", K);
    sb_printf(sb, "                int8x16_t w_block = vcombine_s8(w0, w1);\n\n");
    
    sb_printf(sb, "                acc = vmmlaq_s32(acc, a_block, w_block);\n");
    sb_printf(sb, "            }\n\n");
    
    sb_printf(sb, "            y_row0[n]     = vgetq_lane_s32(acc, 0);\n");
    sb_printf(sb, "            y_row0[n + 1] = vgetq_lane_s32(acc, 1);\n");
    sb_printf(sb, "            y_row1[n]     = vgetq_lane_s32(acc, 2);\n");
    sb_printf(sb, "            y_row1[n + 1] = vgetq_lane_s32(acc, 3);\n");
    sb_printf(sb, "        }\n");
    sb_printf(sb, "    }\n");
    sb_printf(sb, "}\n\n");
}

/* ═══════════════════════════════════════════════════════════════════════════
 * WEIGHT HEADER EMISSION
 * ═══════════════════════════════════════════════════════════════════════════ */

static void emit_weights_header(StringBuffer* sb, const AggregateShapeSpec* spec) {
    const char* name = spec->name;
    int N = spec->N;
    int K = spec->K;
    
    sb_printf(sb, "/* Frozen weights for %s */\n", name);
    sb_printf(sb, "/* Dimensions: [%d, %d] */\n\n", N, K);
    
    /* Emit weights based on quantization type */
    switch (spec->quant) {
        case QUANT_TERNARY:
        case QUANT_INT8:
            sb_printf(sb, "static const int8_t %s_W[%d] __attribute__((aligned(64))) = {\n", 
                     name, N * K);
            /* Weights would be emitted here from spec->weights */
            sb_printf(sb, "    /* Weights embedded at forge time */\n");
            if (spec->weights) {
                const int8_t* w = (const int8_t*)spec->weights;
                for (int i = 0; i < N * K; i++) {
                    if (i % 16 == 0) sb_printf(sb, "    ");
                    sb_printf(sb, "%4d,", w[i]);
                    if (i % 16 == 15 || i == N * K - 1) sb_printf(sb, "\n");
                }
            }
            sb_printf(sb, "};\n\n");
            break;
            
        case QUANT_FLOAT32:
            sb_printf(sb, "static const float %s_W[%d] __attribute__((aligned(64))) = {\n",
                     name, N * K);
            if (spec->weights) {
                const float* w = (const float*)spec->weights;
                for (int i = 0; i < N * K; i++) {
                    if (i % 8 == 0) sb_printf(sb, "    ");
                    sb_printf(sb, "%12.6ff,", w[i]);
                    if (i % 8 == 7) sb_printf(sb, "\n");
                }
            }
            sb_printf(sb, "};\n\n");
            break;
    }
    
    /* Emit bias if present */
    if (spec->bias) {
        sb_printf(sb, "static const float %s_bias[%d] = {\n    ", name, N);
        for (int i = 0; i < N; i++) {
            sb_printf(sb, "%12.6ff,", spec->bias[i]);
            if (i % 8 == 7 && i < N - 1) sb_printf(sb, "\n    ");
        }
        sb_printf(sb, "\n};\n\n");
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * PUBLIC API
 * ═══════════════════════════════════════════════════════════════════════════ */

size_t forge_kernel_to_string(const AggregateShapeSpec* spec, 
                               char* buffer, size_t buffer_size) {
    StringBuffer sb;
    sb_init(&sb, buffer, buffer_size);
    
    /* Header */
    time_t now = time(NULL);
    sb_printf(&sb, "/*\n");
    sb_printf(&sb, " * TriX Forged Soft-Chip: %s\n", spec->name);
    sb_printf(&sb, " * Generated: %s", ctime(&now));
    sb_printf(&sb, " * Topology: K=%d, N=%d\n", spec->K, spec->N);
    sb_printf(&sb, " *\n");
    sb_printf(&sb, " * \"The SDOT instruction IS the Prime.\"\n");
    sb_printf(&sb, " */\n\n");
    
    /* Includes */
    sb_printf(&sb, "#include <stdint.h>\n");
    if (spec->activation == ACT_SIGMOID || spec->activation == ACT_TANH || 
        spec->activation == ACT_GELU || spec->activation == ACT_SOFTMAX) {
        sb_printf(&sb, "#include <math.h>\n");
    }
    sb_printf(&sb, "\n");
    
    /* Emit weights */
    emit_weights_header(&sb, spec);
    
    /* Emit kernel based on strategy */
    switch (spec->strategy) {
        case FORGE_STRATEGY_C_PORTABLE:
            emit_c_portable(&sb, spec);
            break;
        case FORGE_STRATEGY_NEON_BLOCK_16:
            emit_neon_block16(&sb, spec);
            break;
        case FORGE_STRATEGY_NEON_BLOCK8_K64:
            emit_neon_block8_k64(&sb, spec);
            break;
        case FORGE_STRATEGY_NEON_GHOST_12:
            emit_neon_ghost12(&sb, spec);
            break;
        case FORGE_STRATEGY_I8MM_BATCH:
            emit_i8mm_batch(&sb, spec);
            break;
    }
    
    return sb.pos;
}

int forge_kernel_to_file(const AggregateShapeSpec* spec, FILE* out) {
    char buffer[65536];  /* 64KB should be enough for any kernel */
    size_t len = forge_kernel_to_string(spec, buffer, sizeof(buffer));
    
    if (len == 0) return -1;
    
    size_t written = fwrite(buffer, 1, len, out);
    return (written == len) ? 0 : -1;
}

int forge_weights_header(const AggregateShapeSpec* spec, FILE* out) {
    char buffer[1024 * 1024];  /* 1MB for large weight arrays */
    StringBuffer sb;
    sb_init(&sb, buffer, sizeof(buffer));
    
    emit_weights_header(&sb, spec);
    
    size_t written = fwrite(buffer, 1, sb.pos, out);
    return (written == sb.pos) ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * WEIGHT PACKING
 * ═══════════════════════════════════════════════════════════════════════════ */

void forge_pack_block16(const int8_t* src, int8_t* dst, int N, int K) {
    const int K_blocks = K / 16;
    const int N_blocks = N / 16;
    const int block_stride = 16 * 16;
    
    for (int nb = 0; nb < N_blocks; nb++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            int8_t* dst_block = dst + (nb * K_blocks + kb) * block_stride;
            
            for (int ch = 0; ch < 16; ch++) {
                int n = nb * 16 + ch;
                int k_start = kb * 16;
                
                for (int kk = 0; kk < 16; kk++) {
                    dst_block[ch * 16 + kk] = src[n * K + k_start + kk];
                }
            }
        }
    }
}

void forge_pack_block8_k64(const int8_t* src, int8_t* dst, int N, int K) {
    const int K_blocks = K / 64;
    const int N_blocks = N / 8;
    const int block_stride = 8 * 64;  /* 512 bytes per block */
    
    for (int nb = 0; nb < N_blocks; nb++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            int8_t* dst_block = dst + (nb * K_blocks + kb) * block_stride;
            
            for (int ch = 0; ch < 8; ch++) {
                int n = nb * 8 + ch;
                int k_start = kb * 64;
                
                for (int kk = 0; kk < 64; kk++) {
                    dst_block[ch * 64 + kk] = src[n * K + k_start + kk];
                }
            }
        }
    }
}

void forge_pack_ghost12(const int8_t* src, int8_t* dst, int N, int K) {
    const int K_blocks = K / 64;
    const int N_blocks = N / 12;
    const int block_stride = 12 * 64;
    
    for (int nb = 0; nb < N_blocks; nb++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            int8_t* dst_block = dst + (nb * K_blocks + kb) * block_stride;
            
            for (int ch = 0; ch < 12; ch++) {
                int n = nb * 12 + ch;
                int k_start = kb * 64;
                
                for (int kk = 0; kk < 64; kk++) {
                    dst_block[ch * 64 + kk] = src[n * K + k_start + kk];
                }
            }
        }
    }
}

void forge_unpack_ternary(const uint8_t* packed, int8_t* unpacked, int n) {
    for (int i = 0; i < n; i++) {
        int byte_idx = i / 4;
        int bit_pos = (i % 4) * 2;
        int bits = (packed[byte_idx] >> bit_pos) & 0x3;
        
        if (bits == 0) unpacked[i] = 0;
        else if (bits == 1) unpacked[i] = 1;
        else unpacked[i] = -1;  /* bits == 2 or 3 */
    }
}

void forge_pack_i8mm(const int8_t* src, int8_t* dst, int N, int K) {
    /* I8MM uses row-major format, but K must be multiple of 8 */
    int K_aligned = ((K + 7) / 8) * 8;
    
    for (int n = 0; n < N; n++) {
        for (int k = 0; k < K_aligned; k++) {
            if (k < K) {
                dst[n * K_aligned + k] = src[n * K + k];
            } else {
                dst[n * K_aligned + k] = 0;  /* Zero-pad */
            }
        }
    }
}
