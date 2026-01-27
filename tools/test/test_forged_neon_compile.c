/*
 * test_forged_neon_compile.c — Verify forged NEON code compiles and runs
 *
 * This test:
 * 1. Forges a NEON Block-16 kernel
 * 2. Writes it to a temp file
 * 3. Compiles it with clang
 * 4. Links and runs
 *
 * Success: exit 0, correct output
 */

#include "../include/linear_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Test weights: 64x64 ternary (smaller to fit in buffer) */
#define TEST_N 64
#define TEST_K 64

static int8_t test_weights[TEST_N * TEST_K];
static int8_t test_weights_k64[TEST_N * TEST_K];  /* For Block-8-K64 */

static void init_weights(void) {
    for (int i = 0; i < TEST_N * TEST_K; i++) {
        test_weights[i] = (i % 3) - 1;  /* -1, 0, 1, -1, 0, 1, ... */
        test_weights_k64[i] = (i % 3) - 1;
    }
}

static int forge_and_compile(ForgeStrategy strategy, const char* strategy_name) {
    printf("Step 1: Forge NEON kernel (%s)...\n", strategy_name);
    
    AggregateShapeSpec spec = {
        .name = "forged_test",
        .K = TEST_K,
        .N = TEST_N,
        .weights = test_weights,
        .weights_size = sizeof(test_weights),
        .quant = QUANT_INT8,
        .bias = NULL,
        .activation = ACT_NONE,
        .strategy = strategy,
    };
    
    char buffer[128 * 1024];  /* 128KB */
    size_t len = forge_kernel_to_string(&spec, buffer, sizeof(buffer));
    
    if (len == 0) {
        printf("  ERROR: forge_kernel_to_string failed\n");
        return -1;
    }
    printf("  Generated %zu bytes\n", len);
    
    /* Write to temp file */
    const char* tmpfile = "/tmp/forged_neon_test.c";
    FILE* f = fopen(tmpfile, "w");
    if (!f) {
        printf("  ERROR: Cannot create %s\n", tmpfile);
        return -1;
    }
    
    /* Add a main function to make it compilable */
    fprintf(f, "%s\n", buffer);
    fprintf(f, "\n/* Test harness */\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <string.h>\n\n");
    fprintf(f, "int main(void) {\n");
    fprintf(f, "    int8_t x[%d] = {0};\n", TEST_K);
    fprintf(f, "    int32_t y[%d] = {0};\n", TEST_N);
    fprintf(f, "    \n");
    fprintf(f, "    /* Initialize x with test pattern */\n");
    fprintf(f, "    for (int i = 0; i < %d; i++) x[i] = (i %% 5) - 2;\n", TEST_K);
    fprintf(f, "    \n");
    fprintf(f, "    /* Run forged kernel */\n");
    fprintf(f, "    forged_test_forward_neon(x, y);\n");
    fprintf(f, "    \n");
    fprintf(f, "    /* Print first 16 outputs */\n");
    fprintf(f, "    printf(\"Output: \");\n");
    fprintf(f, "    for (int i = 0; i < 16; i++) printf(\"%%d \", y[i]);\n");
    fprintf(f, "    printf(\"...\\n\");\n");
    fprintf(f, "    \n");
    fprintf(f, "    printf(\"SUCCESS: Forged NEON kernel compiled and ran!\\n\");\n");
    fprintf(f, "    return 0;\n");
    fprintf(f, "}\n");
    
    fclose(f);
    printf("  Wrote %s\n", tmpfile);
    
    /* Compile with clang */
    printf("\nStep 2: Compile with clang...\n");
    const char* compile_cmd = 
        "clang -o /tmp/forged_neon_test /tmp/forged_neon_test.c "
        "-mcpu=apple-m4 -march=armv8.6-a+dotprod -O2 -Wall -Wextra 2>&1";
    
    printf("  $ %s\n", compile_cmd);
    FILE* p = popen(compile_cmd, "r");
    if (!p) {
        printf("  ERROR: popen failed\n");
        return -1;
    }
    
    char compile_output[4096] = {0};
    size_t bytes_read = fread(compile_output, 1, sizeof(compile_output) - 1, p);
    int compile_status = pclose(p);
    
    if (bytes_read > 0) {
        printf("  Compiler output:\n%s\n", compile_output);
    }
    
    if (compile_status != 0) {
        printf("  ERROR: Compilation failed (exit %d)\n", compile_status);
        return -1;
    }
    printf("  Compilation successful!\n");
    
    /* Run the compiled binary */
    printf("\nStep 3: Run compiled binary...\n");
    printf("  $ /tmp/forged_neon_test\n");
    
    p = popen("/tmp/forged_neon_test 2>&1", "r");
    if (!p) {
        printf("  ERROR: popen failed\n");
        return -1;
    }
    
    char run_output[4096] = {0};
    bytes_read = fread(run_output, 1, sizeof(run_output) - 1, p);
    int run_status = pclose(p);
    
    if (bytes_read > 0) {
        printf("  %s", run_output);
    }
    
    if (run_status != 0) {
        printf("  ERROR: Execution failed (exit %d)\n", run_status);
        return -1;
    }
    
    return 0;
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  Task 1: Verify Forged NEON Compiles                       ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    init_weights();
    
    /* Test Block-16 strategy */
    printf("=== Testing Block-16 Strategy ===\n\n");
    int result1 = forge_and_compile(FORGE_STRATEGY_NEON_BLOCK_16, "Block-16");
    
    printf("\n=== Testing Block-8-K64 Strategy ===\n\n");
    int result2 = forge_and_compile(FORGE_STRATEGY_NEON_BLOCK8_K64, "Block-8-K64");
    
    int result = (result1 == 0 && result2 == 0) ? 0 : -1;
    
    printf("\n");
    if (result == 0) {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  TASK 1: PASS (Both strategies compile and run!)\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    } else {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  TASK 1: FAIL\n");
        printf("  Block-16:    %s\n", result1 == 0 ? "PASS" : "FAIL");
        printf("  Block-8-K64: %s\n", result2 == 0 ? "PASS" : "FAIL");
        printf("═══════════════════════════════════════════════════════════════\n");
    }
    
    return result;
}
