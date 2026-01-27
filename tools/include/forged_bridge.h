/*
 * forged_bridge.h — Bridge between Forged Soft-Chips and Yinsen Runtime
 *
 * This header provides utilities to:
 *   1. Convert between yinsen trit packing (2-bit) and forge int8 layout
 *   2. Wrap forged kernels in yinsen-compatible interfaces
 *   3. Run forged soft-chips with yinsen data structures
 *
 * The forge uses int8 weights (ternary values) in Block-8-K64 NEON layout.
 * Yinsen uses packed trits (2 bits per weight).
 *
 * Copyright 2026 Trix Research
 */

#ifndef TRIX_FORGED_BRIDGE_H
#define TRIX_FORGED_BRIDGE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * TRIT <-> INT8 CONVERSION
 *
 * Yinsen packing: 4 trits per byte, 2 bits each
 *   00 = 0, 01 = +1, 11 = -1
 *
 * Forge layout: int8 values {-1, 0, +1}, Block-8-K64 arrangement
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Trit encoding (matches yinsen/ternary.h) */
#define TRIT_ZERO  0x0
#define TRIT_POS   0x1
#define TRIT_NEG   0x3

/* Unpack single trit from packed byte */
static inline int8_t bridge_trit_unpack(uint8_t packed, int pos) {
    uint8_t bits = (packed >> (pos * 2)) & 0x3;
    if (bits == TRIT_ZERO) return 0;
    if (bits == TRIT_POS)  return 1;
    if (bits == TRIT_NEG)  return -1;
    return 0;
}

/* Pack int8 trit to 2-bit encoding */
static inline uint8_t bridge_trit_encode(int8_t val) {
    if (val > 0)  return TRIT_POS;
    if (val < 0)  return TRIT_NEG;
    return TRIT_ZERO;
}

/*
 * Convert yinsen packed trits to forge int8 layout (row-major)
 *
 * packed: yinsen format, 4 trits per byte, row-major
 * int8_out: forge format, int8 values, row-major
 * M: rows
 * N: cols
 */
static inline void bridge_trit_to_int8(
    const uint8_t* packed,
    int8_t* int8_out,
    int M, int N
) {
    int bytes_per_row = (N + 3) / 4;
    
    for (int i = 0; i < M; i++) {
        const uint8_t* row = packed + i * bytes_per_row;
        int8_t* out_row = int8_out + i * N;
        
        for (int j = 0; j < N; j++) {
            int byte_idx = j / 4;
            int bit_pos = j % 4;
            out_row[j] = bridge_trit_unpack(row[byte_idx], bit_pos);
        }
    }
}

/*
 * Convert forge int8 layout to yinsen packed trits
 *
 * int8_in: forge format, int8 values
 * packed: yinsen format, 4 trits per byte
 * M: rows
 * N: cols
 */
