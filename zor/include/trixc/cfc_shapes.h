/*
 * TRIXC CfC Shapes — Closed-form Continuous-time Neural Units
 *
 * "Solid-State Fluid Dynamics"
 *
 * The CfC cell is a single frozen graph that replaces ODE integration
 * with closed-form math. No loops. No iteration. Just topology.
 *
 * h(t) = (1 - gate) * h_prev * decay + gate * candidate
 *
 * Where:
 *   gate     = σ(W_gate @ [x, h_prev] + b_gate)
 *   candidate = tanh(W_cand @ [x, h_prev] + b_cand)
 *   decay    = exp(-Δt / τ)
 *
 * All components built from the 5 Primes: ADD, MUL, EXP, MAX, CONST
 *
 * Created by: Tripp + Claude
 * Date: January 2026
 */

#ifndef TRIXC_CFC_SHAPES_H
#define TRIXC_CFC_SHAPES_H

#include "onnx_shapes.h"
#include <math.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC Cell Parameters (Frozen after training)
 *
 * These are the "learned routing" — frozen weights that determine
 * how signals flow through the shape topology.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * CfC Cell configuration
 *
 * input_dim:  Size of input signal
 * hidden_dim: Size of hidden state (the "neurons")
 * tau:        Time constant for decay (learned or fixed)
 */
typedef struct {
    int input_dim;
    int hidden_dim;

    /* Gate weights: W_gate @ [x; h] + b_gate */
    const float* W_gate;    /* [hidden_dim, input_dim + hidden_dim] */
    const float* b_gate;    /* [hidden_dim] */

    /* Candidate weights: W_cand @ [x; h] + b_cand */
    const float* W_cand;    /* [hidden_dim, input_dim + hidden_dim] */
    const float* b_cand;    /* [hidden_dim] */

    /* Time constants (one per neuron, or single shared) */
    const float* tau;       /* [hidden_dim] or [1] */
    int tau_shared;         /* If true, single tau for all neurons */

} CfCParams;

/* ═══════════════════════════════════════════════════════════════════════════
 * Core CfC Shape: The "CfC-Tile"
 *
 * This is the atomic unit of Liquid computation in TriX.
 * A single function call that encompasses what would otherwise
 * require ODE integration.
 *
 * The routing IS the integration.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * trix_cfc_cell — Single CfC cell forward pass
 *
 * Computes: h_new = (1 - gate) * h_prev * decay + gate * candidate
 *
 * @param x         Input signal [input_dim]
 * @param h_prev    Previous hidden state [hidden_dim]
 * @param dt        Time delta (seconds or normalized)
 * @param params    Frozen cell parameters
 * @param h_new     Output: new hidden state [hidden_dim]
 *
 * All computation is feedforward. No loops over time.
 * The closed-form handles arbitrary dt in one shot.
 */
