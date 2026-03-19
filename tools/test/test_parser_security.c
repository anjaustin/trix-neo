/*
 * test_parser_security.c — Security edge-case tests for .trix parser
 *
 * Manual red-teaming: Tests the parser with malicious/fuzzed inputs
 * to find buffer overflows, infinite loops, and other vulnerabilities.
 *
 * Copyright 2026 Trix Research
 */

#include "../include/softchip.h"
#include "../../zor/include/trixc/errors.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) static void test_##name(void)
#define RUN_TEST(name) do { \
    printf("\n[TEST] %s\n", #name); \
    test_##name(); \
} while(0)

#define EXPECT_NO_CRASH(code) do { \
    printf("  Running: %s ... ", #code); \
    fflush(stdout); \
    int crashed = 0; \
    for (int _i = 0; _i < 3; _i++) { \
        code; \
    } \
    printf("OK\n"); \
    tests_passed++; \
} while(0)

#define EXPECT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  FAIL: expected %d, got %d\n", (int)(b), (int)(a)); \
        tests_failed++; \
        return; \
    } \
} while(0)

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 1: Hex parsing with invalid characters
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(hex_parsing_edge_cases) {
    printf("  Testing hex parsing with malicious inputs...\n");
    
    uint8_t pattern[64];
    
    /* Empty string */
    EXPECT_NO_CRASH(signature_from_hex("", pattern));
    
    /* Single char */
    EXPECT_NO_CRASH(signature_from_hex("a", pattern));
    
    /* Odd number of chars */
    EXPECT_NO_CRASH(signature_from_hex("abc", pattern));
    
    /* Non-hex chars */
    EXPECT_NO_CRASH(signature_from_hex("gggggggg", pattern));
    
    /* Very long string */
    char long_str[1000];
    memset(long_str, 'z', sizeof(long_str));
    long_str[sizeof(long_str)-1] = '\0';
    EXPECT_NO_CRASH(signature_from_hex(long_str, pattern));
    
    /* Null prefix */
    EXPECT_NO_CRASH(signature_from_hex("0x", pattern));
    
    /* Uppercase X */
    EXPECT_NO_CRASH(signature_from_hex("0XABCDEF", pattern));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 2: Base64 parsing edge cases
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(base64_edge_cases) {
    printf("  Testing base64 parsing with malicious inputs...\n");
    
    uint8_t pattern[64];
    
    /* Empty */
    EXPECT_NO_CRASH(signature_from_base64("", pattern));
    
    /* Invalid chars */
    EXPECT_NO_CRASH(signature_from_base64("!!!", pattern));
    
    /* No padding */
    EXPECT_NO_CRASH(signature_from_base64("AAAA", pattern));
    
    /* Extra padding */
    EXPECT_NO_CRASH(signature_from_base64("====", pattern));
    
    /* Mixed case */
    EXPECT_NO_CRASH(signature_from_base64("aBcDeF", pattern));
    
    /* Very long */
    char long_b64[1000];
    memset(long_b64, '!', sizeof(long_b64));
    long_b64[sizeof(long_b64)-1] = '\0';
    EXPECT_NO_CRASH(signature_from_base64(long_b64, pattern));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 3: Shape name parsing
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(shape_name_edge_cases) {
    printf("  Testing shape name parsing with malicious inputs...\n");
    
    /* Empty */
    EXPECT_NO_CRASH(ShapeType s = shape_from_name(""));
    
    /* Very long */
    char long_name[1000];
    memset(long_name, 'z', sizeof(long_name));
    long_name[sizeof(long_name)-1] = '\0';
    EXPECT_NO_CRASH(ShapeType s = shape_from_name(long_name));
    
    /* Null */
    EXPECT_NO_CRASH(ShapeType s = shape_from_name(NULL));
    
    /* Special chars */
    EXPECT_NO_CRASH(ShapeType s = shape_from_name("; rm -rf /"));
    EXPECT_NO_CRASH(ShapeType s = shape_from_name("../etc/passwd"));
    EXPECT_NO_CRASH(ShapeType s = shape_from_name("${HOME}"));
    
    /* Numbers */
    EXPECT_NO_CRASH(ShapeType s = shape_from_name("12345"));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 4: Signature to hex conversion
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(signature_to_hex_edge_cases) {
    printf("  Testing signature to hex conversion...\n");
    
    /* Normal case */
    uint8_t pattern[64];
    memset(pattern, 0xAB, 64);
    char hex[129];
    EXPECT_NO_CRASH(signature_to_hex(pattern, hex));
    
    /* All zeros */
    memset(pattern, 0, 64);
    EXPECT_NO_CRASH(signature_to_hex(pattern, hex));
    
    /* All 0xFF */
    memset(pattern, 0xFF, 64);
    EXPECT_NO_CRASH(signature_to_hex(pattern, hex));
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 5: SoftChipSpec initialization
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(spec_init_edge_cases) {
    printf("  Testing spec initialization...\n");
    
    /* Normal init */
    SoftChipSpec spec;
    EXPECT_NO_CRASH(softchip_init(&spec));
    
    /* NULL (should handle gracefully) */
    printf("  Testing NULL spec init...");
    int result = softchip_init(NULL);
    if (result != TRIX_OK) {
        printf(" (correctly rejected NULL) OK\n");
        tests_passed++;
    } else {
        printf(" FAIL - should have returned error\n");
        tests_failed++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 6: String parsing (softchip_parse_string)
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(string_parsing_edge_cases) {
    printf("  Testing string parsing edge cases...\n");
    
    SoftChipSpec spec;
    
    /* Empty string */
    EXPECT_NO_CRASH(softchip_parse_string("", &spec));
    
    /* Just newlines */
    EXPECT_NO_CRASH(softchip_parse_string("\n\n\n", &spec));
    
    /* Just comments */
    EXPECT_NO_CRASH(softchip_parse_string("# comment\n# another\n", &spec));
    
    /* Very long line */
    char long_line[10000];
    memset(long_line, 'a', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    EXPECT_NO_CRASH(softchip_parse_string(long_line, &spec));
    
    /* Many colons */
    char many_colons[1000];
    memset(many_colons, ':', sizeof(many_colons) - 1);
    many_colons[sizeof(many_colons) - 1] = '\0';
    EXPECT_NO_CRASH(softchip_parse_string(many_colons, &spec));
    
    /* No colon (non-key-value) */
    EXPECT_NO_CRASH(softchip_parse_string("just some text without colons", &spec));
    
    /* Null inputs */
    printf("  Testing NULL string parse...");
    int result = softchip_parse_string(NULL, &spec);
    if (result != TRIX_OK) {
        printf(" (correctly rejected NULL) OK\n");
        tests_passed++;
    } else {
        printf(" FAIL - should have returned error\n");
        tests_failed++;
    }
    
    /* NULL spec */
    printf("  Testing NULL spec parse...");
    result = softchip_parse_string("test: value", NULL);
    if (result != TRIX_OK) {
        printf(" (correctly rejected NULL) OK\n");
        tests_passed++;
    } else {
        printf(" FAIL - should have returned error\n");
        tests_failed++;
    }
}

/* ═══════════════════════════════════════════════════════════════════════════
 * EDGE CASE 7: Memory exhaustion simulation
 * ═══════════════════════════════════════════════════════════════════════════ */

TEST(memory_edge_cases) {
    printf("  Testing memory edge cases...\n");
    
    /* Parse valid spec */
    const char* valid_spec = 
        "softchip:\n"
        "  name: test\n"
        "  version: 1.0\n"
        "state:\n"
        "  bits: 512\n"
        "shapes:\n"
        "  - xor\n"
        "  - and\n";
    
    SoftChipSpec spec;
    
    /* Multiple parses shouldn't leak */
    for (int i = 0; i < 100; i++) {
        softchip_init(&spec);
        softchip_parse_string(valid_spec, &spec);
    }
    printf("  100 iterations: OK\n");
    tests_passed++;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * MAIN
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(int argc, char** argv) {
    printf("╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║     TriX Parser Security Tests (Red-Teaming)                        ║\n");
    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    
    RUN_TEST(hex_parsing_edge_cases);
    RUN_TEST(base64_edge_cases);
    RUN_TEST(shape_name_edge_cases);
    RUN_TEST(signature_to_hex_edge_cases);
    RUN_TEST(spec_init_edge_cases);
    RUN_TEST(string_parsing_edge_cases);
    RUN_TEST(memory_edge_cases);
    
    printf("\n╔══════════════════════════════════════════════════════════════════════╗\n");
    printf("║                           RESULTS                                    ║\n");
    printf("╠══════════════════════════════════════════════════════════════════════╣\n");
    printf("║  Tests Passed: %d                                                    ║\n", tests_passed);
    printf("║  Tests Failed: %d                                                    ║\n", tests_failed);
    printf("╚══════════════════════════════════════════════════════════════════════╝\n");
    
    return tests_failed > 0 ? 1 : 0;
}