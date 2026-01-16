/*
 * 07_cfc_demo.c — CfC Liquid Neural Network Demo
 *
 * Demonstrates:
 *   1. Creating CfC parameters manually
 *   2. Processing a sequence
 *   3. Getting classification output
 *
 * This is what a frozen CfC chip does under the hood.
 *
 * Build:
 *   gcc -O3 -I../include 07_cfc_demo.c -o 07_cfc_demo -lm
 *
 * Run:
 *   ./07_cfc_demo
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "trixc/cfc_shapes.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Example Network: 2 inputs → 4 hidden → 2 outputs
 *
 * This is a tiny network that demonstrates the CfC architecture.
 * In practice, you'd generate these with lnn2trix.py from trained weights.
 * ═══════════════════════════════════════════════════════════════════════════ */

#define INPUT_DIM  2
#define HIDDEN_DIM 4
#define OUTPUT_DIM 2
#define CONCAT_DIM (INPUT_DIM + HIDDEN_DIM)

/* Gate weights: controls how much new info to accept */
static const float W_gate[HIDDEN_DIM * CONCAT_DIM] = {
    /* Neuron 0: responds to input 0 */
    0.5f, 0.0f,  0.1f, 0.0f, 0.0f, 0.0f,
    /* Neuron 1: responds to input 1 */
    0.0f, 0.5f,  0.0f, 0.1f, 0.0f, 0.0f,
    /* Neuron 2: combines inputs */
    0.3f, 0.3f,  0.0f, 0.0f, 0.1f, 0.0f,
    /* Neuron 3: responds to recurrent */
    0.0f, 0.0f,  0.2f, 0.2f, 0.0f, 0.1f,
};
static const float b_gate[HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};

/* Candidate weights: what the new info looks like */
static const float W_cand[HIDDEN_DIM * CONCAT_DIM] = {
    1.0f, 0.0f,  0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f,  0.0f, 0.0f, 0.0f, 0.0f,
    0.5f, 0.5f,  0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f,  0.5f, 0.5f, 0.0f, 0.0f,
};
static const float b_cand[HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};

/* Time constants: how fast each neuron "forgets" */
static const float tau[HIDDEN_DIM] = {0.5f, 1.0f, 2.0f, 4.0f};

/* Output projection: hidden → class logits */
static const float W_out[HIDDEN_DIM * OUTPUT_DIM] = {
    1.0f, -1.0f,   /* Neuron 0 → positive for class 0 */
    -1.0f, 1.0f,   /* Neuron 1 → positive for class 1 */
    0.5f, 0.5f,    /* Neuron 2 → contributes to both */
    0.0f, 0.0f,    /* Neuron 3 → no direct contribution */
};
static const float b_out[OUTPUT_DIM] = {0.0f, 0.0f};

/* ═══════════════════════════════════════════════════════════════════════════
 * Demo Functions
 * ═══════════════════════════════════════════════════════════════════════════ */