static inline void trix_cfc_cell(
    const float* x,
    const float* h_prev,
    float dt,
    const CfCParams* params,
    float* h_new
) {
    const int in_dim = params->input_dim;
    const int hid_dim = params->hidden_dim;
    const int concat_dim = in_dim + hid_dim;

    /* Stack for temporary allocations (no malloc) */
    float concat[concat_dim];       /* [x; h_prev] */
    float gate_pre[hid_dim];        /* Pre-activation gate */
    float gate[hid_dim];            /* Sigmoid gate */
    float cand_pre[hid_dim];        /* Pre-activation candidate */
    float candidate[hid_dim];       /* Tanh candidate */

    /* ─────────────────────────────────────────────────────────────────────
     * Step 1: Concatenate [x; h_prev]
     * This is the "input gathering" — pure topology, no math
     * ───────────────────────────────────────────────────────────────────── */
    memcpy(concat, x, in_dim * sizeof(float));
    memcpy(concat + in_dim, h_prev, hid_dim * sizeof(float));

    /* ─────────────────────────────────────────────────────────────────────
     * Step 2: Gate computation
     * gate = σ(W_gate @ concat + b_gate)
     *
     * Shapes used: GEMM (MUL + ADD), Sigmoid (EXP)
     * ───────────────────────────────────────────────────────────────────── */
    trix_onnx_gemm(concat, params->W_gate, params->b_gate,
                   gate_pre, 1, hid_dim, concat_dim, 1.0f, 1.0f);

    for (int i = 0; i < hid_dim; i++) {
        gate[i] = trix_onnx_sigmoid(gate_pre[i]);
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 3: Candidate computation
     * candidate = tanh(W_cand @ concat + b_cand)
     *
     * Shapes used: GEMM (MUL + ADD), Tanh (EXP)
     * ───────────────────────────────────────────────────────────────────── */
    trix_onnx_gemm(concat, params->W_cand, params->b_cand,
                   cand_pre, 1, hid_dim, concat_dim, 1.0f, 1.0f);

    for (int i = 0; i < hid_dim; i++) {
        candidate[i] = trix_onnx_tanh(cand_pre[i]);
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 4: Decay computation (the "time shortcut")
     * decay = exp(-dt / tau)
     *
     * This is where CfC magic happens: instead of iterating through
     * time steps, we jump directly to the answer.
     *
     * Shapes used: EXP, MUL, CONST
     * ───────────────────────────────────────────────────────────────────── */
    float decay[hid_dim];

    if (params->tau_shared) {
        float decay_scalar = expf(-dt / params->tau[0]);
        for (int i = 0; i < hid_dim; i++) {
            decay[i] = decay_scalar;
        }
    } else {
        for (int i = 0; i < hid_dim; i++) {
            decay[i] = expf(-dt / params->tau[i]);
        }
    }

    /* ─────────────────────────────────────────────────────────────────────
     * Step 5: State update (the "mixer")
     * h_new = (1 - gate) * h_prev * decay + gate * candidate
     *
     * Shapes used: MUL, ADD, CONST(1)
     * ───────────────────────────────────────────────────────────────────── */
    for (int i = 0; i < hid_dim; i++) {
        float retention = (1.0f - gate[i]) * h_prev[i] * decay[i];
        float update = gate[i] * candidate[i];
        h_new[i] = retention + update;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Simplified CfC: Fixed Time Step
 *
 * When dt is constant (e.g., fixed sample rate hardware), the decay
 * can be precomputed and baked into the weights.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * CfC parameters with precomputed decay
 */
typedef struct {
    int input_dim;
    int hidden_dim;

    const float* W_gate;
    const float* b_gate;
    const float* W_cand;
    const float* b_cand;

    const float* decay;     /* Precomputed exp(-dt/tau) [hidden_dim] */

} CfCParamsFixed;

/**
 * trix_cfc_cell_fixed — CfC with precomputed decay
 *
 * Even faster: decay is a frozen constant, not computed at runtime.
 */
static inline void trix_cfc_cell_fixed(
    const float* x,
    const float* h_prev,
    const CfCParamsFixed* params,
    float* h_new
) {
    const int in_dim = params->input_dim;
    const int hid_dim = params->hidden_dim;
    const int concat_dim = in_dim + hid_dim;

    float concat[concat_dim];
    float gate_pre[hid_dim];
    float gate[hid_dim];
    float cand_pre[hid_dim];
    float candidate[hid_dim];

    /* Concatenate */
    memcpy(concat, x, in_dim * sizeof(float));
    memcpy(concat + in_dim, h_prev, hid_dim * sizeof(float));

    /* Gate */
    trix_onnx_gemm(concat, params->W_gate, params->b_gate,
                   gate_pre, 1, hid_dim, concat_dim, 1.0f, 1.0f);
    for (int i = 0; i < hid_dim; i++) {
        gate[i] = trix_onnx_sigmoid(gate_pre[i]);
    }

    /* Candidate */
    trix_onnx_gemm(concat, params->W_cand, params->b_cand,
                   cand_pre, 1, hid_dim, concat_dim, 1.0f, 1.0f);
    for (int i = 0; i < hid_dim; i++) {
        candidate[i] = trix_onnx_tanh(cand_pre[i]);
    }

    /* Mix with precomputed decay */
    for (int i = 0; i < hid_dim; i++) {
        float retention = (1.0f - gate[i]) * h_prev[i] * params->decay[i];
        float update = gate[i] * candidate[i];
        h_new[i] = retention + update;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC Sequence Processing
 *
 * Process a sequence of inputs through the CfC cell.
 * Each step is independent — can be parallelized on GPU/FPGA.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * trix_cfc_forward — Process a sequence
 *
 * @param inputs    Input sequence [seq_len, input_dim]
 * @param seq_len   Number of time steps
 * @param dt        Time delta (constant or array)
 * @param params    Frozen parameters
 * @param h_init    Initial hidden state [hidden_dim] (or NULL for zeros)
 * @param outputs   Output: hidden states [seq_len, hidden_dim]
 * @param h_final   Output: final hidden state [hidden_dim] (or NULL)
 */
static inline void trix_cfc_forward(
    const float* inputs,
    int seq_len,
    float dt,
    const CfCParams* params,
    const float* h_init,
    float* outputs,
    float* h_final
) {
    const int hid_dim = params->hidden_dim;
    const int in_dim = params->input_dim;

    /* Initialize hidden state */
    float h_current[hid_dim];
    if (h_init) {
        memcpy(h_current, h_init, hid_dim * sizeof(float));
    } else {
        memset(h_current, 0, hid_dim * sizeof(float));
    }

    /* Process sequence */
    for (int t = 0; t < seq_len; t++) {
        const float* x_t = inputs + t * in_dim;
        float* out_t = outputs + t * hid_dim;

        trix_cfc_cell(x_t, h_current, dt, params, out_t);
        memcpy(h_current, out_t, hid_dim * sizeof(float));
    }

    /* Return final state if requested */
    if (h_final) {
        memcpy(h_final, h_current, hid_dim * sizeof(float));
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * CfC Output Projection
 *
 * Map hidden state to output (classification, regression, etc.)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    int hidden_dim;
    int output_dim;
    const float* W_out;     /* [hidden_dim, output_dim] for h @ W_out */
    const float* b_out;     /* [output_dim] */
} CfCOutputParams;

/**
 * trix_cfc_output — Project hidden state to output
 */
static inline void trix_cfc_output(
    const float* h,
    const CfCOutputParams* params,
    float* output
) {
    trix_onnx_gemm(h, params->W_out, params->b_out,
                   output, 1, params->output_dim, params->hidden_dim,
                   1.0f, 1.0f);
}

/**
 * trix_cfc_output_softmax — Project to class probabilities
 */
static inline void trix_cfc_output_softmax(
    const float* h,
    const CfCOutputParams* params,
    float* probs
) {
    float logits[params->output_dim];
    trix_cfc_output(h, params, logits);
    trix_onnx_softmax(logits, probs, params->output_dim);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Complete CfC Network (the "19-Neuron Driver")
 *
 * A full CfC network as a single shape. Input → Hidden → Output.
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    CfCParams cell;
    CfCOutputParams output;
} CfCNetwork;

/**
 * trix_cfc_infer — Full inference: sequence → class probabilities
 */
static inline void trix_cfc_infer(
    const float* inputs,
    int seq_len,
    float dt,
    const CfCNetwork* net,
    float* probs
) {
    const int hid_dim = net->cell.hidden_dim;

    /* Process sequence, get final hidden state */
    float h_final[hid_dim];
    float outputs[seq_len * hid_dim];  /* Intermediate states (could discard) */

    trix_cfc_forward(inputs, seq_len, dt, &net->cell, NULL, outputs, h_final);

    /* Project to output probabilities */
    trix_cfc_output_softmax(h_final, &net->output, probs);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Shape Verification Helpers
 *
 * For proving determinism and bounds.
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * Memory footprint of a CfC cell (in bytes)
 */
static inline size_t trix_cfc_memory_footprint(const CfCParams* params) {
    const int in_dim = params->input_dim;
    const int hid_dim = params->hidden_dim;
    const int concat_dim = in_dim + hid_dim;

    size_t weights = 0;
    weights += hid_dim * concat_dim * sizeof(float);  /* W_gate */
    weights += hid_dim * sizeof(float);               /* b_gate */
    weights += hid_dim * concat_dim * sizeof(float);  /* W_cand */
    weights += hid_dim * sizeof(float);               /* b_cand */
    weights += (params->tau_shared ? 1 : hid_dim) * sizeof(float);  /* tau */

    return weights;
}

/**
 * FLOPs per cell forward pass
 */
static inline size_t trix_cfc_flops(const CfCParams* params) {
    const int in_dim = params->input_dim;
    const int hid_dim = params->hidden_dim;
    const int concat_dim = in_dim + hid_dim;

    size_t flops = 0;
    flops += 2 * hid_dim * concat_dim;  /* Gate GEMM */
    flops += hid_dim * 4;               /* Sigmoid (~4 ops) */
    flops += 2 * hid_dim * concat_dim;  /* Candidate GEMM */
    flops += hid_dim * 4;               /* Tanh (~4 ops) */
    flops += hid_dim * 2;               /* Decay exp */
    flops += hid_dim * 5;               /* Mix (1-g, *, *, g*, +) */

    return flops;
}

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_CFC_SHAPES_H */