static inline void bridge_int8_to_trit(
    const int8_t* int8_in,
    uint8_t* packed,
    int M, int N
) {
    int bytes_per_row = (N + 3) / 4;
    
    for (int i = 0; i < M; i++) {
        const int8_t* in_row = int8_in + i * N;
        uint8_t* out_row = packed + i * bytes_per_row;
        
        memset(out_row, 0, bytes_per_row);
        
        for (int j = 0; j < N; j++) {
            int byte_idx = j / 4;
            int bit_pos = j % 4;
            out_row[byte_idx] |= bridge_trit_encode(in_row[j]) << (bit_pos * 2);
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * BLOCK-8-K64 WEIGHT PACKING
 *
 * The forge NEON kernels expect weights in Block-8-K64 layout:
 *   - 8 output rows per block
 *   - 64 K elements per iteration
 *   - Within each block: [row0_k0..k63, row1_k0..k63, ..., row7_k0..k63]
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Pack row-major int8 weights to Block-8-K64 layout
 *
 * src: row-major int8 weights [N, K]
 * dst: Block-8-K64 packed weights
 * N: output dimension (must be multiple of 8)
 * K: input dimension (must be multiple of 64)
 */
static inline void bridge_pack_block8_k64(
    const int8_t* src,
    int8_t* dst,
    int N, int K
) {
    int N_blocks = N / 8;
    int K_blocks = K / 64;
    int block_stride = 8 * 64;  /* 512 bytes per N-block per K-block */
    
    for (int nb = 0; nb < N_blocks; nb++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            int8_t* block = dst + (nb * K_blocks + kb) * block_stride;
            
            for (int row = 0; row < 8; row++) {
                int global_row = nb * 8 + row;
                const int8_t* src_row = src + global_row * K + kb * 64;
                int8_t* dst_row = block + row * 64;
                memcpy(dst_row, src_row, 64);
            }
        }
    }
}

/*
 * Unpack Block-8-K64 layout back to row-major
 */
static inline void bridge_unpack_block8_k64(
    const int8_t* src,
    int8_t* dst,
    int N, int K
) {
    int N_blocks = N / 8;
    int K_blocks = K / 64;
    int block_stride = 8 * 64;
    
    for (int nb = 0; nb < N_blocks; nb++) {
        for (int kb = 0; kb < K_blocks; kb++) {
            const int8_t* block = src + (nb * K_blocks + kb) * block_stride;
            
            for (int row = 0; row < 8; row++) {
                int global_row = nb * 8 + row;
                const int8_t* src_row = block + row * 64;
                int8_t* dst_row = dst + global_row * K + kb * 64;
                memcpy(dst_row, src_row, 64);
            }
        }
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * ACTIVATION QUANTIZATION
 *
 * Forge kernels expect int8 activations, yinsen uses float.
 * This bridge handles the conversion.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    float scale;        /* Multiply int result by this to get float */
    float inv_scale;    /* 127 / absmax */
} BridgeQuantParams;

/*
 * Quantize float activations to int8 (symmetric, per-tensor)
 */
static inline void bridge_quantize_activations(
    const float* x,
    int8_t* x_q,
    int n,
    BridgeQuantParams* params
) {
    /* Find absmax */
    float absmax = 0.0f;
    for (int i = 0; i < n; i++) {
        float abs_val = x[i] > 0 ? x[i] : -x[i];
        if (abs_val > absmax) absmax = abs_val;
    }
    
    if (absmax < 1e-8f) absmax = 1e-8f;
    
    params->scale = absmax / 127.0f;
    params->inv_scale = 127.0f / absmax;
    
    for (int i = 0; i < n; i++) {
        int v = (int)(x[i] * params->inv_scale);
        if (v > 127) v = 127;
        if (v < -127) v = -127;
        x_q[i] = (int8_t)v;
    }
}

/*
 * Dequantize int32 accumulator to float
 */
static inline void bridge_dequantize_accum(
    const int32_t* y_int,
    float* y_float,
    int n,
    const BridgeQuantParams* params
) {
    for (int i = 0; i < n; i++) {
        y_float[i] = (float)y_int[i] * params->scale;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * DIMENSION UTILITIES
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Round up to multiple of 8 (for N dimension) */
static inline int bridge_align8(int n) {
    return ((n + 7) / 8) * 8;
}

/* Round up to multiple of 64 (for K dimension) */
static inline int bridge_align64(int k) {
    return ((k + 63) / 64) * 64;
}

/* Calculate packed weight size for Block-8-K64 layout */
static inline size_t bridge_block8_k64_bytes(int N, int K) {
    int N_aligned = bridge_align8(N);
    int K_aligned = bridge_align64(K);
    return (size_t)N_aligned * K_aligned;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MEMORY STATISTICS
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    size_t yinsen_bytes;    /* 2-bit packed trits */
    size_t forge_bytes;     /* int8 Block-8-K64 */
    size_t float_bytes;     /* float32 baseline */
    float compression_vs_float;
    float forge_overhead;   /* forge / yinsen */
} BridgeMemoryStats;

static inline void bridge_memory_stats(
    int M, int N,
    BridgeMemoryStats* stats
) {
    int N_aligned = bridge_align8(N);
    int bytes_per_row_trit = (N + 3) / 4;
    
    stats->yinsen_bytes = (size_t)M * bytes_per_row_trit;
    stats->forge_bytes = bridge_block8_k64_bytes(M, N_aligned);
    stats->float_bytes = (size_t)M * N * sizeof(float);
    
    stats->compression_vs_float = (float)stats->float_bytes / (float)stats->forge_bytes;
    stats->forge_overhead = (float)stats->forge_bytes / (float)stats->yinsen_bytes;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIX_FORGED_BRIDGE_H */
