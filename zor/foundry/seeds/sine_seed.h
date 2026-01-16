/*
 * sine_seed.h — EntroMorphic Frozen Seed
 *
 * Target: Time-series prediction (noisy sine wave)
 * Generated: Fri Jan 16 04:03:59 2026
 * Fitness: 3031.00
 * Generation: 258
 * Mutations: 1544798
 *
 * Usage:
 *   #include "sine_seed.h"
 *   float output = sine_seed_step(input, state);
 */

#ifndef sine_SEED_H
#define sine_SEED_H

#include <stdint.h>
#include <string.h>
#include <math.h>

/* Seed Metadata */
#define sine_SEED_NODES 98
#define sine_SEED_REGS 99
#define sine_SEED_FITNESS 3031.00f
#define sine_SEED_GEN 258

/* Frozen Node Data */
static const uint8_t sine_seed_opcodes[98] = {
    13, 22, 8, 20, 28, 9, 10, 10, 8, 13, 23, 22, 20, 28, 9, 10, 
    22, 8, 13, 24, 8, 23, 10, 23, 23, 10, 10, 13, 24, 8, 11, 28, 
    22, 10, 10, 8, 22, 10, 8, 11, 8, 20, 10, 10, 8, 10, 24, 8, 
    11, 8, 9, 10, 8, 8, 13, 24, 8, 11, 28, 8, 10, 10, 22, 13, 
    10, 22, 11, 28, 21, 10, 10, 8, 24, 8, 10, 8, 23, 10, 8, 21, 
    10, 8, 24, 10, 8, 24, 10, 8, 24, 23, 8, 24, 10, 8, 24, 10, 
    8, 23
};

static const uint8_t sine_seed_flags[98] = {
    1, 1, 1, 1, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 
    1, 0, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 0, 0, 1, 1, 1, 1, 
    1, 1, 1, 1, 0, 1, 0, 1, 1, 1, 1, 0, 1, 0, 1, 1, 
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 
    1, 1
};

static const uint16_t sine_seed_out_idx[98] = {
    10, 11, 12, 13, 14, 15, 16, 17, 2, 18, 19, 20, 21, 22, 23, 24, 
    25, 3, 26, 27, 28, 29, 30, 31, 32, 33, 4, 34, 35, 36, 37, 38, 
    39, 40, 41, 5, 42, 43, 44, 45, 46, 47, 48, 49, 6, 50, 51, 52, 
    53, 54, 55, 56, 57, 7, 58, 59, 60, 61, 62, 63, 64, 65, 8, 66, 
    67, 68, 69, 70, 71, 72, 73, 9, 74, 75, 76, 77, 78, 79, 80, 81, 
    82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 
    98, 1
};

static const uint32_t sine_seed_in_a[98] = {
    0, 28, 11, 84, 2, 11, 53, 72, 
    16, 93, 11, 19, 21, 94, 76, 23, 
    87, 68, 0, 0, 27, 55, 46, 56, 
    65, 29, 81, 17, 67, 35, 88, 28, 
    3, 42, 26, 40, 0, 50, 43, 43, 
    10, 94, 47, 61, 9, 57, 0, 35, 
    0, 48, 27, 84, 55, 12, 94, 59, 
    21, 37, 72, 2, 6, 81, 37, 66, 
    57, 67, 91, 83, 59, 35, 69, 72, 
    68, 51, 8, 52, 70, 14, 98, 0, 
    7, 8, 0, 69, 83, 0, 6, 86, 
    3, 74, 89, 53, 34, 92, 76, 42, 
    95, 98
};

static const uint32_t sine_seed_in_b[98] = {
    0, 0, 10, 12, 0, 13, 14, 0, 
    17, 0, 0, 18, 20, 0, 21, 22, 
    0, 25, 0, 0, 26, 28, 0, 29, 
    30, 0, 33, 0, 0, 34, 36, 0, 
    37, 38, 0, 41, 0, 0, 42, 44, 
    0, 45, 46, 0, 49, 0, 0, 50, 
    52, 0, 53, 54, 0, 57, 0, 0, 
    58, 60, 0, 61, 62, 0, 65, 0, 
    0, 66, 68, 0, 69, 70, 0, 73, 
    0, 0, 75, 76, 0, 78, 79, 0, 
    81, 82, 0, 84, 85, 0, 87, 88, 
    0, 90, 91, 0, 93, 94, 0, 96, 
    97, 0
};

