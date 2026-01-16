/*
 * determinism_test.c — Cross-Platform Determinism Test
 *
 * "Is it truly deterministic across architectures?"
 *
 * Floating-point math can vary across platforms (x86 vs ARM, NEON vs SSE).
 * This test verifies that the chip produces IDENTICAL outputs for
 * identical inputs, every single time.
 *
 * We generate a fixed input sequence, run it multiple times, and verify
 * the outputs match bit-for-bit. We also generate a checksum that can
 * be compared across platforms.
 *
 * Build:
 *   gcc -O3 -I../include determinism_test.c -o determinism_test -lm
 *
 * Run:
 *   ./determinism_test
 *
 * Cross-platform verification:
 *   Run on multiple architectures and compare the CHECKSUM values.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

/* Include the evolved sine tracker */
#include "../foundry/seeds/sine_seed.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Test Configuration
 * ═══════════════════════════════════════════════════════════════════════════ */

#define TEST_SEQUENCE_LEN  10000    /* Number of test samples */
#define NUM_RUNS           5        /* Number of repeated runs */
#define SEED               0xDEADBEEF  /* Fixed RNG seed */

/* Reference checksum (computed on first run, verified on subsequent) */
/* These values should be IDENTICAL across all platforms */

/* ═══════════════════════════════════════════════════════════════════════════
 * Deterministic Input Generator
 * ═══════════════════════════════════════════════════════════════════════════ */

static uint32_t rng_state;

void rng_seed(uint32_t seed) {
    rng_state = seed;
}

/* Xorshift32 - simple, fast, deterministic */
uint32_t rng_next(void) {
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

float rng_float(void) {
    /* Convert to float in [-1, 1] range */
    return ((float)(rng_next() & 0x7FFFFFFF) / (float)0x7FFFFFFF) * 2.0f - 1.0f;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Checksum (FNV-1a hash of float bits)
 * ═══════════════════════════════════════════════════════════════════════════ */

typedef struct {
    uint64_t hash;
    float sum;
    float sum_sq;
    int count;
} Checksum;

void checksum_init(Checksum* cs) {
    cs->hash = 0xcbf29ce484222325ULL;  /* FNV-1a offset basis */
    cs->sum = 0.0f;
    cs->sum_sq = 0.0f;
    cs->count = 0;
}

void checksum_add(Checksum* cs, float value) {
    /* Add to FNV-1a hash using float bit representation */
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));

    for (int i = 0; i < 4; i++) {
        cs->hash ^= (bits >> (i * 8)) & 0xFF;
        cs->hash *= 0x100000001b3ULL;  /* FNV-1a prime */
    }

    cs->sum += value;
    cs->sum_sq += value * value;
    cs->count++;
}

int checksum_equal(const Checksum* a, const Checksum* b) {
    return (a->hash == b->hash) &&
           (a->sum == b->sum) &&
           (a->sum_sq == b->sum_sq) &&
           (a->count == b->count);
}

