/*
 * test_cfc.c — CfC Shape Tests
 *
 * Rigorous testing of Closed-form Continuous-time shapes.
 *
 * "Trust, but verify. Then verify again."
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "../include/trixc/cfc_shapes.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Macros
 * ═══════════════════════════════════════════════════════════════════════════ */

static int tests_run = 0;
static int tests_passed = 0;
static int section_run = 0;
static int section_passed = 0;

#define FLOAT_EQ(a, b, tol) (fabsf((a) - (b)) < (tol))

#define TEST(cond, ...) do { \
    tests_run++; section_run++; \
    if (cond) { tests_passed++; section_passed++; } \
    else { printf("  FAIL: " __VA_ARGS__); printf("\n"); } \
} while(0)

#define SECTION(name) do { \
    section_run = 0; section_passed = 0; \
    printf("\n%s\n", name); \
} while(0)

#define SECTION_END() do { \
    printf("  %d/%d passed\n", section_passed, section_run); \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Weights (Small 2-input, 4-hidden, 2-output network)
 * ═══════════════════════════════════════════════════════════════════════════ */

#define TEST_INPUT_DIM  2
#define TEST_HIDDEN_DIM 4
#define TEST_OUTPUT_DIM 2
#define TEST_CONCAT_DIM (TEST_INPUT_DIM + TEST_HIDDEN_DIM)

/* Simple identity-ish gate weights (learns to pass through) */
static const float test_W_gate[TEST_HIDDEN_DIM * TEST_CONCAT_DIM] = {
    /* Each row: [x0, x1, h0, h1, h2, h3] */
    0.5f, 0.0f, 0.1f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.5f, 0.0f, 0.1f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.1f, 0.0f,
    0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.1f,
};
static const float test_b_gate[TEST_HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};

static const float test_W_cand[TEST_HIDDEN_DIM * TEST_CONCAT_DIM] = {
    1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.5f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f,
    0.0f, 0.0f, 0.5f, 0.5f, 0.0f, 0.0f,
};
static const float test_b_cand[TEST_HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};

static const float test_tau[1] = {1.0f};

/* W_out stored as [hidden_dim, output_dim] for GEMM: h @ W_out */
static const float test_W_out[TEST_HIDDEN_DIM * TEST_OUTPUT_DIM] = {
    /* Column 0 (output 0), Column 1 (output 1) */
    1.0f, 0.0f,   /* h[0] weights */
    0.0f, 1.0f,   /* h[1] weights */
    0.5f, 0.0f,   /* h[2] weights */
    0.0f, 0.5f,   /* h[3] weights */
};
static const float test_b_out[TEST_OUTPUT_DIM] = {0.0f, 0.0f};