static const float sine_seed_values[98] = {
    0.000000f, 0.812404f, 0.312514f, 0.002990f, 3.969114f, 0.000000f, 
    0.020997f, -0.057013f, -0.116287f, 0.160995f, 1.000000f, 0.004484f, 
    0.000000f, 3.956388f, -0.096493f, -0.114043f, 0.440195f, 0.289676f, 
    0.174795f, 1.201956f, -0.060525f, -0.125149f, 0.871576f, -0.409575f, 
    0.000000f, 0.000000f, 0.000000f, 0.119934f, 1.000000f, 0.000000f, 
    0.000000f, 2.784023f, 0.000000f, 0.029559f, 0.385703f, 0.145949f, 
    -0.071396f, 1.000000f, 0.000000f, 0.228698f, 1.860647f, 0.000000f, 
    0.000000f, -0.282034f, -0.284911f, -0.322442f, 1.318188f, 0.183859f, 
    -0.036071f, 3.230340f, 0.026898f, 0.000000f, 0.000000f, 0.000000f, 
    0.160816f, 1.000000f, 0.184567f, -0.156221f, 2.381856f, 0.093377f, 
    -0.129973f, 0.283780f, 0.020606f, -0.284711f, 1.124220f, 0.000000f, 
    0.000000f, 3.521952f, 0.249256f, -0.106973f, 0.039057f, 0.000000f, 
    0.000000f, -0.209754f, -0.237289f, 0.249955f, -0.484246f, 0.072948f, 
    0.022860f, 0.051127f, -0.098567f, 0.171048f, 0.317400f, -0.298830f, 
    -0.167598f, -0.454950f, 0.000000f, 0.000000f, 0.361033f, 0.072261f, 
    -0.429103f, -0.255568f, -0.224229f, -0.042731f, 0.444433f, 0.000000f, 
    -0.137852f, 0.000000f
};

/* Execute one step of the seed */
static inline float sine_seed_step(float input, float* state) {
    float regs[99];
    memcpy(regs, state, 99 * sizeof(float));
    regs[0] = input;  /* Input register */

    /* Execute all active nodes */
    for (int i = 0; i < 98; i++) {
        if (!(sine_seed_flags[i] & 0x01)) continue;
        float a = regs[sine_seed_in_a[i]];
        float b = regs[sine_seed_in_b[i]];
        float v = sine_seed_values[i];
        float result;
        switch (sine_seed_opcodes[i]) {
            case 8:  result = a + b; break;  /* ADD */
            case 9:  result = a - b; break;  /* SUB */
            case 10: result = a * b; break;  /* MUL */
            case 11: result = b != 0 ? a/b : 0; break;  /* DIV */
            case 13: result = fabsf(a); break;  /* ABS */
            case 14: result = a > b ? a : b; break;  /* MAX */
            case 16: result = expf(a); break;  /* EXP */
            case 20: result = 1.0f/(1.0f+expf(-a)); break;  /* SIGMOID */
            case 21: result = tanhf(a); break;  /* TANH */
            case 22: result = a > 0 ? a : 0; break;  /* RELU */
            case 23: result = a/(1.0f+fabsf(a)); break;  /* SOFTSIGN */
            case 24: result = v; break;  /* CONST */
            case 27: result = a * expf(-0.1f/v); break;  /* DECAY */
            default: result = 0; break;
        }
        regs[sine_seed_out_idx[i]] = result;
    }

    /* Save state */
    memcpy(state, regs, 99 * sizeof(float));
    return regs[1];  /* Output register */
}

/* Reset seed state */
static inline void sine_seed_reset(float* state) {
    memset(state, 0, 99 * sizeof(float));
}

#endif /* sine_SEED_H */
