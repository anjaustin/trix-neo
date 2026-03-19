/*
 * fuzz_softchip.c — Fuzzing tests for .trix parser (Red-Teaming)
 *
 * This file provides fuzzing tests for the softchip parser to find
 * security vulnerabilities, buffer overflows, and edge cases.
 *
 * Build with libFuzzer:
 *   clang -fsanitize=fuzzer,address fuzz_softchip.c -o fuzz_softchip \
 *       -I../include -I../../zor/include tools/src/softchip.c \
 *       zor/src/errors.c zor/src/logging.c zor/src/memory.c -lm
 *
 * Run:
 *   ./fuzz_softchip /path/to/corpus/
 *
 * Copyright 2026 Trix Research
 */

#include "../include/softchip.h"
#include "../../zor/include/trixc/memory.h"
#include "../../zor/include/trixc/errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fuzz target: parse .trix spec from memory */
int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size == 0 || size > 1024 * 1024) {
        return 0;
    }
    
    /* Create null-terminated string from fuzz input */
    char* input = (char*)malloc(size + 1);
    if (!input) {
        return 0;
    }
    memcpy(input, data, size);
    input[size] = '\0';
    
    /* Initialize spec - will be populated by parser */
    SoftChipSpec spec;
    softchip_init(&spec);
    
    /* Try to parse - this is what we fuzz */
    softchip_parse_string(input, &spec);
    
    /* Test signature parsing with fuzzed hex data */
    uint8_t pattern[64];
    memset(pattern, 0, sizeof(pattern));
    
    /* Try various hex string lengths */
    for (size_t len = 1; len < size && len < 256; len++) {
        char hex_buf[512];
        if (len < sizeof(hex_buf)) {
            memcpy(hex_buf, input, len);
            hex_buf[len] = '\0';
            signature_from_hex(hex_buf, pattern);
        }
    }
    
    /* Try base64 decoding with fuzzed input */
    for (size_t len = 1; len < size && len < 256; len++) {
        char b64_buf[512];
        if (len < sizeof(b64_buf)) {
            memcpy(b64_buf, input, len);
            b64_buf[len] = '\0';
            signature_from_base64(b64_buf, pattern);
        }
    }
    
    /* Test shape name parsing */
    if (size > 0) {
        char name_buf[64];
        size_t name_len = size < sizeof(name_buf) - 1 ? size : sizeof(name_buf) - 1;
        memcpy(name_buf, data, name_len);
        name_buf[name_len] = '\0';
        
        ShapeType shape = shape_from_name(name_buf);
        (void)shape;
    }
    
    /* Test signature hex conversion */
    if (size >= 64) {
        char hex_out[129];
        signature_to_hex(data, hex_out);
    }
    
    free(input);
    return 0;
}

/* Initialize fuzzing corpus from directory */
#ifdef FUZZ_CORPUS
int main(int argc, char** argv) {
    printf("TriX Parser Fuzzer\n");
    printf("Usage: %s [corpus_dir]\n", argv[0]);
    printf("If no corpus dir provided, uses empty corpus for random fuzzing\n");
    return LLVMFuzzerTestOneInput(NULL, 0);
}
#endif