/*
 * trix_run.c — TriX Runtime CLI
 *
 * Simple CLI to load and run inference on .trix files
 *
 * Usage:
 *   ./trix_run model.trix 000102030405...
 *   ./trix_run --interactive model.trix
 *   ./trix_run --benchmark model.trix
 */

#include "../../zor/include/trixc/runtime.h"
#include "../../zor/include/trixc/errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_INPUT_LEN 256

static void print_hex(const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        printf("%02x", data[i]);
    }
}

static int hex_to_bytes(const char* hex, uint8_t* out, size_t max_len) {
    size_t hex_len = strlen(hex);
    size_t i = 0;
    size_t j = 0;
    
    while (i < hex_len && j < max_len) {
        /* Skip non-hex characters */
        if (hex[i] == ' ' || hex[i] == '\n' || hex[i] == '\r' || hex[i] == '\t') {
            i++;
            continue;
        }
        
        /* Need 2 hex chars for a byte */
        if (i + 1 >= hex_len) break;
        
        char byte_str[3] = {hex[i], hex[i+1], '\0'};
        char* end = NULL;
        long byte = strtol(byte_str, &end, 16);
        
        if (end && *end == '\0' && byte >= 0 && byte <= 255) {
            out[j++] = (uint8_t)byte;
        }
        
        i += 2;
    }
    
    /* Zero-pad the rest */
    while (j < max_len) {
        out[j++] = 0;
    }
    
    return 0;
}

static void print_banner(void) {
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║              TriX Runtime CLI v1.0                          ║\n");
    printf("║        Deterministic Neural Inference Engine               ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");
}

static void print_chip_info(const trix_chip_t* chip) {
    trix_chip_info_t info;
    if (trix_info(chip, &info) != TRIX_OK) return;
    
    printf("Chip: %s v%s\n", info.name, info.version);
    printf("  State bits: %d\n", info.state_bits);
    printf("  Signatures: %d\n", info.num_signatures);
    printf("\n");
    
    printf("Signatures:\n");
    for (int i = 0; i < info.num_signatures; i++) {
        const char* label = trix_label(chip, i);
        int threshold = trix_threshold(chip, i);
        const uint8_t* sig = trix_signature(chip, i);
        
        printf("  [%d] %-20s threshold=%d  pattern=", i, label ? label : "(null)", threshold);
        print_hex(sig, 8);
        printf("...\n");
    }
    printf("\n");
}

static void run_single_inference(trix_chip_t* chip, const char* hex_input) {
    uint8_t input[64];
    hex_to_bytes(hex_input, input, 64);
    
    printf("Input: ");
    print_hex(input, 64);
    printf("\n\n");
    
    trix_result_t result = trix_infer(chip, input);
    
    if (result.match >= 0) {
        printf("✓ MATCH: %s\n", result.label);
        printf("  Distance: %d / %d\n", result.distance, result.threshold);
        printf("  Signature index: %d\n", result.match);
    } else {
        printf("✗ NO MATCH\n");
        printf("  Closest distance: %d\n", result.distance);
    }
}

static void run_interactive(trix_chip_t* chip) {
    char input[MAX_INPUT_LEN];
    
    print_chip_info(chip);
    
    printf("Interactive mode. Enter hex patterns (or 'quit' to exit).\n");
    printf("Example: 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n\n");
    
    while (1) {
        printf("> ");
        fflush(stdout);
        
        if (!fgets(input, sizeof(input), stdin)) {
            break;
        }
        
        /* Remove newline */
        input[strcspn(input, "\n")] = 0;
        
        if (strcmp(input, "quit") == 0 || strcmp(input, "q") == 0) {
            printf("Goodbye!\n");
            break;
        }
        
        if (strlen(input) == 0) {
            continue;
        }
        
        run_single_inference(chip, input);
    }
}

static void run_benchmark(trix_chip_t* chip, int iterations) {
    printf("Running benchmark: %d iterations...\n", iterations);
    
    /* Generate random input */
    uint8_t input[64];
    for (int i = 0; i < 64; i++) {
        input[i] = rand() & 0xFF;
    }
    
    clock_t start = clock();
    
    for (int i = 0; i < iterations; i++) {
        trix_infer(chip, input);
    }
    
    clock_t end = clock();
    double elapsed = (double)(end - start) / CLOCKS_PER_SEC;
    double throughput = iterations / elapsed;
    
    printf("\nResults:\n");
    printf("  Iterations:   %d\n", iterations);
    printf("  Time:         %.3f seconds\n", elapsed);
    printf("  Throughput:   %.1f inferences/sec\n", throughput);
    printf("  Latency:      %.3f ms/inference\n", elapsed * 1000 / iterations);
}

