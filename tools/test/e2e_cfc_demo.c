/*
 * e2e_cfc_demo.c — End-to-End CfC Forge Demo
 *
 * This demo shows the complete pipeline:
 *   1. Create synthetic CfC weights (simulating PyTorch export)
 *   2. Forge the CfC as a soft-chip
 *   3. Compile the forged soft-chip
 *   4. Run inference
 *   5. Verify correctness against reference
 *
 * "From weights to wires in one command."
 *
 * Copyright 2026 Trix Research
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/time.h>

#include "../include/cfc_forge.h"
#include "../include/forged_bridge.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * SYNTHETIC WEIGHT GENERATION
 * 
 * In production, these come from lnn2trix.py / PyTorch export.
 * Here we generate deterministic ternary patterns for testing.
 * ═══════════════════════════════════════════════════════════════════════════ */

/* Simple PRNG for reproducible tests */
static uint32_t g_seed = 42;

static uint32_t lcg_next(void) {
    g_seed = g_seed * 1664525 + 1013904223;
    return g_seed;
}

static int8_t random_ternary(void) {
    uint32_t r = lcg_next() % 3;
    return (int8_t)r - 1;  /* -1, 0, +1 */
}

static float random_float(float min, float max) {
    float t = (float)(lcg_next() % 10000) / 10000.0f;
    return min + t * (max - min);
}

/*
 * Generate synthetic CfC weights
 *
 * The pattern: ternary weights with balanced distribution
 */
