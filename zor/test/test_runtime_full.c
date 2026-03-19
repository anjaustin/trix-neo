/*
 * test_runtime_full.c — Full test of runtime with toolchain parser
 */

#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Full Runtime Test with Toolchain Parser ===\n\n");
    
    /* Create a test .trix file */
    FILE* f = fopen("/tmp/full_test.trix", "w");
    fprintf(f, "softchip:\n");
    fprintf(f, "  name: full_test_chip\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "\n");
    fprintf(f, "state:\n");
    fprintf(f, "  bits: 512\n");
    fprintf(f, "\n");
    fprintf(f, "signatures:\n");
    fprintf(f, "  pattern_zero:\n");
    fprintf(f, "    pattern: 0000000000000000000000000000000000000000000000000000000000000000\n");
    fprintf(f, "    threshold: 32\n");
    fprintf(f, "\n");
    fprintf(f, "  pattern_ff:\n");
    fprintf(f, "    pattern: fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\n");
    fprintf(f, "    threshold: 32\n");
    fprintf(f, "\n");
    fprintf(f, "inference:\n");
    fprintf(f, "  mode: first_match\n");
    fprintf(f, "  default: unknown\n");
    fclose(f);
    
    /* Load the chip */
    printf("Loading chip...\n");
    int error = 0;
    trix_chip_t* chip = trix_load("/tmp/full_test.trix", &error);
    
    if (!chip) {
        printf("FAIL: Could not load chip (error=%d)\n", error);
        return 1;
    }
    
    /* Get info */
    const trix_chip_info_t* info = trix_info(chip);
    printf("Loaded: %s v%s\n", info->name, info->version);
    printf("Signatures: %d\n", info->num_signatures);
    
    /* Print signature details */
    for (int i = 0; i < info->num_signatures; i++) {
        printf("\nSignature %d:\n", i);
        printf("  label: %s\n", trix_label(chip, i));
        printf("  threshold: %d\n", trix_threshold(chip, i));
        
        const uint8_t* sig = trix_signature(chip, i);
        printf("  pattern (first 8 bytes): ");
        for (int j = 0; j < 8; j++) {
            printf("%02x ", sig[j]);
        }
        printf("\n");
    }
    
    /* Test with all-zeros input */
    printf("\n--- Test 1: All zeros input ---\n");
    uint8_t zero_input[64];
    memset(zero_input, 0, 64);
    
    trix_result_t result = trix_infer(chip, zero_input);
    printf("Result: match=%d, distance=%d, label=%s\n", 
           result.match, result.distance, result.label ? result.label : "(null)");
    
    if (result.match == 0 && result.distance == 0) {
        printf("✓ PASS: Matched pattern_zero with distance 0\n");
    } else {
        printf("✗ FAIL: Expected match=0, distance=0\n");
    }
    
    /* Test with all-0xFF input */
    printf("\n--- Test 2: All 0xFF input ---\n");
    uint8_t ff_input[64];
    memset(ff_input, 0xFF, 64);
    
    result = trix_infer(chip, ff_input);
    printf("Result: match=%d, distance=%d, label=%s\n", 
           result.match, result.distance, result.label ? result.label : "(null)");
    
    if (result.match == 1 && result.distance == 0) {
        printf("✓ PASS: Matched pattern_ff with distance 0\n");
    } else {
        printf("✗ FAIL: Expected match=1, distance=0\n");
    }
    
    /* Test with partial match */
    printf("\n--- Test 3: Partial match (10 bit errors) ---\n");
    uint8_t partial[64];
    memset(partial, 0, 64);
    partial[0] = 0x03;  /* 2 bits different in first byte */
    partial[1] = 0x08;  /* 1 bit different in second byte */
    /* Total: ~3 bits different = should match with threshold 32 */
    
    result = trix_infer(chip, partial);
    printf("Result: match=%d, distance=%d, label=%s\n", 
           result.match, result.distance, result.label ? result.label : "(null)");
    
    trix_chip_free(chip);
    
    printf("\n=== DONE ===\n");
    return 0;
}