static void run_demo(trix_chip_t* chip) {
    print_chip_info(chip);
    
    printf("=== Demo: Built-in Test Cases ===\n\n");
    
    printf("Test 1: All zeros input\n");
    uint8_t zero_input[64];
    memset(zero_input, 0, 64);
    printf("Input: ");
    print_hex(zero_input, 64);
    printf("\n");
    trix_result_t r = trix_infer(chip, zero_input);
    printf("Result: %s (distance=%d)\n\n", r.match >= 0 ? r.label : "no match", r.distance);
    
    printf("Test 2: All ones input\n");
    uint8_t ones_input[64];
    memset(ones_input, 0xFF, 64);
    printf("Input: ");
    print_hex(ones_input, 64);
    printf("\n");
    r = trix_infer(chip, ones_input);
    printf("Result: %s (distance=%d)\n\n", r.match >= 0 ? r.label : "no match", r.distance);
    
    printf("Test 3: Partial match\n");
    uint8_t partial[64];
    memset(partial, 0x0F, 64);  /* Only 4 bits set per byte = 256 bits set */
    printf("Input: ");
    print_hex(partial, 64);
    printf("\n");
    r = trix_infer(chip, partial);
    printf("Result: %s (distance=%d)\n", r.match >= 0 ? r.label : "no match", r.distance);
}

int main(int argc, char** argv) {
    print_banner();
    
    if (argc < 2) {
        printf("Usage:\n");
        printf("  %s <chip.trix> [hex_input]\n", argv[0]);
        printf("  %s --interactive <chip.trix>\n", argv[0]);
        printf("  %s --benchmark <chip.trix> [iterations]\n", argv[0]);
        printf("  %s --demo <chip.trix>\n", argv[0]);
        printf("\n");
        printf("Examples:\n");
        printf("  %s gesture.trix 000102030405...\n", argv[0]);
        printf("  %s gesture.trix --interactive\n", argv[0]);
        printf("  %s gesture.trix --benchmark 10000\n", argv[0]);
        return 1;
    }
    
    /* Parse options */
    int mode = 0;  /* 0=single, 1=interactive, 2=benchmark, 3=demo */
    const char* chip_path = NULL;
    const char* hex_input = NULL;
    int iterations = 1000;
    
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--interactive") == 0 || strcmp(argv[i], "-i") == 0) {
            mode = 1;
        } else if (strcmp(argv[i], "--benchmark") == 0 || strcmp(argv[i], "-b") == 0) {
            mode = 2;
        } else if (strcmp(argv[i], "--demo") == 0 || strcmp(argv[i], "-d") == 0) {
            mode = 3;
        } else if (argv[i][0] != '-' && !chip_path) {
            chip_path = argv[i];
        } else if (argv[i][0] != '-' && !hex_input) {
            hex_input = argv[i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s <chip.trix> [options]\n", argv[0]);
            return 0;
        } else {
            iterations = atoi(argv[i]);
        }
    }
    
    if (!chip_path) {
        printf("Error: No chip file specified\n");
        return 1;
    }
    
    /* Load chip */
    int error = 0;
    trix_chip_t* chip = trix_load(chip_path, &error);
    
    if (!chip) {
        printf("Error: Failed to load chip (error=%d)\n", error);
        return 1;
    }
    
    printf("✓ Loaded: %s\n\n", chip_path);
    
    /* Run requested mode */
    switch (mode) {
        case 0:  /* Single inference */
            if (hex_input) {
                run_single_inference(chip, hex_input);
            } else {
                print_chip_info(chip);
                printf("Provide hex input to run inference.\n");
                printf("Example: %s %s 000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f\n", 
                       argv[0], chip_path);
            }
            break;
            
        case 1:  /* Interactive */
            run_interactive(chip);
            break;
            
        case 2:  /* Benchmark */
            run_benchmark(chip, iterations);
            break;
            
        case 3:  /* Demo */
            run_demo(chip);
            break;
    }
    
    trix_chip_free(chip);
    return 0;
}