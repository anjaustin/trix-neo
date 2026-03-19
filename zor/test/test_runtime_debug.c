/*
 * test_runtime_debug.c — Debug the runtime hex parsing
 */

#include "../include/trixc/runtime.h"
#include <stdio.h>
#include <string.h>

int main() {
    printf("=== Runtime Hex Parsing Debug ===\n\n");
    
    /* Create a test .trix file */
    FILE* f = fopen("/tmp/debug_chip.trix", "w");
    fprintf(f, "softchip:\n");
    fprintf(f, "  name: debug_chip\n");
    fprintf(f, "  version: 1.0.0\n");
    fprintf(f, "state:\n");
    fprintf(f, "  bits: 512\n");
    fprintf(f, "signatures:\n");
    fprintf(f, "  sig_a:\n");
    /* A simple pattern: 00 01 02 03 ... */
    fprintf(f, "    pattern: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n");
    fprintf(f, "    threshold: 256\n");  /* High threshold to allow matching */
    fprintf(f, "inference:\n");
    fprintf(f, "  mode: first_match\n");
    fclose(f);
    
    /* Print the file content */
    printf("File content:\n");
    f = fopen("/tmp/debug_chip.trix", "r");
    char line[256];
    while (fgets(line, sizeof(line), f)) {
        printf("  %s", line);
    }
    fclose(f);
    
    /* Load the chip */
    printf("\nLoading chip...\n");
    int error = 0;
    trix_chip_t* chip = trix_load("/tmp/debug_chip.trix", &error);
    
    if (!chip) {
        printf("FAIL: Could not load chip (error=%d)\n", error);
        return 1;
    }
    
    /* Check parsed signatures */
    printf("\nParsed signatures:\n");
    printf("  num_signatures: %d\n", chip->num_signatures);
    
    if (chip->num_signatures > 0) {
        printf("  label: %s\n", chip->labels[0]);
        printf("  threshold: %d\n", chip->thresholds[0]);
        
        printf("  pattern (first 16 bytes): ");
        for (int i = 0; i < 16; i++) {
            printf("%02x ", chip->signatures[0][i]);
        }
        printf("\n");
        
        /* Expected pattern: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ... */
        printf("  Expected pattern: 00 01 02 03 04 05 06 07 08 09 0a 0b 0c 0d 0e 0f ...\n");
    }
    
    /* Test inference with matching input */
    printf("\nTesting inference:\n");
    uint8_t input[64];
    for (int i = 0; i < 64; i++) input[i] = i;  /* 00 01 02 03 ... */
    
    trix_result_t result = trix_infer(chip, input);
    printf("  match: %d\n", result.match);
    printf("  distance: %d\n", result.distance);
    printf("  label: %s\n", result.label ? result.label : "(null)");
    
    /* Test with exact matching input */
    printf("\nTesting with exact signature:\n");
    memset(input, 0, 64);
    for (int i = 0; i < 32; i++) input[i] = i;  /* 00 01 02 ... 1f */
    
    result = trix_infer(chip, input);
    printf("  match: %d\n", result.match);
    printf("  distance: %d\n", result.distance);
    
    trix_chip_free(chip);
    
    return 0;
}