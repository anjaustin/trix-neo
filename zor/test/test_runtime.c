/*
 * test_runtime.c — Test the TriX runtime engine
 */

#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║              TriX Runtime Engine Test                                ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n\n");
    
    /* Create a test .trix file with SIMPLE format */
    FILE* f = fopen("/tmp/test_chip.trix", "w");
    fprintf(f, "softchip:\n");
    fprintf(f, "  name: test_runtime_chip\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "\n");
    fprintf(f, "state:\n");
    fprintf(f, "  bits: 512\n");
    fprintf(f, "\n");
    fprintf(f, "signatures:\n");
    fprintf(f, "  sig1:\n");
    fprintf(f, "    pattern: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
    fprintf(f, "    threshold: 32\n");
    fprintf(f, "\n");
    fprintf(f, "inference:\n");
    fprintf(f, "  mode: first_match\n");
    fprintf(f, "  default: unknown\n");
    fclose(f);
    
    /* Load the chip */
    printf("[TEST] Loading chip from .trix file...\n");
    int error = 0;
    trix_chip_t* chip = trix_load("/tmp/test_chip.trix", &error);
    
    if (!chip) {
        printf("FAIL: Could not load chip (error=%d)\n", error);
        return 1;
    }
    
    const trix_chip_info_t* info = trix_info(chip);
    printf("  ✓ Loaded: %s v%s\n", info->name, info->version);
    printf("  ✓ Signatures: %d\n", info->num_signatures);
    
    /* Test inference */
    printf("\n[TEST] Testing with zero input...\n");
    uint8_t zero_input[64] = {0};
    trix_result_t result = trix_infer(chip, zero_input);
    printf("  Result: match=%d, distance=%d, label=%s\n", 
           result.match, result.distance, result.label ? result.label : "(null)");
    
    /* Test with pattern that should match with threshold 32 */
    printf("\n[TEST] Testing threshold matching...\n");
    /* First 32 bytes should match within threshold 32 */
    uint8_t partial[64];
    memset(partial, 0xFF, 64);
    memset(partial, 0, 32);  /* First half matches signature */
    
    result = trix_infer(chip, partial);
    printf("  Partial match: match=%d, distance=%d\n", result.match, result.distance);
    
    /* Test get label */
    printf("\n[TEST] Labels...\n");
    const char* label = trix_label(chip, 0);
    printf("  Label[0]: %s\n", label ? label : "(null)");
    
    /* Test memory */
    printf("\n[TEST] Memory footprint: %zu bytes\n", trix_memory_footprint(chip));
    
    /* Test null safety */
    printf("\n[TEST] Null safety...\n");
    trix_chip_free(NULL);
    printf("  ✓ trix_chip_free(NULL): OK\n");
    printf("  ✓ trix_info(NULL): %p\n", (void*)trix_info(NULL));
    
    /* Cleanup */
    printf("\n[TEST] Cleanup...\n");
    trix_chip_free(chip);
    printf("  ✓ Freed\n");
    
    printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                     TESTS COMPLETE                                   ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    
    return 0;
}