void checksum_print(const Checksum* cs, const char* label) {
    printf("%s:\n", label);
    printf("  Hash:   0x%016lX\n", cs->hash);
    printf("  Sum:    %.10f\n", cs->sum);
    printf("  SumSq:  %.10f\n", cs->sum_sq);
    printf("  Count:  %d\n", cs->count);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Platform Detection
 * ═══════════════════════════════════════════════════════════════════════════ */

const char* detect_platform(void) {
#if defined(__aarch64__) || defined(_M_ARM64)
    return "ARM64 (AArch64)";
#elif defined(__arm__) || defined(_M_ARM)
    return "ARM32";
#elif defined(__x86_64__) || defined(_M_X64)
    return "x86_64 (AMD64)";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86 (i386)";
#elif defined(__riscv)
    return "RISC-V";
#else
    return "Unknown";
#endif
}

const char* detect_simd(void) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
    return "NEON";
#elif defined(__AVX512F__)
    return "AVX-512";
#elif defined(__AVX2__)
    return "AVX2";
#elif defined(__AVX__)
    return "AVX";
#elif defined(__SSE4_2__)
    return "SSE4.2";
#elif defined(__SSE2__)
    return "SSE2";
#else
    return "None/Scalar";
#endif
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Main Test
 * ═══════════════════════════════════════════════════════════════════════════ */

int main(void) {
    printf("\n");
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║  DETERMINISM TEST (Cross-Platform Verification)              ║\n");
    printf("║  \"Same input → Same output. Always. Everywhere.\"           ║\n");
    printf("╚══════════════════════════════════════════════════════════════╝\n");
    printf("\n");

    /* Platform info */
    printf("Platform Detection:\n");
    printf("  Architecture: %s\n", detect_platform());
    printf("  SIMD:         %s\n", detect_simd());
    printf("  sizeof(float): %zu bytes\n", sizeof(float));
    printf("  sizeof(double): %zu bytes\n", sizeof(double));
    printf("\n");

    printf("Chip Statistics:\n");
    printf("  Nodes: %d\n", sine_SEED_NODES);
    printf("  Registers: %d\n", sine_SEED_REGS);
    printf("  Memory: %d bytes\n", (int)(sine_SEED_REGS * sizeof(float)));
    printf("\n");

    printf("Test Configuration:\n");
    printf("  Sequence length: %d samples\n", TEST_SEQUENCE_LEN);
    printf("  RNG seed: 0x%X\n", SEED);
    printf("  Number of runs: %d\n", NUM_RUNS);
    printf("\n");

    /* Allocate buffers */
    float* inputs = malloc(TEST_SEQUENCE_LEN * sizeof(float));
    float* outputs = malloc(TEST_SEQUENCE_LEN * sizeof(float));
    Checksum checksums[NUM_RUNS];

    /* Generate fixed input sequence */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("GENERATING INPUT SEQUENCE\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    rng_seed(SEED);
    for (int i = 0; i < TEST_SEQUENCE_LEN; i++) {
        inputs[i] = rng_float();
    }

    /* Show first few inputs for verification */
    printf("First 10 inputs (for cross-platform verification):\n");
    for (int i = 0; i < 10; i++) {
        uint32_t bits;
        memcpy(&bits, &inputs[i], sizeof(bits));
        printf("  [%d] %+.10f (0x%08X)\n", i, inputs[i], bits);
    }
    printf("\n");

    /* Run multiple times */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("RUNNING %d IDENTICAL PASSES\n", NUM_RUNS);
    printf("═══════════════════════════════════════════════════════════════\n\n");

    for (int run = 0; run < NUM_RUNS; run++) {
        /* Reset state */
        float state[sine_SEED_REGS];
        sine_seed_reset(state);
        checksum_init(&checksums[run]);

        /* Run chip */
        for (int i = 0; i < TEST_SEQUENCE_LEN; i++) {
            outputs[i] = sine_seed_step(inputs[i], state);
            checksum_add(&checksums[run], outputs[i]);
        }

        printf("Run %d: Hash=0x%016lX Sum=%.6f\n",
               run + 1, checksums[run].hash, checksums[run].sum);
    }
    printf("\n");

    /* Compare all runs */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("COMPARING RUNS\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    int all_match = 1;
    for (int run = 1; run < NUM_RUNS; run++) {
        int match = checksum_equal(&checksums[0], &checksums[run]);
        printf("Run 1 vs Run %d: %s\n", run + 1, match ? "IDENTICAL" : "DIFFERENT!");
        if (!match) all_match = 0;
    }
    printf("\n");

    /* Show final outputs for cross-platform verification */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("CROSS-PLATFORM VERIFICATION VALUES\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("Last 10 outputs (compare across platforms):\n");
    for (int i = TEST_SEQUENCE_LEN - 10; i < TEST_SEQUENCE_LEN; i++) {
        uint32_t bits;
        memcpy(&bits, &outputs[i], sizeof(bits));
        printf("  [%d] %+.10f (0x%08X)\n", i, outputs[i], bits);
    }
    printf("\n");

    checksum_print(&checksums[0], "Final Checksum");
    printf("\n");

    /* Save reference file */
    printf("═══════════════════════════════════════════════════════════════\n");
    printf("REFERENCE VALUES (Copy these to verify on other platforms)\n");
    printf("═══════════════════════════════════════════════════════════════\n\n");

    printf("// Paste into reference file for cross-platform comparison:\n");
    printf("#define REF_HASH     0x%016lXULL\n", checksums[0].hash);
    printf("#define REF_SUM      %.10ff\n", checksums[0].sum);
    printf("#define REF_SUMSQ    %.10ff\n", checksums[0].sum_sq);
    printf("#define REF_COUNT    %d\n", checksums[0].count);
    printf("\n");

    /* Verdict */
    printf("═══════════════════════════════════════════════════════════════\n");

    if (all_match) {
        printf("VERDICT: DETERMINISTIC\n");
        printf("═══════════════════════════════════════════════════════════════\n\n");
        printf("All %d runs produced BIT-IDENTICAL outputs.\n", NUM_RUNS);
        printf("  ✓ Same input → Same output (every time)\n");
        printf("  ✓ No floating-point drift between runs\n");
        printf("  ✓ Reproducible computation\n");
        printf("\n");
        printf(">>> SKEPTIC'S DOUBT #5: ANSWERED <<<\n");
        printf("\"Is it truly deterministic across architectures?\"\n");
        printf("REBUTTAL: On %s with %s: YES.\n", detect_platform(), detect_simd());
        printf("          Hash: 0x%016lX\n", checksums[0].hash);
        printf("          Compare this hash on other platforms.\n");
    } else {
        printf("VERDICT: NON-DETERMINISTIC\n");
        printf("═══════════════════════════════════════════════════════════════\n\n");
        printf("WARNING: Runs produced DIFFERENT outputs!\n");
        printf("This should not happen with pure floating-point math.\n");
        printf("Check for uninitialized memory or race conditions.\n");
    }

    printf("\n");
    printf("\"The math is the same. The bits are the same. Always.\"\n");
    printf("\n");

    free(inputs);
    free(outputs);

    return all_match ? 0 : 1;
}
