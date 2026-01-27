/*
 * test_i8mm_forge.c — Test I8MM (SMMLA) Forge Backend
 *
 * Tests the I8MM batch inference kernel for M4 chips.
 *
 * Copyright 2026 Trix Research
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "../include/linear_forge.h"

/* Simple PRNG for reproducible tests */
static uint32_t g_seed = 12345;

static uint32_t lcg_next(void) {
    g_seed = g_seed * 1664525 + 1013904223;
    return g_seed;
}

static int8_t random_ternary(void) {
    return (int8_t)(lcg_next() % 3) - 1;
}

/* Reference implementation for verification */
static void ref_matmul_batch(
    const int8_t* x,    /* [M, K] */
    const int8_t* W,    /* [N, K] */
    int32_t* y,         /* [M, N] */
    int M, int N, int K
) {
    for (int m = 0; m < M; m++) {
        for (int n = 0; n < N; n++) {
            int32_t sum = 0;
            for (int k = 0; k < K; k++) {
                sum += x[m * K + k] * W[n * K + k];
            }
            y[m * N + n] = sum;
        }
    }
}

int main() {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  I8MM (SMMLA) Forge Backend Test                           ║\n");
    printf("║  \"SMMLA: 2x8x2 matrix multiply per instruction\"            ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    /* Test dimensions */
    const int N = 64;   /* Output dimension (must be multiple of 4) */
    const int K = 128;  /* Input dimension (must be multiple of 8) */
    
    printf("Step 1: Generate test weights and forge I8MM kernel\n");
    printf("  Dimensions: N=%d, K=%d\n", N, K);
    
    /* Generate weights */
    int8_t* W = malloc(N * K);
    for (int i = 0; i < N * K; i++) {
        W[i] = random_ternary();
    }
    
    /* Create spec */
    AggregateShapeSpec spec = {0};
    strcpy(spec.name, "i8mm_test");
    spec.N = N;
    spec.K = K;
    spec.weights = W;
    spec.quant = QUANT_INT8;
    spec.strategy = FORGE_STRATEGY_I8MM_BATCH;
    
    /* Forge to string */
    char* buffer = malloc(128 * 1024);
    size_t len = forge_kernel_to_string(&spec, buffer, 128 * 1024);
    
    printf("  Generated: %zu bytes\n\n", len);
    
    /* Write to file */
    const char* header_path = "/tmp/i8mm_forged.h";
    FILE* f = fopen(header_path, "w");
    fwrite(buffer, 1, len, f);
    fclose(f);
    printf("Step 2: Wrote forged kernel to %s\n\n", header_path);
    
    /* Create test driver */
    const char* main_path = "/tmp/i8mm_test_main.c";
    f = fopen(main_path, "w");
    
    /* Test with batch size 4 */
    const int BATCH = 4;
    
    fprintf(f, "#include \"i8mm_forged.h\"\n");
    fprintf(f, "#include <stdio.h>\n");
    fprintf(f, "#include <stdlib.h>\n");
    fprintf(f, "#include <string.h>\n");
    fprintf(f, "#include <sys/time.h>\n\n");
    
    /* Reference implementation */
    fprintf(f, "static void ref_matmul_batch(\n");
    fprintf(f, "    const int8_t* x, const int8_t* W, int32_t* y,\n");
    fprintf(f, "    int M, int N, int K\n");
    fprintf(f, ") {\n");
    fprintf(f, "    for (int m = 0; m < M; m++)\n");
    fprintf(f, "        for (int n = 0; n < N; n++) {\n");
    fprintf(f, "            int32_t sum = 0;\n");
    fprintf(f, "            for (int k = 0; k < K; k++)\n");
    fprintf(f, "                sum += x[m * K + k] * W[n * K + k];\n");
    fprintf(f, "            y[m * N + n] = sum;\n");
    fprintf(f, "        }\n");
    fprintf(f, "}\n\n");
    
    fprintf(f, "int main() {\n");
    fprintf(f, "    const int M = %d;\n", BATCH);
    fprintf(f, "    const int N = %d;\n", N);
    fprintf(f, "    const int K = %d;\n\n", K);
    
    /* Allocate test data */
    fprintf(f, "    int8_t* x = malloc(M * K);\n");
    fprintf(f, "    int32_t* y_ref = malloc(M * N * sizeof(int32_t));\n");
    fprintf(f, "    int32_t* y_i8mm = malloc(M * N * sizeof(int32_t));\n");
    fprintf(f, "    int8_t* W_packed = malloc((N/2) * (K/8) * 16);\n\n");
    
    /* Initialize input */
    fprintf(f, "    /* Initialize input with pattern */\n");
    fprintf(f, "    for (int i = 0; i < M * K; i++) {\n");
    fprintf(f, "        x[i] = (i %% 3) - 1;  /* -1, 0, 1 pattern */\n");
    fprintf(f, "    }\n\n");
    
    /* Pack weights */
    fprintf(f, "    /* Pack weights for I8MM */\n");
    fprintf(f, "    i8mm_test_pack_i8mm(i8mm_test_W, W_packed);\n\n");
    
    /* Run reference */
    fprintf(f, "    /* Run reference */\n");
    fprintf(f, "    ref_matmul_batch(x, i8mm_test_W, y_ref, M, N, K);\n\n");
    
    /* Run I8MM */
    fprintf(f, "    /* Run I8MM batched with packed weights */\n");
    fprintf(f, "    i8mm_test_forward_batch_opt(x, W_packed, y_i8mm, M);\n\n");
    
    /* Verify */
    fprintf(f, "    /* Verify */\n");
    fprintf(f, "    int errors = 0;\n");
    fprintf(f, "    for (int i = 0; i < M * N; i++) {\n");
    fprintf(f, "        if (y_ref[i] != y_i8mm[i]) {\n");
    fprintf(f, "            if (errors < 5) {\n");
    fprintf(f, "                printf(\"MISMATCH at [%%d,%%d]: ref=%%d, i8mm=%%d\\n\",\n");
    fprintf(f, "                       i / N, i %% N, y_ref[i], y_i8mm[i]);\n");
    fprintf(f, "            }\n");
    fprintf(f, "            errors++;\n");
    fprintf(f, "        }\n");
    fprintf(f, "    }\n\n");
    
    fprintf(f, "    if (errors == 0) {\n");
    fprintf(f, "        printf(\"PASS: All %%d outputs match!\\n\", M * N);\n");
    fprintf(f, "    } else {\n");
    fprintf(f, "        printf(\"FAIL: %%d/%%d mismatches\\n\", errors, M * N);\n");
    fprintf(f, "    }\n\n");
    
    /* Benchmark */
    fprintf(f, "    /* Benchmark */\n");
    fprintf(f, "    const int ITERS = 10000;\n");
    fprintf(f, "    struct timeval t0, t1;\n");
    fprintf(f, "    gettimeofday(&t0, NULL);\n");
    fprintf(f, "    for (int i = 0; i < ITERS; i++) {\n");
    fprintf(f, "        i8mm_test_forward_batch_opt(x, W_packed, y_i8mm, M);\n");
    fprintf(f, "    }\n");
    fprintf(f, "    gettimeofday(&t1, NULL);\n");
    fprintf(f, "    double secs = (t1.tv_sec - t0.tv_sec) + (t1.tv_usec - t0.tv_usec) / 1e6;\n");
    fprintf(f, "    double ops = (double)M * N * K * 2 * ITERS;  /* 2 ops per MAC */\n");
    fprintf(f, "    double gops = ops / secs / 1e9;\n");
    fprintf(f, "    printf(\"I8MM batch=%%d: %%.1f GOP/s\\n\", M, gops);\n\n");
    
    fprintf(f, "    free(x);\n");
    fprintf(f, "    free(y_ref);\n");
    fprintf(f, "    free(y_i8mm);\n");
    fprintf(f, "    free(W_packed);\n");
    fprintf(f, "    return errors > 0 ? 1 : 0;\n");
    fprintf(f, "}\n");
    
    fclose(f);
    printf("Step 3: Created test driver at %s\n\n", main_path);
    
    /* Compile */
    printf("Step 4: Compile with I8MM support\n");
    char cmd[512];
    snprintf(cmd, sizeof(cmd),
        "clang -o /tmp/i8mm_test /tmp/i8mm_test_main.c -I/tmp "
        "-mcpu=apple-m4 -march=armv8.6-a+i8mm -O3 -Wall 2>&1");
    
    printf("  $ %s\n", cmd);
    
    FILE* proc = popen(cmd, "r");
    char output[1024];
    int has_output = 0;
    while (fgets(output, sizeof(output), proc)) {
        printf("  %s", output);
        has_output = 1;
    }
    int status = pclose(proc);
    
    if (status != 0) {
        printf("  COMPILATION FAILED!\n");
        printf("  Note: I8MM requires ARMv8.6-a or later\n");
        free(W);
        free(buffer);
        return 1;
    }
    
    if (!has_output) {
        printf("  Compilation successful!\n\n");
    }
    
    /* Run */
    printf("Step 5: Run I8MM test\n");
    proc = popen("/tmp/i8mm_test 2>&1", "r");
    while (fgets(output, sizeof(output), proc)) {
        printf("  %s", output);
    }
    status = pclose(proc);
    
    printf("\n");
    if (status == 0) {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  I8MM FORGE TEST: PASS\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    } else {
        printf("═══════════════════════════════════════════════════════════════\n");
        printf("  I8MM FORGE TEST: FAIL\n");
        printf("═══════════════════════════════════════════════════════════════\n");
    }
    
    free(W);
    free(buffer);
    return status;
}
