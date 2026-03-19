/*
 * linear_forge.h — TriX Linear Kingdom Circuit Stamper
 *
 * "Forge isn't a loop compiler. It's a circuit stamper."
 *
 * This generates ASIC-style soft-chips for Linear Kingdom shapes.
 * The SDOT instruction IS the Prime — it matches activation patterns
 * against weight patterns and accumulates the result.
 *
 * Architecture:
 *   - AggregateShape: Tiles Primes across memory (the "field")
 *   - ForgeKernel(): Stamps the M4 assembly directly
 *   - No interpreter, no runtime loop — just frozen instructions
 *
 * Copyright 2026 Trix Research
 */

#ifndef TRIX_LINEAR_FORGE_H
#define TRIX_LINEAR_FORGE_H

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * STRATEGY SELECTION
 *
 * Different M4 instruction patterns for different workloads.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    FORGE_STRATEGY_C_PORTABLE,      /* Scalar C loops (reference) */
    FORGE_STRATEGY_NEON_BLOCK_16,   /* NEON SDOT, 16 output channels, K=16 */
    FORGE_STRATEGY_NEON_BLOCK8_K64, /* NEON SDOT, 8 output channels, K=64 (fastest) */
    FORGE_STRATEGY_NEON_GHOST_12,   /* LDNP + SDOT, 12 output channels */
    FORGE_STRATEGY_I8MM_BATCH,      /* SMMLA for batch > 1 */
} ForgeStrategy;

/* ═══════════════════════════════════════════════════════════════════════════
 * QUANTIZATION TYPES
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    QUANT_TERNARY,    /* {-1, 0, +1}, 2 bits packed */
    QUANT_INT8,       /* [-127, 127] */
    QUANT_FLOAT32,    /* IEEE float (for debugging) */
} QuantType;

/* ═══════════════════════════════════════════════════════════════════════════
 * ACTIVATION FUSION
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef enum {
    ACT_NONE,
    ACT_RELU,
    ACT_SIGMOID,
    ACT_TANH,
    ACT_GELU,
    ACT_SOFTMAX,
} ActivationType;

/* ═══════════════════════════════════════════════════════════════════════════
 * AGGREGATE SHAPE SPECIFICATION
 *
 * Defines the topology of a Linear Kingdom shape.
 * The Forge stamps this topology into an instruction stream.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    /* Identity */
    char name[64];              /* Kernel name (e.g., "layer1_gate") */
    
    /* Topology */
    int K;                      /* Input depth (must be multiple of 16) */
    int N;                      /* Output width (must be multiple of 12 or 16) */
    
    /* Weights (frozen at forge time) */
    const void* weights;        /* Packed weights, layout depends on strategy */
    size_t weights_size;
    QuantType quant;
    
    /* Optional bias */
    const float* bias;          /* N floats, or NULL */
    
    /* Optional fused activation */
    ActivationType activation;
    
    /* Strategy selection */
    ForgeStrategy strategy;
    
} AggregateShapeSpec;

/* ═══════════════════════════════════════════════════════════════════════════
 * FORGE API
 *
 * These functions "stamp" the circuit into output buffers.
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * forge_kernel_to_file — Stamp kernel to C/ASM file
 *
 * Writes complete, compilable C code with inline assembly to the file.
 * The output is self-contained — no dependencies except arm_neon.h.
 *
 * Returns: 0 on success, -1 on error
 */
int forge_kernel_to_file(const AggregateShapeSpec* spec, FILE* out);

/*
 * forge_kernel_to_string — Stamp kernel to string buffer
 *
 * Writes to buffer, returns bytes written.
 * Buffer must be pre-allocated (recommend 64KB for large kernels).
 */
size_t forge_kernel_to_string(const AggregateShapeSpec* spec, 
                               char* buffer, size_t buffer_size);

/*
 * forge_weights_header — Emit frozen weights as C header
 *
 * Writes static const arrays for weights and optional bias.
 * Weights are packed into the layout required by the strategy.
 */
int forge_weights_header(const AggregateShapeSpec* spec, FILE* out);

/* ═══════════════════════════════════════════════════════════════════════════
 * WEIGHT PACKING
 *
 * Transform weights into the layout required by each strategy.
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Pack ternary weights for Block-16 kernel
 * 
 * Input:  Row-major int8 weights [N, K] where values are -1, 0, +1
 * Output: Block-16 layout [N/16, K/16, 16, 16]
 */
int forge_pack_block16(const int8_t* src, int8_t* dst, int N, int K);

/*
 * Pack ternary weights for Block-8-K64 kernel (highest performance)
 *
 * Input:  Row-major int8 weights [N, K]
 * Output: Block-8-K64 layout [N/8, K/64, 8, 64]
 */
int forge_pack_block8_k64(const int8_t* src, int8_t* dst, int N, int K);

/*
 * Pack ternary weights for Ghost-12 kernel
 *
 * Input:  Row-major int8 weights [N, K]
 * Output: Ghost-12 layout [N/12, K/64, 12, 64]
 */
int forge_pack_ghost12(const int8_t* src, int8_t* dst, int N, int K);

/*
 * Pack weights for I8MM batch kernel
 *
 * For I8MM, weights are kept in row-major [N, K] format but K must
 * be a multiple of 8. The SMMLA instruction processes 8 K-elements
 * per operation.
 *
 * Input:  Row-major int8 weights [N, K]
 * Output: Row-major with K padded to multiple of 8
 */
int forge_pack_i8mm(const int8_t* src, int8_t* dst, int N, int K);

/*
 * Unpack 2-bit ternary to int8
 */
int forge_unpack_ternary(const uint8_t* packed, int8_t* unpacked, int n);

/* ═══════════════════════════════════════════════════════════════════════════
 * METRICS
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * Estimate operations for a forged kernel
 */
static inline size_t forge_ops(const AggregateShapeSpec* spec) {
    /* MatVec: 2*N*K ops (mul + add) */
    return 2 * (size_t)spec->N * (size_t)spec->K;
}

/*
 * Estimate weight memory
 */
static inline size_t forge_weight_bytes(const AggregateShapeSpec* spec) {
    switch (spec->quant) {
        case QUANT_TERNARY: return ((size_t)spec->N * spec->K + 3) / 4;
        case QUANT_INT8:    return (size_t)spec->N * spec->K;
        case QUANT_FLOAT32: return (size_t)spec->N * spec->K * 4;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIX_LINEAR_FORGE_H */