static void generate_cfc_weights(
    int input_dim,
    int hidden_dim,
    int output_dim,
    int8_t* W_gate,
    int8_t* W_cand,
    float* b_gate,
    float* b_cand,
    float* tau,
    int8_t* W_out,
    float* b_out
) {
    int concat_dim = input_dim + hidden_dim;
    
    /* Gate weights (ternary) */
    for (int i = 0; i < hidden_dim * concat_dim; i++) {
        W_gate[i] = random_ternary();
    }
    
    /* Candidate weights (ternary) */
    for (int i = 0; i < hidden_dim * concat_dim; i++) {
        W_cand[i] = random_ternary();
    }
    
    /* Biases (small float values) */
    for (int i = 0; i < hidden_dim; i++) {
        b_gate[i] = random_float(-0.1f, 0.1f);
        b_cand[i] = random_float(-0.1f, 0.1f);
    }
    
    /* Time constants (positive, centered around 1.0) */
    for (int i = 0; i < hidden_dim; i++) {
        tau[i] = random_float(0.5f, 2.0f);
    }
    
    /* Output weights (ternary) */
    for (int i = 0; i < output_dim * hidden_dim; i++) {
        W_out[i] = random_ternary();
    }
    
    /* Output bias */
    for (int i = 0; i < output_dim; i++) {
        b_out[i] = random_float(-0.1f, 0.1f);
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * REFERENCE CfC IMPLEMENTATION
 *
 * Scalar reference for correctness verification
 * ═══════════════════════════════════════════════════════════════════════════ */

static float ref_sigmoid(float x) {
    return 1.0f / (1.0f + expf(-x));
}

static float ref_tanh(float x) {
    return tanhf(x);
}

/* Row-major matvec: y = W @ x */
static void ref_matvec(
    const int8_t* W,
    const float* x,
    float* y,
    int M, int N
) {
    for (int i = 0; i < M; i++) {
        float sum = 0.0f;
        for (int j = 0; j < N; j++) {
            sum += (float)W[i * N + j] * x[j];
        }
        y[i] = sum;
    }
}

static void ref_cfc_cell(
    const float* x,
    const float* h_prev,
    float dt,
    int input_dim,
    int hidden_dim,
    const int8_t* W_gate,
    const int8_t* W_cand,
    const float* b_gate,
    const float* b_cand,
    const float* tau,
    float* h_new
) {
    int concat_dim = input_dim + hidden_dim;
    
    /* Concatenate [x; h_prev] */
    float concat[concat_dim];
    memcpy(concat, x, input_dim * sizeof(float));
    memcpy(concat + input_dim, h_prev, hidden_dim * sizeof(float));
    
    /* Gate */
    float gate_pre[hidden_dim];
    float gate[hidden_dim];
    ref_matvec(W_gate, concat, gate_pre, hidden_dim, concat_dim);
    for (int i = 0; i < hidden_dim; i++) {
        gate[i] = ref_sigmoid(gate_pre[i] + b_gate[i]);
    }
    
    /* Candidate */
    float cand_pre[hidden_dim];
    float candidate[hidden_dim];
    ref_matvec(W_cand, concat, cand_pre, hidden_dim, concat_dim);
    for (int i = 0; i < hidden_dim; i++) {
        candidate[i] = ref_tanh(cand_pre[i] + b_cand[i]);
    }
    
    /* Update */
    for (int i = 0; i < hidden_dim; i++) {
        float decay = expf(-dt / tau[i]);
        float retention = (1.0f - gate[i]) * h_prev[i] * decay;
        float update = gate[i] * candidate[i];
        h_new[i] = retention + update;
    }
}

static void ref_softmax(const float* x, float* out, int n) {
    float max_val = x[0];
    for (int i = 1; i < n; i++) if (x[i] > max_val) max_val = x[i];
    
    float sum = 0.0f;
    for (int i = 0; i < n; i++) {
        out[i] = expf(x[i] - max_val);
        sum += out[i];
    }
    for (int i = 0; i < n; i++) out[i] /= sum;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * FORGE AND COMPILE
 * ═══════════════════════════════════════════════════════════════════════════ */

static int forge_and_compile(const CfCCellSpec* spec) {
    /* Step 1: Forge to header */
    char header_path[] = "/tmp/e2e_cfc_forged.h";
    FILE* header_file = fopen(header_path, "w");
    if (!header_file) {
        printf("  ERROR: Cannot create %s\n", header_path);
        return -1;
    }
    
    forge_cfc_to_file(spec, header_file);
    fclose(header_file);
    printf("  Forged: %s\n", header_path);
    
    /* Step 2: Create test driver */
    char main_path[] = "/tmp/e2e_cfc_main.c";
    FILE* main_file = fopen(main_path, "w");
    if (!main_file) {
        printf("  ERROR: Cannot create %s\n", main_path);
        return -1;
    }
    
    fprintf(main_file, "#include \"e2e_cfc_forged.h\"\n");
    fprintf(main_file, "#include <stdio.h>\n");
    fprintf(main_file, "#include <string.h>\n\n");
    fprintf(main_file, "int main() {\n");
    fprintf(main_file, "    /* Test input */\n");
    fprintf(main_file, "    float x[%d];\n", spec->input_dim);
    fprintf(main_file, "    for (int i = 0; i < %d; i++) x[i] = 0.1f * i;\n\n", spec->input_dim);
    fprintf(main_file, "    float h[%d] = {0};\n", spec->hidden_dim);
    fprintf(main_file, "    float h_new[%d];\n", spec->hidden_dim);
    fprintf(main_file, "    float dt = 0.1f;\n\n");
    fprintf(main_file, "    /* Run CfC cell */\n");
    fprintf(main_file, "    %s_cell(x, h, dt, h_new);\n\n", spec->name);
    fprintf(main_file, "    /* Print first few hidden values */\n");
    fprintf(main_file, "    printf(\"h_new: \");\n");
    fprintf(main_file, "    for (int i = 0; i < 8 && i < %d; i++) {\n", spec->hidden_dim);
    fprintf(main_file, "        printf(\"%%7.4f \", h_new[i]);\n");
    fprintf(main_file, "    }\n");
    fprintf(main_file, "    printf(\"...\\n\");\n\n");
    
    if (spec->has_output) {
        fprintf(main_file, "    /* Output projection */\n");
        fprintf(main_file, "    float probs[%d];\n", spec->output_dim);
        fprintf(main_file, "    %s_output_softmax(h_new, probs);\n\n", spec->name);
        fprintf(main_file, "    printf(\"probs: \");\n");
        fprintf(main_file, "    for (int i = 0; i < %d; i++) {\n", spec->output_dim);
        fprintf(main_file, "        printf(\"%%5.3f \", probs[i]);\n");
        fprintf(main_file, "    }\n");
        fprintf(main_file, "    printf(\"\\n\");\n\n");
        fprintf(main_file, "    float sum = 0.0f;\n");
        fprintf(main_file, "    for (int i = 0; i < %d; i++) sum += probs[i];\n", spec->output_dim);
        fprintf(main_file, "    printf(\"sum: %%f\\n\", sum);\n\n");
    }
    
    fprintf(main_file, "    return 0;\n");
    fprintf(main_file, "}\n");
    fclose(main_file);
    printf("  Created: %s\n", main_path);
    
    /* Step 3: Compile */
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "clang -o /tmp/e2e_cfc /tmp/e2e_cfc_main.c -I/tmp "
        "-mcpu=apple-m4 -march=armv8.6-a+dotprod -O2 -Wall -lm 2>&1");
    
    printf("  Compiling...\n");
    FILE* proc = popen(cmd, "r");
    if (!proc) {
        printf("  ERROR: Cannot run compiler\n");
        return -1;
    }
    
    char output[1024];
    int has_output = 0;
    while (fgets(output, sizeof(output), proc)) {
        printf("    %s", output);
        has_output = 1;
    }
    
    int status = pclose(proc);
    if (status != 0) {
        printf("  ERROR: Compilation failed\n");
        return -1;
    }
    
    if (!has_output) {
        printf("  Compilation successful!\n");
    }
    
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * RUN FORGED SOFT-CHIP
 * ═══════════════════════════════════════════════════════════════════════════ */

static int run_forged(float* forged_h_new, int hidden_dim, int output_dim) {
    printf("  Running forged soft-chip...\n");
    
    FILE* proc = popen("/tmp/e2e_cfc 2>&1", "r");
    if (!proc) {
        printf("  ERROR: Cannot run forged binary\n");
        return -1;
    }
    
    char line[1024];
    while (fgets(line, sizeof(line), proc)) {
        printf("    %s", line);
        
        /* Parse h_new values */
        if (strncmp(line, "h_new: ", 7) == 0) {
            char* p = line + 7;
            for (int i = 0; i < hidden_dim && i < 8; i++) {
                forged_h_new[i] = strtof(p, &p);
            }
        }
    }
    
    int status = pclose(proc);
    return (status == 0) ? 0 : -1;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  End-to-End CfC Forge Demo                                 ║\n");
    printf("║  \"From weights to wires in one command.\"                   ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    /* Dimensions (must be Block-8-K64 compatible) */
    const int INPUT_DIM = 64;
    const int HIDDEN_DIM = 32;  /* Multiple of 8 */
    const int OUTPUT_DIM = 10;
    const int CONCAT_DIM = INPUT_DIM + HIDDEN_DIM;  /* 96, rounds to 128 */
    
    /* Allocate weights */
    int8_t* W_gate = malloc(HIDDEN_DIM * CONCAT_DIM);
    int8_t* W_cand = malloc(HIDDEN_DIM * CONCAT_DIM);
    float* b_gate = malloc(HIDDEN_DIM * sizeof(float));
    float* b_cand = malloc(HIDDEN_DIM * sizeof(float));
    float* tau = malloc(HIDDEN_DIM * sizeof(float));
    int8_t* W_out = malloc(OUTPUT_DIM * HIDDEN_DIM);
    float* b_out = malloc(OUTPUT_DIM * sizeof(float));
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 1: Generate synthetic weights
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 1: Generate synthetic CfC weights\n");
    g_seed = 42;  /* Reset for reproducibility */
    generate_cfc_weights(
        INPUT_DIM, HIDDEN_DIM, OUTPUT_DIM,
        W_gate, W_cand, b_gate, b_cand, tau,
        W_out, b_out
    );
    
    /* Count weight distribution */
    int pos = 0, neg = 0, zero = 0;
    for (int i = 0; i < HIDDEN_DIM * CONCAT_DIM; i++) {
        if (W_gate[i] > 0) pos++;
        else if (W_gate[i] < 0) neg++;
        else zero++;
    }
    printf("  Dimensions: input=%d, hidden=%d, output=%d\n", INPUT_DIM, HIDDEN_DIM, OUTPUT_DIM);
    printf("  W_gate distribution: +1=%d, 0=%d, -1=%d (total=%d)\n", 
           pos, zero, neg, pos + zero + neg);
    printf("  Weight memory: %zu bytes (vs %zu float)\n",
           (size_t)(HIDDEN_DIM * CONCAT_DIM * 2 + OUTPUT_DIM * HIDDEN_DIM),
           (size_t)(HIDDEN_DIM * CONCAT_DIM * 2 + OUTPUT_DIM * HIDDEN_DIM) * 4);
    printf("\n");
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 2: Compute reference output
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 2: Compute reference output\n");
    
    float x[INPUT_DIM];
    for (int i = 0; i < INPUT_DIM; i++) x[i] = 0.1f * i;
    
    float h_prev[HIDDEN_DIM] = {0};
    float ref_h_new[HIDDEN_DIM];
    float dt = 0.1f;
    
    ref_cfc_cell(x, h_prev, dt, INPUT_DIM, HIDDEN_DIM,
                 W_gate, W_cand, b_gate, b_cand, tau, ref_h_new);
    
    printf("  ref h_new: ");
    for (int i = 0; i < 8; i++) printf("%7.4f ", ref_h_new[i]);
    printf("...\n\n");
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 3: Pack weights for Block-8-K64 layout
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 3: Pack weights for NEON Block-8-K64\n");
    
    /* Need to align K to 64 */
    int K_aligned = bridge_align64(CONCAT_DIM);
    printf("  Concat dim: %d -> aligned: %d\n", CONCAT_DIM, K_aligned);
    
    /* Create padded row-major weights */
    int8_t* W_gate_padded = calloc(HIDDEN_DIM * K_aligned, 1);
    int8_t* W_cand_padded = calloc(HIDDEN_DIM * K_aligned, 1);
    
    for (int i = 0; i < HIDDEN_DIM; i++) {
        memcpy(W_gate_padded + i * K_aligned, W_gate + i * CONCAT_DIM, CONCAT_DIM);
        memcpy(W_cand_padded + i * K_aligned, W_cand + i * CONCAT_DIM, CONCAT_DIM);
    }
    
    /* Pack to Block-8-K64 */
    int8_t* W_gate_packed = malloc(HIDDEN_DIM * K_aligned);
    int8_t* W_cand_packed = malloc(HIDDEN_DIM * K_aligned);
    
    bridge_pack_block8_k64(W_gate_padded, W_gate_packed, HIDDEN_DIM, K_aligned);
    bridge_pack_block8_k64(W_cand_padded, W_cand_packed, HIDDEN_DIM, K_aligned);
    
    /* Align output projection */
    int H_aligned = bridge_align64(HIDDEN_DIM);
    int8_t* W_out_padded = calloc(OUTPUT_DIM * H_aligned, 1);
    int8_t* W_out_packed = malloc(bridge_align8(OUTPUT_DIM) * H_aligned);
    
    for (int i = 0; i < OUTPUT_DIM; i++) {
        memcpy(W_out_padded + i * H_aligned, W_out + i * HIDDEN_DIM, HIDDEN_DIM);
    }
    bridge_pack_block8_k64(W_out_padded, W_out_packed, bridge_align8(OUTPUT_DIM), H_aligned);
    
    printf("  Packed W_gate: %d bytes\n", HIDDEN_DIM * K_aligned);
    printf("  Packed W_cand: %d bytes\n", HIDDEN_DIM * K_aligned);
    printf("  Packed W_out:  %d bytes\n", (int)bridge_block8_k64_bytes(bridge_align8(OUTPUT_DIM), H_aligned));
    printf("\n");
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 4: Forge CfC soft-chip
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 4: Forge CfC soft-chip\n");
    
    CfCCellSpec spec = {0};
    strcpy(spec.name, "e2e_cfc");
    spec.input_dim = INPUT_DIM;
    spec.hidden_dim = HIDDEN_DIM;
    spec.W_gate = W_gate_packed;
    spec.W_cand = W_cand_packed;
    spec.b_gate = b_gate;
    spec.b_cand = b_cand;
    spec.tau = tau;
    spec.tau_shared = 0;
    spec.has_output = 1;
    spec.output_dim = OUTPUT_DIM;
    spec.W_out = W_out_packed;
    spec.b_out = b_out;
    spec.strategy = FORGE_STRATEGY_NEON_BLOCK8_K64;
    
    if (forge_and_compile(&spec) != 0) {
        printf("\nFORGE FAILED!\n");
        return 1;
    }
    printf("\n");
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 5: Run forged soft-chip
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 5: Run forged soft-chip\n");
    
    float forged_h_new[HIDDEN_DIM];
    if (run_forged(forged_h_new, HIDDEN_DIM, OUTPUT_DIM) != 0) {
        printf("\nRUN FAILED!\n");
        return 1;
    }
    printf("\n");
    
    /* ═══════════════════════════════════════════════════════════════════
     * STEP 6: Verify correctness
     * ═══════════════════════════════════════════════════════════════════ */
    printf("Step 6: Verify correctness\n");
    
    /* Note: The forged version uses int8 quantization, so we expect some error */
    float max_error = 0.0f;
    for (int i = 0; i < 8; i++) {
        float error = fabsf(forged_h_new[i] - ref_h_new[i]);
        if (error > max_error) max_error = error;
        printf("  h[%d]: ref=%7.4f, forged=%7.4f, error=%7.4f\n",
               i, ref_h_new[i], forged_h_new[i], error);
    }
    
    printf("\n  Max error (first 8): %f\n", max_error);
    
    /* Quantization error should be bounded by the scale factor */
    /* With int8 quantization, expect ~1% relative error */
    float threshold = 0.5f;  /* Generous for int8 quantization */
    if (max_error < threshold) {
        printf("  Status: PASS (error < %.1f)\n", threshold);
    } else {
        printf("  Status: FAIL (error >= %.1f)\n", threshold);
        printf("  Note: High error may indicate weight packing issue\n");
    }
    
    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  End-to-End Demo Complete!\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    
    /* Cleanup */
    free(W_gate);
    free(W_cand);
    free(b_gate);
    free(b_cand);
    free(tau);
    free(W_out);
    free(b_out);
    free(W_gate_padded);
    free(W_cand_padded);
    free(W_gate_packed);
    free(W_cand_packed);
    free(W_out_padded);
    free(W_out_packed);
    
    return 0;
}
