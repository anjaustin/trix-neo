/*
 * alpha_device.cuh — EntroMorphic CUDA Device Header
 *
 * The Liquid Physics that runs inside every voxel.
 * This is the GPU-optimized version of the V3 Efficient Species.
 *
 * Architecture: Register-based execution for maximum throughput
 * Memory: 19 neurons per voxel (simplified for benchmark)
 *
 * For full V3 (98 nodes), see alpha_device_full.cuh
 */

#ifndef ALPHA_DEVICE_CUH
#define ALPHA_DEVICE_CUH

#include <math.h>

/* Benchmark Configuration: Simplified 19-neuron network */
#define NODE_COUNT 19

/* ═══════════════════════════════════════════════════════════════════════════
 * DEVICE ACTIVATION FUNCTIONS
 * ═══════════════════════════════════════════════════════════════════════════ */

__device__ __forceinline__ float d_tanh(float x) {
    return tanhf(x);
}

__device__ __forceinline__ float d_sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

__device__ __forceinline__ float d_softsign(float x) {
    return x / (1.0f + fabsf(x));
}

__device__ __forceinline__ float d_relu(float x) {
    return fmaxf(0.0f, x);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * THE LIQUID PHYSICS STEP
 *
 * This simulates a CfC (Closed-form Continuous-time) cell:
 * - Input integration with temporal feedback
 * - Liquid time constants (mixing between neurons)
 * - State evolution with inertia
 *
 * Computational load: ~100 FLOPs per call (approximates real V3)
 * ═══════════════════════════════════════════════════════════════════════════ */

__device__ inline float alpha_step(float input, float* state) {
    /* Load state into local registers for fast access */
    float x[NODE_COUNT];

    #pragma unroll
    for (int i = 0; i < NODE_COUNT; i++) {
        x[i] = state[i];
    }

    /* Temporal feedback from output neuron */
    float feedback = x[NODE_COUNT - 1];

    /* ─────────────────────────────────────────────────────────────────────
     * Layer 1: Input Integration
     * Combines external signal with recurrent feedback
     * ───────────────────────────────────────────────────────────────────── */
    x[0] = d_tanh(input * 0.5f + feedback * 0.1f);

    /* ─────────────────────────────────────────────────────────────────────
     * Layer 2: Liquid Time Constants
     *
     * Each neuron evolves based on:
     *   - Its own inertia (0.9 decay)
     *   - Input from previous neuron (0.1 coupling)
     *   - Nonlinear self-modulation (tanh feedback)
     *
     * This creates the "liquid" behavior where information
     * flows and mixes through the network over time.
     * ───────────────────────────────────────────────────────────────────── */
    #pragma unroll
    for (int i = 1; i < NODE_COUNT; i++) {
        float inertia = x[i] * 0.9f;
        float coupling = x[i - 1] * 0.1f;
        float modulation = d_tanh(x[i] * 0.05f);
        x[i] = inertia + coupling + modulation;
    }

    /* Store back to state array */
    #pragma unroll
    for (int i = 0; i < NODE_COUNT; i++) {
        state[i] = x[i];
    }

    /* Output is the last neuron */
    return x[NODE_COUNT - 1];
}

/* ═══════════════════════════════════════════════════════════════════════════
 * STATE RESET
 * ═══════════════════════════════════════════════════════════════════════════ */

__device__ inline void alpha_reset(float* state) {
    #pragma unroll
    for (int i = 0; i < NODE_COUNT; i++) {
        state[i] = 0.0f;
    }
}

#endif /* ALPHA_DEVICE_CUH */
