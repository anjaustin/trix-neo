/*
 * cfc_forge.h — TriX CfC Soft-Chip Forge
 *
 * Forges complete CfC (Closed-form Continuous-time) cells as soft-chips.
 *
 * CfC Architecture:
 *   gate      = sigmoid(W_gate @ [x; h_prev] + b_gate)
 *   candidate = tanh(W_cand @ [x; h_prev] + b_cand)
 *   decay     = exp(-dt / tau)
 *   h_new     = (1 - gate) * h_prev * decay + gate * candidate
 *
 * The forge stamps:
 *   - NEON Block-8-K64 kernels for W_gate and W_cand MatVec
 *   - Fused sigmoid/tanh activations
 *   - The gating/decay update logic
 *   - Optional output projection layer
 *
 * Copyright 2026 Trix Research
 */

#ifndef TRIX_CFC_FORGE_H
#define TRIX_CFC_FORGE_H

#include "linear_forge.h"
#include <stdio.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC CELL SPECIFICATION
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    char name[64];              /* Cell name (e.g., "cfc_layer1") */
    
    /* Dimensions */
    int input_dim;              /* x vector size */
    int hidden_dim;             /* h vector size */
    
    /* Weights (frozen at forge time) */
    const int8_t* W_gate;       /* [hidden_dim, input_dim + hidden_dim] packed */
    const int8_t* W_cand;       /* [hidden_dim, input_dim + hidden_dim] packed */
    const float* b_gate;        /* [hidden_dim] */
    const float* b_cand;        /* [hidden_dim] */
    const float* tau;           /* Time constant [hidden_dim] or [1] */
    int tau_shared;             /* If true, single tau for all hidden units */
    
    /* Optional output projection */
    int has_output;             /* If true, include W_out projection */
    int output_dim;
    const int8_t* W_out;        /* [output_dim, hidden_dim] packed */
    const float* b_out;         /* [output_dim] */
    
    /* Target strategy */
    ForgeStrategy strategy;     /* NEON_BLOCK8_K64 recommended */
    
} CfCCellSpec;

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC FORGE API
 * ═══════════════════════════════════════════════════════════════════════════ */

/*
 * forge_cfc_to_string — Generate complete CfC soft-chip
 *
 * Emits:
 *   - Weight arrays (frozen)
 *   - gate_forward_neon() kernel
 *   - candidate_forward_neon() kernel
 *   - cfc_cell() function with gating logic
 *   - cfc_forward() for sequence processing
 *   - Optional: output_forward() for classification
 */
size_t forge_cfc_to_string(const CfCCellSpec* spec, char* buffer, size_t size);

/*
 * forge_cfc_to_file — Write CfC soft-chip to file
 */
int forge_cfc_to_file(const CfCCellSpec* spec, FILE* out);

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC METRICS
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Operations per cell step (2 MatVec + gating) */
static inline size_t cfc_ops_per_step(const CfCCellSpec* spec) {
    int concat_dim = spec->input_dim + spec->hidden_dim;
    /* 2 MatVecs (gate + candidate) */
    size_t matvec_ops = 2 * 2 * (size_t)spec->hidden_dim * concat_dim;
    /* Gating: 3 muls + 2 adds per hidden unit */
    size_t gate_ops = 5 * spec->hidden_dim;
    return matvec_ops + gate_ops;
}

/* Weight memory (ternary packed) */
static inline size_t cfc_weight_bytes(const CfCCellSpec* spec) {
    int concat_dim = spec->input_dim + spec->hidden_dim;
    size_t bytes = 0;
    bytes += (size_t)spec->hidden_dim * concat_dim;  /* W_gate */
    bytes += spec->hidden_dim * sizeof(float);        /* b_gate */
    bytes += (size_t)spec->hidden_dim * concat_dim;  /* W_cand */
    bytes += spec->hidden_dim * sizeof(float);        /* b_cand */
    bytes += (spec->tau_shared ? 1 : spec->hidden_dim) * sizeof(float);
    
    if (spec->has_output) {
        bytes += (size_t)spec->output_dim * spec->hidden_dim;  /* W_out */
        bytes += spec->output_dim * sizeof(float);              /* b_out */
    }
    
    return bytes;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIX_CFC_FORGE_H */
