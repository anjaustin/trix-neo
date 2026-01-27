/*
 * test_cfc_forge.c — Test CfC Soft-Chip Forge
 *
 * This test:
 * 1. Creates a CfC spec with test weights
 * 2. Forges it to a header file
 * 3. Compiles and runs the forged code
 * 4. Verifies the output is reasonable
 */

#include "../include/cfc_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test dimensions */
#define TEST_INPUT_DIM  64
#define TEST_HIDDEN_DIM 32
#define TEST_OUTPUT_DIM 10
#define TEST_CONCAT_DIM (TEST_INPUT_DIM + TEST_HIDDEN_DIM)

/* Test weights */
static int8_t test_W_gate[TEST_HIDDEN_DIM * TEST_CONCAT_DIM];
static int8_t test_W_cand[TEST_HIDDEN_DIM * TEST_CONCAT_DIM];
static int8_t test_W_out[TEST_OUTPUT_DIM * TEST_HIDDEN_DIM];
static float test_b_gate[TEST_HIDDEN_DIM];
static float test_b_cand[TEST_HIDDEN_DIM];
static float test_b_out[TEST_OUTPUT_DIM];
static float test_tau[1] = {1.0f};

static void init_weights(void) {
    /* Ternary pattern: -1, 0, +1 */
    for (int i = 0; i < TEST_HIDDEN_DIM * TEST_CONCAT_DIM; i++) {
        test_W_gate[i] = (i % 3) - 1;
        test_W_cand[i] = ((i + 1) % 3) - 1;
    }
    for (int i = 0; i < TEST_OUTPUT_DIM * TEST_HIDDEN_DIM; i++) {
        test_W_out[i] = (i % 3) - 1;
    }
    
    /* Zero biases */
    for (int i = 0; i < TEST_HIDDEN_DIM; i++) {
        test_b_gate[i] = 0.0f;
        test_b_cand[i] = 0.0f;
    }
    for (int i = 0; i < TEST_OUTPUT_DIM; i++) {
        test_b_out[i] = 0.0f;
    }
}

