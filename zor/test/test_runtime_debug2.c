/*
 * test_runtime_debug2.c — Debug inference loop
 */

#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>

int main() {
    /* Create test file */
    FILE* f = fopen("/tmp/debug2.trix", "w");
    fprintf(f, "softchip:\n  name: debug2\n");
    fprintf(f, "state:\n  bits: 512\n");
    fprintf(f, "signatures:\n");
    fprintf(f, "  sig_zero:\n    pattern: ");
    for (int i = 0; i < 64; i++) fprintf(f, "00");
    fprintf(f, "\n    threshold: 32\n");
    fprintf(f, "  sig_ff:\n    pattern: ");
    for (int i = 0; i < 64; i++) fprintf(f, "ff");
    fprintf(f, "\n    threshold: 32\n");
    fprintf(f, "inference:\n  mode: first_match\n");
    fclose(f);
    
    int error = 0;
    trix_chip_t* chip = trix_load("/tmp/debug2.trix", &error);
    if (!chip) { printf("Load failed\n"); return 1; }
    
    /* Manually check what signatures look like */
    printf("=== Signature 0 (should be zeros) ===\n");
    const uint8_t* s0 = trix_signature(chip, 0);
    printf("First 8 bytes: ");
    for (int i = 0; i < 8; i++) printf("%02x ", s0[i]);
    printf("\n");
    printf("Threshold: %d\n", trix_threshold(chip, 0));
    
    printf("\n=== Signature 1 (should be ff) ===\n");
    const uint8_t* s1 = trix_signature(chip, 1);
    printf("First 8 bytes: ");
    for (int i = 0; i < 8; i++) printf("%02x ", s1[i]);
    printf("\n");
    printf("Threshold: %d\n", trix_threshold(chip, 1));
    
    /* Test input: all 0xFF */
    uint8_t input[64];
    memset(input, 0xFF, 64);
    
    printf("\n=== Input (all 0xFF) ===\n");
    printf("First 8 bytes: ");
    for (int i = 0; i < 8; i++) printf("%02x ", input[i]);
    printf("\n");
    
    /* Manual XOR */
    printf("\n=== Manual XOR with sig1 (0xFF) ===\n");
    int manual_dist = 0;
    for (int i = 0; i < 64; i++) {
        uint8_t x = input[i] ^ s1[i];
        while (x) { manual_dist += x & 1; x >>= 1; }
    }
    printf("Distance: %d\n", manual_dist);
    printf("Threshold: %d\n", trix_threshold(chip, 1));
    printf("Should match: %s\n", (manual_dist <= trix_threshold(chip, 1)) ? "YES" : "NO");
    
    /* Now run inference */
    printf("\n=== trix_infer result ===\n");
    trix_result_t r = trix_infer(chip, input);
    printf("match=%d, distance=%d, label=%s\n", r.match, r.distance, r.label ? r.label : "(null)");
    
    trix_chip_free(chip);
    return 0;
}