/*
 * test_linear_forge.c — Test the Linear Kingdom Circuit Stamper
 *
 * Verifies that forge_kernel_to_string() produces valid C/ASM code.
 *
 * Copyright 2026 Trix Research
 */

#include "../include/linear_forge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Test weights: simple 32x64 ternary matrix */
static int8_t test_weights[32 * 64];

static void init_test_weights(void) {
    /* Fill with ternary pattern: -1, 0, +1, 0, -1, 0, +1, ... */
    for (int i = 0; i < 32 * 64; i++) {
        switch (i % 4) {
            case 0: test_weights[i] = -1; break;
            case 1: test_weights[i] = 0; break;
            case 2: test_weights[i] = 1; break;
            case 3: test_weights[i] = 0; break;
        }
    }
}

static void test_c_portable(void) {
    printf("=== Test C Portable Backend ===\n\n");
    
    AggregateShapeSpec spec = {
        .name = "test_layer",
        .K = 64,
        .N = 32,
        .weights = test_weights,
        .weights_size = sizeof(test_weights),
        .quant = QUANT_INT8,
        .bias = NULL,
        .activation = ACT_RELU,
        .strategy = FORGE_STRATEGY_C_PORTABLE,
    };
    
    char buffer[32768];
    size_t len = forge_kernel_to_string(&spec, buffer, sizeof(buffer));
    
    printf("Generated %zu bytes:\n", len);
    printf("─────────────────────────────────────────\n");
    printf("%s", buffer);
    printf("─────────────────────────────────────────\n\n");
}

static void test_neon_block16(void) {
    printf("=== Test NEON Block-16 Backend ===\n\n");
    
    /* Need dimensions divisible by 16 */
    int8_t weights_16x16[256];
    for (int i = 0; i < 256; i++) {
        weights_16x16[i] = (i % 3) - 1;  /* -1, 0, 1, -1, 0, 1, ... */
    }
    
    AggregateShapeSpec spec = {
        .name = "block16_test",
        .K = 16,
        .N = 16,
        .weights = weights_16x16,
        .weights_size = sizeof(weights_16x16),
        .quant = QUANT_INT8,
        .bias = NULL,
        .activation = ACT_NONE,
        .strategy = FORGE_STRATEGY_NEON_BLOCK_16,
    };
    
    char buffer[32768];
    size_t len = forge_kernel_to_string(&spec, buffer, sizeof(buffer));
    
    printf("Generated %zu bytes:\n", len);
    printf("─────────────────────────────────────────\n");
    /* Only print first 2000 chars to avoid flooding terminal */
    if (len > 2000) {
        printf("%.2000s\n... (truncated)\n", buffer);
    } else {
        printf("%s", buffer);
    }
    printf("─────────────────────────────────────────\n\n");
}

static void test_weight_packing(void) {
    printf("=== Test Weight Packing ===\n\n");
    
    /* Source: 16x16 row-major */
    int8_t src[256];
    for (int n = 0; n < 16; n++) {
        for (int k = 0; k < 16; k++) {
            src[n * 16 + k] = n * 16 + k;  /* 0, 1, 2, ... 255 */
        }
    }
    
    /* Destination: Block-16 layout */
    int8_t dst[256];
    forge_pack_block16(src, dst, 16, 16);
    
    /* Verify: For 16x16 with one block, layout should be same */
    int match = 1;
    for (int i = 0; i < 256; i++) {
        if (src[i] != dst[i]) {
            match = 0;
            printf("Mismatch at %d: src=%d, dst=%d\n", i, src[i], dst[i]);
        }
    }
    
    if (match) {
        printf("Block-16 packing: PASS (16x16 single block)\n");
    } else {
        printf("Block-16 packing: FAIL\n");
    }
    
    /* Test ternary unpacking */
    uint8_t packed[4] = {
        0x01,  /* 00 01 = 0, +1 */
        0x0E,  /* 10 11 = -1, -1 (both map to -1) */
        0x05,  /* 01 01 = +1, +1 */
        0x00,  /* 00 00 = 0, 0 */
    };
    int8_t unpacked[16];
    forge_unpack_ternary(packed, unpacked, 16);
    
    printf("\nTernary unpack test:\n");
    printf("  Packed:   %02x %02x %02x %02x\n", 
           packed[0], packed[1], packed[2], packed[3]);
    printf("  Unpacked: ");
    for (int i = 0; i < 16; i++) {
        printf("%2d ", unpacked[i]);
    }
    printf("\n\n");
}

static void test_metrics(void) {
    printf("=== Test Metrics ===\n\n");
    
    AggregateShapeSpec spec = {
        .name = "metrics_test",
        .K = 4096,
        .N = 4096,
        .quant = QUANT_TERNARY,
    };
    
    size_t ops = forge_ops(&spec);
    size_t bytes = forge_weight_bytes(&spec);
    
    printf("Topology: %d x %d\n", spec.N, spec.K);
    printf("Operations: %zu (%.2f GOP)\n", ops, ops / 1e9);
    printf("Weight bytes (ternary): %zu (%.2f MB)\n", bytes, bytes / 1e6);
    printf("Compression vs float32: %.1fx\n", 
           (float)(spec.N * spec.K * 4) / bytes);
    printf("\n");
}

int main(void) {
    printf("╔════════════════════════════════════════════════════════════╗\n");
    printf("║  TriX Linear Forge — Test Suite                           ║\n");
    printf("║  \"The SDOT instruction IS the Prime.\"                     ║\n");
    printf("╚════════════════════════════════════════════════════════════╝\n\n");
    
    init_test_weights();
    
    test_c_portable();
    test_neon_block16();
    test_weight_packing();
    test_metrics();
    
    printf("All tests complete.\n");
    return 0;
}