void print_array(const char* name, const float* arr, int n) {
    printf("  %s: [", name);
    for (int i = 0; i < n; i++) {
        printf("%.3f%s", arr[i], i < n-1 ? ", " : "");
    }
    printf("]\n");
}

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  CfC Liquid Neural Network Demo                              │\n");
    printf("│  \"Solid-state fluid dynamics in action\"                     │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");
    printf("\n");

    /* Build parameter structs */
    CfCParams cell_params = {
        .input_dim = INPUT_DIM,
        .hidden_dim = HIDDEN_DIM,
        .W_gate = W_gate,
        .b_gate = b_gate,
        .W_cand = W_cand,
        .b_cand = b_cand,
        .tau = tau,
        .tau_shared = 0,  /* Per-neuron tau */
    };

    CfCOutputParams output_params = {
        .hidden_dim = HIDDEN_DIM,
        .output_dim = OUTPUT_DIM,
        .W_out = W_out,
        .b_out = b_out,
    };

    printf("Network architecture:\n");
    printf("  Input:  %d dimensions\n", INPUT_DIM);
    printf("  Hidden: %d neurons (CfC cells)\n", HIDDEN_DIM);
    printf("  Output: %d classes\n", OUTPUT_DIM);
    printf("  Memory: %zu bytes\n", trix_cfc_memory_footprint(&cell_params));
    printf("  FLOPs:  %zu per step\n", trix_cfc_flops(&cell_params));
    printf("\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Demo 1: Single step with constant input
     * ───────────────────────────────────────────────────────────────────── */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Demo 1: Single Step\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    float h[HIDDEN_DIM] = {0};  /* Start with zero state */
    float x[INPUT_DIM] = {1.0f, 0.0f};  /* Input: [1, 0] */
    float h_new[HIDDEN_DIM];

    printf("Input: [1.0, 0.0]\n");
    printf("Initial state: [0, 0, 0, 0]\n\n");

    trix_cfc_cell(x, h, 0.1f, &cell_params, h_new);

    printf("After one step (dt=0.1):\n");
    print_array("h", h_new, HIDDEN_DIM);
    printf("\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Demo 2: Process a sequence (sine wave)
     * ───────────────────────────────────────────────────────────────────── */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Demo 2: Sequence Processing (sine wave input)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    #define SEQ_LEN 20
    float inputs[SEQ_LEN * INPUT_DIM];
    float outputs[SEQ_LEN * HIDDEN_DIM];
    float h_final[HIDDEN_DIM];

    /* Generate sine wave input */
    printf("Input sequence (sin/cos wave):\n");
    for (int t = 0; t < SEQ_LEN; t++) {
        inputs[t * INPUT_DIM + 0] = sinf(t * 0.3f);
        inputs[t * INPUT_DIM + 1] = cosf(t * 0.3f);
        if (t < 5 || t >= SEQ_LEN - 2) {
            printf("  t=%2d: [%6.3f, %6.3f]\n", t,
                   inputs[t * INPUT_DIM + 0], inputs[t * INPUT_DIM + 1]);
        } else if (t == 5) {
            printf("  ...\n");
        }
    }
    printf("\n");

    /* Process sequence */
    trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &cell_params, NULL, outputs, h_final);

    printf("Final hidden state:\n");
    print_array("h_final", h_final, HIDDEN_DIM);
    printf("\n");

    /* Get classification */
    float probs[OUTPUT_DIM];
    trix_cfc_output_softmax(h_final, &output_params, probs);

    printf("Classification:\n");
    printf("  Class 0: %.1f%%\n", probs[0] * 100);
    printf("  Class 1: %.1f%%\n", probs[1] * 100);
    printf("\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Demo 3: Different time constants = different memory
     * ───────────────────────────────────────────────────────────────────── */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Demo 3: Time Constants (Memory Decay)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Time constants (τ): [0.5, 1.0, 2.0, 4.0]\n");
    printf("Larger τ = slower decay = longer memory\n\n");

    /* Apply impulse and watch decay */
    float h_decay[HIDDEN_DIM] = {1.0f, 1.0f, 1.0f, 1.0f};
    float x_zero[INPUT_DIM] = {0.0f, 0.0f};

    printf("Starting state: [1, 1, 1, 1]\n");
    printf("Input: [0, 0] (impulse then nothing)\n\n");

    printf("Decay over time:\n");
    for (int t = 0; t < 10; t++) {
        trix_cfc_cell(x_zero, h_decay, 0.5f, &cell_params, h_new);
        memcpy(h_decay, h_new, sizeof(h_decay));

        printf("  t=%d: [%.3f, %.3f, %.3f, %.3f]\n", t,
               h_decay[0], h_decay[1], h_decay[2], h_decay[3]);
    }
    printf("\n");
    printf("Notice: Neuron 3 (τ=4.0) decays slowest\n");
    printf("\n");

    /* ─────────────────────────────────────────────────────────────────────
     * Demo 4: Determinism check
     * ───────────────────────────────────────────────────────────────────── */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Demo 4: Determinism Verification\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    float h1_final[HIDDEN_DIM], h2_final[HIDDEN_DIM];
    float outputs1[SEQ_LEN * HIDDEN_DIM], outputs2[SEQ_LEN * HIDDEN_DIM];

    trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &cell_params, NULL, outputs1, h1_final);
    trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &cell_params, NULL, outputs2, h2_final);

    int match = 1;
    for (int i = 0; i < HIDDEN_DIM; i++) {
        if (h1_final[i] != h2_final[i]) match = 0;
    }

    printf("Run 1 final state: [%.6f, %.6f, %.6f, %.6f]\n",
           h1_final[0], h1_final[1], h1_final[2], h1_final[3]);
    printf("Run 2 final state: [%.6f, %.6f, %.6f, %.6f]\n",
           h2_final[0], h2_final[1], h2_final[2], h2_final[3]);
    printf("\n");
    printf("Identical? %s\n", match ? "YES ✓ (Deterministic)" : "NO ✗ (BUG!)");
    printf("\n");

    printf("═══════════════════════════════════════════════════════════════\n");
    printf("Demo complete.\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    return 0;
}