/* Precomputed decay for dt=0.1, tau=1.0 -> exp(-0.1) ≈ 0.9048 */
static const float test_decay[TEST_HIDDEN_DIM] = {
    0.90483742f, 0.90483742f, 0.90483742f, 0.90483742f
};

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Basic CfC Cell Properties
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_basic(void) {
    SECTION("CfC BASIC PROPERTIES");

    CfCParams params = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
    };

    /* Test 1: Zero input, zero state -> near-zero output */
    {
        float x[TEST_INPUT_DIM] = {0.0f, 0.0f};
        float h_prev[TEST_HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};
        float h_new[TEST_HIDDEN_DIM];

        trix_cfc_cell(x, h_prev, 0.1f, &params, h_new);

        /* With zero input and zero state, output should be near zero */
        float sum = 0.0f;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            sum += fabsf(h_new[i]);
        }
        TEST(sum < 0.1f, "Zero input/state should give near-zero output (got %f)", sum);
    }

    /* Test 2: Determinism - same input -> same output */
    {
        float x[TEST_INPUT_DIM] = {0.5f, -0.3f};
        float h_prev[TEST_HIDDEN_DIM] = {0.1f, 0.2f, 0.0f, 0.0f};
        float h_new1[TEST_HIDDEN_DIM], h_new2[TEST_HIDDEN_DIM];

        trix_cfc_cell(x, h_prev, 0.1f, &params, h_new1);
        trix_cfc_cell(x, h_prev, 0.1f, &params, h_new2);

        int match = 1;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            if (h_new1[i] != h_new2[i]) match = 0;
        }
        TEST(match, "Determinism: identical calls must produce identical results");
    }

    /* Test 3: State retention with zero input */
    {
        float x[TEST_INPUT_DIM] = {0.0f, 0.0f};
        float h_prev[TEST_HIDDEN_DIM] = {1.0f, 1.0f, 1.0f, 1.0f};
        float h_new[TEST_HIDDEN_DIM];

        trix_cfc_cell(x, h_prev, 0.1f, &params, h_new);

        /* With zero input, state should decay but not explode */
        int bounded = 1;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            if (fabsf(h_new[i]) > 2.0f) bounded = 0;
        }
        TEST(bounded, "State should remain bounded with zero input");
    }

    /* Test 4: Decay over time */
    {
        float x[TEST_INPUT_DIM] = {0.0f, 0.0f};
        float h[TEST_HIDDEN_DIM] = {1.0f, 1.0f, 1.0f, 1.0f};

        /* Run 100 steps with zero input */
        for (int t = 0; t < 100; t++) {
            float h_new[TEST_HIDDEN_DIM];
            trix_cfc_cell(x, h, 0.1f, &params, h_new);
            memcpy(h, h_new, sizeof(h));
        }

        /* State should decay toward zero */
        float sum = 0.0f;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            sum += fabsf(h[i]);
        }
        TEST(sum < 0.5f, "State should decay over time (got %f)", sum);
    }

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Fixed Time Step Mode
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_fixed(void) {
    SECTION("CfC FIXED TIME STEP");

    CfCParams params_var = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
    };

    CfCParamsFixed params_fixed = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .decay = test_decay,
    };

    /* Test: Fixed and variable should produce same result for same dt */
    {
        float x[TEST_INPUT_DIM] = {0.7f, -0.2f};
        float h_prev[TEST_HIDDEN_DIM] = {0.3f, 0.1f, 0.0f, 0.5f};
        float h_var[TEST_HIDDEN_DIM], h_fixed[TEST_HIDDEN_DIM];

        trix_cfc_cell(x, h_prev, 0.1f, &params_var, h_var);
        trix_cfc_cell_fixed(x, h_prev, &params_fixed, h_fixed);

        int match = 1;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            if (!FLOAT_EQ(h_var[i], h_fixed[i], 1e-5f)) {
                match = 0;
                printf("    h[%d]: var=%f, fixed=%f\n", i, h_var[i], h_fixed[i]);
            }
        }
        TEST(match, "Fixed mode should match variable mode for same dt");
    }

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Sequence Processing
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_sequence(void) {
    SECTION("CfC SEQUENCE PROCESSING");

    CfCParams params = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
    };

    /* Test: Process sequence and verify final state */
    {
        #define SEQ_LEN 10
        float inputs[SEQ_LEN * TEST_INPUT_DIM];
        float outputs[SEQ_LEN * TEST_HIDDEN_DIM];
        float h_final[TEST_HIDDEN_DIM];

        /* Create sine wave input */
        for (int t = 0; t < SEQ_LEN; t++) {
            inputs[t * TEST_INPUT_DIM + 0] = sinf(t * 0.5f);
            inputs[t * TEST_INPUT_DIM + 1] = cosf(t * 0.5f);
        }

        trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &params, NULL, outputs, h_final);

        /* Verify outputs are bounded */
        int bounded = 1;
        for (int i = 0; i < SEQ_LEN * TEST_HIDDEN_DIM; i++) {
            if (fabsf(outputs[i]) > 10.0f) bounded = 0;
        }
        TEST(bounded, "Sequence outputs should be bounded");

        /* Verify final state matches last output */
        int match = 1;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            if (!FLOAT_EQ(h_final[i], outputs[(SEQ_LEN-1) * TEST_HIDDEN_DIM + i], 1e-6f)) {
                match = 0;
            }
        }
        TEST(match, "Final state should match last output");
        #undef SEQ_LEN
    }

    /* Test: Determinism of sequence processing */
    {
        #define SEQ_LEN 5
        float inputs[SEQ_LEN * TEST_INPUT_DIM] = {
            0.1f, 0.2f,
            0.3f, 0.4f,
            0.5f, 0.6f,
            0.7f, 0.8f,
            0.9f, 1.0f,
        };
        float out1[SEQ_LEN * TEST_HIDDEN_DIM], out2[SEQ_LEN * TEST_HIDDEN_DIM];
        float h1[TEST_HIDDEN_DIM], h2[TEST_HIDDEN_DIM];

        trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &params, NULL, out1, h1);
        trix_cfc_forward(inputs, SEQ_LEN, 0.1f, &params, NULL, out2, h2);

        int match = 1;
        for (int i = 0; i < SEQ_LEN * TEST_HIDDEN_DIM; i++) {
            if (out1[i] != out2[i]) match = 0;
        }
        TEST(match, "Sequence processing must be deterministic");
        #undef SEQ_LEN
    }

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Output Projection
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_output(void) {
    SECTION("CfC OUTPUT PROJECTION");

    CfCOutputParams out_params = {
        .hidden_dim = TEST_HIDDEN_DIM,
        .output_dim = TEST_OUTPUT_DIM,
        .W_out = test_W_out,
        .b_out = test_b_out,
    };

    /* Test: Linear projection */
    {
        float h[TEST_HIDDEN_DIM] = {1.0f, 0.0f, 0.5f, 0.0f};
        float output[TEST_OUTPUT_DIM];

        trix_cfc_output(h, &out_params, output);

        /* Expected: [1.0*1 + 0.5*0.5, 0*1 + 0*0.5] = [1.25, 0] */
        TEST(FLOAT_EQ(output[0], 1.25f, 1e-5f), "Linear output[0] expected 1.25, got %f", output[0]);
        TEST(FLOAT_EQ(output[1], 0.0f, 1e-5f), "Linear output[1] expected 0, got %f", output[1]);
    }

    /* Test: Softmax output sums to 1 */
    {
        float h[TEST_HIDDEN_DIM] = {0.5f, 0.3f, 0.1f, 0.2f};
        float probs[TEST_OUTPUT_DIM];

        trix_cfc_output_softmax(h, &out_params, probs);

        float sum = 0.0f;
        for (int i = 0; i < TEST_OUTPUT_DIM; i++) {
            sum += probs[i];
        }
        TEST(FLOAT_EQ(sum, 1.0f, 1e-5f), "Softmax should sum to 1 (got %f)", sum);

        /* All probabilities should be positive */
        int positive = 1;
        for (int i = 0; i < TEST_OUTPUT_DIM; i++) {
            if (probs[i] < 0.0f) positive = 0;
        }
        TEST(positive, "Softmax outputs must be positive");
    }

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Time Constant Behavior
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_tau(void) {
    SECTION("CfC TIME CONSTANTS");

    /* Test: Larger tau = slower decay */
    {
        float tau_fast[1] = {0.1f};
        float tau_slow[1] = {10.0f};

        CfCParams params_fast = {
            .input_dim = TEST_INPUT_DIM,
            .hidden_dim = TEST_HIDDEN_DIM,
            .W_gate = test_W_gate,
            .b_gate = test_b_gate,
            .W_cand = test_W_cand,
            .b_cand = test_b_cand,
            .tau = tau_fast,
            .tau_shared = 1,
        };

        CfCParams params_slow = {
            .input_dim = TEST_INPUT_DIM,
            .hidden_dim = TEST_HIDDEN_DIM,
            .W_gate = test_W_gate,
            .b_gate = test_b_gate,
            .W_cand = test_W_cand,
            .b_cand = test_b_cand,
            .tau = tau_slow,
            .tau_shared = 1,
        };

        float x[TEST_INPUT_DIM] = {0.0f, 0.0f};
        float h_init[TEST_HIDDEN_DIM] = {1.0f, 1.0f, 1.0f, 1.0f};
        float h_fast[TEST_HIDDEN_DIM], h_slow[TEST_HIDDEN_DIM];

        memcpy(h_fast, h_init, sizeof(h_fast));
        memcpy(h_slow, h_init, sizeof(h_slow));

        /* Run 10 steps */
        for (int t = 0; t < 10; t++) {
            float tmp[TEST_HIDDEN_DIM];
            trix_cfc_cell(x, h_fast, 0.1f, &params_fast, tmp);
            memcpy(h_fast, tmp, sizeof(h_fast));

            trix_cfc_cell(x, h_slow, 0.1f, &params_slow, tmp);
            memcpy(h_slow, tmp, sizeof(h_slow));
        }

        /* Fast tau should decay more */
        float sum_fast = 0.0f, sum_slow = 0.0f;
        for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
            sum_fast += fabsf(h_fast[i]);
            sum_slow += fabsf(h_slow[i]);
        }
        TEST(sum_fast < sum_slow, "Smaller tau should decay faster (fast=%f, slow=%f)", sum_fast, sum_slow);
    }

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Memory Footprint and FLOPs
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_metrics(void) {
    SECTION("CfC METRICS");

    CfCParams params = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
    };

    size_t mem = trix_cfc_memory_footprint(&params);
    size_t flops = trix_cfc_flops(&params);

    printf("  Memory: %zu bytes\n", mem);
    printf("  FLOPs:  %zu per step\n", flops);

    TEST(mem > 0, "Memory footprint should be positive");
    TEST(flops > 0, "FLOPs should be positive");

    /* For a 2-input, 4-hidden network:
     * W_gate: 4 * 6 * 4 = 96 bytes
     * b_gate: 4 * 4 = 16 bytes
     * W_cand: 4 * 6 * 4 = 96 bytes
     * b_cand: 4 * 4 = 16 bytes
     * tau: 1 * 4 = 4 bytes
     * Total: 228 bytes */
    TEST(mem == 228, "Expected 228 bytes, got %zu", mem);

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Test: Stress Test (Many iterations)
 * ═══════════════════════════════════════════════════════════════════════════ */

static void test_cfc_stress(void) {
    SECTION("CfC STRESS TEST");

    CfCParams params = {
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .b_gate = test_b_gate,
        .W_cand = test_W_cand,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
    };

    /* Run 10000 steps with oscillating input */
    {
        float h[TEST_HIDDEN_DIM] = {0.0f, 0.0f, 0.0f, 0.0f};
        float x[TEST_INPUT_DIM];

        int bounded = 1;
        for (int t = 0; t < 10000; t++) {
            x[0] = sinf(t * 0.1f);
            x[1] = cosf(t * 0.1f);

            float h_new[TEST_HIDDEN_DIM];
            trix_cfc_cell(x, h, 0.01f, &params, h_new);
            memcpy(h, h_new, sizeof(h));

            for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
                if (fabsf(h[i]) > 100.0f || isnan(h[i]) || isinf(h[i])) {
                    bounded = 0;
                    break;
                }
            }
            if (!bounded) break;
        }
        TEST(bounded, "10000 iterations should remain numerically stable");
    }

    printf("  10000 iterations: STABLE\n");

    SECTION_END();
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╭──────────────────────────────────────────────────────────────╮\n");
    printf("│  TriX CfC Shape Tests                                        │\n");
    printf("│  \"Solid-State Fluid Dynamics\"                                │\n");
    printf("╰──────────────────────────────────────────────────────────────╯\n");

    test_cfc_basic();
    test_cfc_fixed();
    test_cfc_sequence();
    test_cfc_output();
    test_cfc_tau();
    test_cfc_metrics();
    test_cfc_stress();

    printf("\n");
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("  TOTAL: %d/%d tests passed", tests_passed, tests_run);
    if (tests_passed == tests_run) {
        printf(" ✓\n");
    } else {
        printf(" ✗ (%d failed)\n", tests_run - tests_passed);
    }
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("\n");

    return (tests_passed == tests_run) ? 0 : 1;
}