static int forge_and_test(void) {
    printf("Step 1: Create CfC spec...\n");
    
    CfCCellSpec spec = {
        .name = "test_cfc",
        .input_dim = TEST_INPUT_DIM,
        .hidden_dim = TEST_HIDDEN_DIM,
        .W_gate = test_W_gate,
        .W_cand = test_W_cand,
        .b_gate = test_b_gate,
        .b_cand = test_b_cand,
        .tau = test_tau,
        .tau_shared = 1,
        .has_output = 1,
        .output_dim = TEST_OUTPUT_DIM,
        .W_out = test_W_out,
        .b_out = test_b_out,
        .strategy = FORGE_STRATEGY_NEON_BLOCK8_K64,
    };
    
    printf("  Input:  %d\n", spec.input_dim);
    printf("  Hidden: %d\n", spec.hidden_dim);
    printf("  Output: %d\n", spec.output_dim);
    printf("  Concat: %d\n", TEST_CONCAT_DIM);
    
    printf("\nStep 2: Forge CfC soft-chip...\n");
    
    char buffer[256 * 1024];
    size_t len = forge_cfc_to_string(&spec, buffer, sizeof(buffer));
    
    if (len == 0) {
        printf("  ERROR: forge_cfc_to_string failed\n");
        return -1;
    }
    printf("  Generated %zu bytes\n", len);
    
    /* Write to file */
    const char* tmpfile = "/tmp/test_cfc_forged.h";
    FILE* f = fopen(tmpfile, "w");
    if (!f) {
        printf("  ERROR: Cannot create %s\n", tmpfile);
        return -1;
    }
    fprintf(f, "%s", buffer);
    fclose(f);
    printf("  Wrote %s\n", tmpfile);
    
    /* Create test harness */
    const char* testfile = "/tmp/test_cfc_main.c";
    f = fopen(testfile, "w");
    if (!f) {
        printf("  ERROR: Cannot create %s\n", testfile);
        return -1;
    }
    
    fprintf(f, "#include \"test_cfc_forged.h\"\n");
    fprintf(f, "#include <stdio.h>\n\n");
    fprintf(f, "int main(void) {\n");
    fprintf(f, "    float x[%d];\n", TEST_INPUT_DIM);
    fprintf(f, "    float h[%d] = {0};\n", TEST_HIDDEN_DIM);
    fprintf(f, "    float h_new[%d];\n", TEST_HIDDEN_DIM);
    fprintf(f, "    float probs[%d];\n\n", TEST_OUTPUT_DIM);
    fprintf(f, "    /* Initialize input */\n");
    fprintf(f, "    for (int i = 0; i < %d; i++) x[i] = (float)(i %% 5) / 5.0f - 0.4f;\n\n", TEST_INPUT_DIM);
    fprintf(f, "    /* Run CfC cell */\n");
    fprintf(f, "    test_cfc_cell(x, h, 0.1f, h_new);\n\n");
    fprintf(f, "    /* Print first few hidden values */\n");
    fprintf(f, "    printf(\"h_new: \");\n");
    fprintf(f, "    for (int i = 0; i < 8; i++) printf(\"%%6.3f \", h_new[i]);\n");
    fprintf(f, "    printf(\"...\\n\");\n\n");
    fprintf(f, "    /* Run output projection */\n");
    fprintf(f, "    test_cfc_output_softmax(h_new, probs);\n\n");
    fprintf(f, "    printf(\"probs: \");\n");
    fprintf(f, "    for (int i = 0; i < %d; i++) printf(\"%%5.3f \", probs[i]);\n", TEST_OUTPUT_DIM);
    fprintf(f, "    printf(\"\\n\");\n\n");
    fprintf(f, "    /* Verify probabilities sum to 1 */\n");
    fprintf(f, "    float sum = 0;\n");
    fprintf(f, "    for (int i = 0; i < %d; i++) sum += probs[i];\n", TEST_OUTPUT_DIM);
    fprintf(f, "    printf(\"sum: %%f\\n\", sum);\n\n");
    fprintf(f, "    if (sum > 0.99f && sum < 1.01f) {\n");
    fprintf(f, "        printf(\"SUCCESS: CfC soft-chip works!\\n\");\n");
    fprintf(f, "        return 0;\n");
    fprintf(f, "    } else {\n");
    fprintf(f, "        printf(\"FAIL: Probability sum = %%f (expected ~1.0)\\n\", sum);\n");
    fprintf(f, "        return 1;\n");
    fprintf(f, "    }\n");
    fprintf(f, "}\n");
    fclose(f);
    printf("  Wrote %s\n", testfile);
    
    /* Compile */
    printf("\nStep 3: Compile forged CfC...\n");
    const char* compile_cmd = 
        "clang -o /tmp/test_cfc /tmp/test_cfc_main.c "
        "-I/tmp -mcpu=apple-m4 -march=armv8.6-a+dotprod -O2 -Wall -lm 2>&1";
    
    printf("  $ %s\n", compile_cmd);
    FILE* p = popen(compile_cmd, "r");
    if (!p) {
        printf("  ERROR: popen failed\n");
        return -1;
    }
    
    char output[4096] = {0};
    size_t bytes_read = fread(output, 1, sizeof(output) - 1, p);
    int status = pclose(p);
    
    if (bytes_read > 0) {
        printf("  Compiler output:\n%s\n", output);
    }
    
    if (status != 0) {
        printf("  ERROR: Compilation failed (exit %d)\n", status);
        return -1;
    }
    printf("  Compilation successful!\n");
    
    /* Run */
    printf("\nStep 4: Run forged CfC...\n");
    printf("  $ /tmp/test_cfc\n");
    
    p = popen("/tmp/test_cfc 2>&1", "r");
    if (!p) {
        printf("  ERROR: popen failed\n");
        return -1;
    }
    
    bytes_read = fread(output, 1, sizeof(output) - 1, p);
    status = pclose(p);
    
    if (bytes_read > 0) {
        printf("  %s", output);
    }
    
    return status == 0 ? 0 : -1;
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Task 4: CfC Soft-Chip Forge Test                          ║\n");
    printf("║  \"The CfC cell is just 5 Primes orchestrated in time.\"     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    init_weights();
    
    int result = forge_and_test();
    
    printf("\n");
    if (result == 0) {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  TASK 4: PASS - CfC Soft-Chip Forged Successfully!\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    } else {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  TASK 4: FAIL\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    }
    
    return result;
}
