/*
 * runtime.c — TriX Runtime Engine
 *
 * Load and run soft chips.
 * Uses the toolchain's parser for .trix files.
 *
 * Copyright 2026 TriX Research
 */

#include "../include/trixc/runtime.h"
#include "../include/trixc/errors.h"
#include "../include/trixc/memory.h"
#include "../include/trixc/logging.h"

#include "../include/trixc/chip_private.h"
#include "../include/trixc/linear_runtime.h"

#include "../../tools/include/softchip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>


/*
 * SIMD Popcount Implementations
 */

#include <stdatomic.h>

#if defined(__ARM_NEON) || defined(__ARM_NEON__)
#include <arm_neon.h>
#endif

#if defined(__AVX2__)
#include <immintrin.h>
#endif

/* Function pointer type */
typedef int (*popcount_fn)(const uint8_t* a, const uint8_t* b);

/* Portable popcount fallback */
static int popcount64_portable(const uint8_t* a, const uint8_t* b) {
    if (!a || !b) return 512;
    
    int count = 0;
    for (int i = 0; i < CHIP_STATE_BYTES; i++) {
        uint8_t x = a[i] ^ b[i];
        while (x) {
            count += x & 1;
            x >>= 1;
        }
    }
    return count;
}

/* ARM NEON popcount - uses vcnt (population count) */
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
static int popcount64_neon(const uint8_t* a, const uint8_t* b) {
    if (!a || !b) return 512;
    
    /* Load 64 bytes in 4x16 vectors - unaligned safe */
    uint8x16_t a0 = vld1q_u8(a);
    uint8x16_t a1 = vld1q_u8(a + 16);
    uint8x16_t a2 = vld1q_u8(a + 32);
    uint8x16_t a3 = vld1q_u8(a + 48);
    
    uint8x16_t b0 = vld1q_u8(b);
    uint8x16_t b1 = vld1q_u8(b + 16);
    uint8x16_t b2 = vld1q_u8(b + 32);
    uint8x16_t b3 = vld1q_u8(b + 48);
    
    /* XOR */
    uint8x16_t x0 = veorq_u8(a0, b0);
    uint8x16_t x1 = veorq_u8(a1, b1);
    uint8x16_t x2 = veorq_u8(a2, b2);
    uint8x16_t x3 = veorq_u8(a3, b3);
    
    /* Count bits in each byte using vcntq (16-byte variant) */
    uint8x16_t c0 = vcntq_u8(x0);
    uint8x16_t c1 = vcntq_u8(x1);
    uint8x16_t c2 = vcntq_u8(x2);
    uint8x16_t c3 = vcntq_u8(x3);

    /* Widen to uint16 to avoid uint8 overflow (max total = 512) */
    uint16x8_t w0 = vpaddlq_u8(c0);   /* pairwise widen-add: u8→u16 */
    uint16x8_t w1 = vpaddlq_u8(c1);
    uint16x8_t w2 = vpaddlq_u8(c2);
    uint16x8_t w3 = vpaddlq_u8(c3);

    /* Reduce uint16 lanes */
    uint16x8_t s01 = vaddq_u16(w0, w1);
    uint16x8_t s23 = vaddq_u16(w2, w3);
    uint16x8_t s0123 = vaddq_u16(s01, s23);

    /* Widen to uint32 and reduce */
    uint32x4_t d = vpaddlq_u16(s0123);
    uint64x2_t q = vpaddlq_u32(d);

    return (int)(vgetq_lane_u64(q, 0) + vgetq_lane_u64(q, 1));
}
#endif /* ARM_NEON */

/* x86 AVX2 popcount - with runtime CPU feature check */
#if defined(__AVX2__)
static int popcount64_avx2(const uint8_t* a, const uint8_t* b) {
    if (!a || !b) return 512;
    
    /* Check POPCNT support at runtime */
    static int has_popcnt = -1;
    if (has_popcnt < 0) {
        int ecx;
        __asm__("cpuid" : "=c"(ecx) : "a"(1), "c"(0) : "ebx", "edx");
        has_popcnt = (ecx >> 23) & 1;
    }
    
    if (!has_popcnt) {
        /* Fallback to portable if CPU lacks POPCNT */
        return popcount64_portable(a, b);
    }
    
    __m256i va = _mm256_loadu_si256((const __m256i*)a);
    __m256i vb = _mm256_loadu_si256((const __m256i*)b);
    __m256i x = _mm256_xor_si256(va, vb);
    
    __m256i pop = _mm256_popcnt_u8(x);
    
    __m128i sum_lo = _mm256_castsi256_si128(pop);
    __m128i sum_hi = _mm256_extracti128_si256(pop, 1);
    __m128i sum = _mm_add_epi8(sum_lo, sum_hi);
    sum = _mm_sad_epu8(sum, _mm_setzero_si128());
    
    return (int)_mm_cvtsi128_si32(sum);
}
#endif /* AVX2 */

/* Thread-safe initialization using atomic */
static popcount_fn choose_popcount(void) {
    /* Use static init for thread safety */
    static popcount_fn fn = NULL;
    static int initialized = 0;
    
    if (!initialized) {
#if defined(__ARM_NEON) || defined(__ARM_NEON__)
        fn = popcount64_neon;
#elif defined(__AVX2__)
        fn = popcount64_avx2;
#else
        fn = popcount64_portable;
#endif
        initialized = 1;
    }
    
    return fn;
}

/* Global popcount function with thread-safe initialization */
static popcount_fn g_popcount = NULL;

static int popcount64(const uint8_t* a, const uint8_t* b) {
    if (!g_popcount) {
        g_popcount = choose_popcount();
    }
    return g_popcount(a, b);
}


/*
 * Load chip from .trix specification file
 */
trix_chip_t* trix_load(const char* filename, int* error) {
    if (!filename) {
        if (error) *error = TRIX_ERROR_NULL_POINTER;
        return NULL;
    }
    
    trix_log_init();
    
    /* Parse using the toolchain */
    SoftChipSpec spec;
    int parse_result = softchip_parse(filename, &spec);
    
    if (parse_result != TRIX_OK) {
        if (error) *error = parse_result;
        log_error("trix_load: Failed to parse '%s' (error=%d)", filename, parse_result);
        return NULL;
    }
    
    /* Allocate runtime chip */
    trix_chip_t* chip = (trix_chip_t*)calloc(1, sizeof(trix_chip_t));
    if (!chip) {
        if (error) *error = TRIX_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    
    /* Copy metadata */
    strncpy(chip->name, spec.name, sizeof(chip->name) - 1);
    strncpy(chip->version, spec.version, sizeof(chip->version) - 1);
    chip->state_bits = spec.state_bits;
    chip->num_signatures = spec.num_signatures;
    chip->num_shapes = spec.num_shapes;
    chip->num_linear_layers = spec.num_linear_layers;
    chip->mode = spec.mode;
    strncpy(chip->default_label, spec.default_label, sizeof(chip->default_label) - 1);
    
    /* Copy signatures - THE KEY PART */
    for (int i = 0; i < spec.num_signatures && i < CHIP_MAX_SIGNATURES; i++) {
        memcpy(chip->signatures[i], spec.signatures[i].pattern, CHIP_STATE_BYTES);
        chip->thresholds[i] = spec.signatures[i].threshold;
        chip->shapes[i] = spec.signatures[i].shape_index;
        strncpy(chip->labels[i], spec.signatures[i].name, sizeof(chip->labels[i]) - 1);
        
        log_debug("Loaded signature %d: %s (threshold=%d, shape=%d)", 
                  i, chip->labels[i], chip->thresholds[i], chip->shapes[i]);
    }
    
    log_info("Loaded chip '%s' with %d signatures", chip->name, chip->num_signatures);

    /* Load linear layer weights */
    for (int i = 0; i < spec.num_linear_layers && i < CHIP_MAX_LINEAR_LAYERS; i++) {
        chip->layer_input_dim[i]  = spec.linear_layers[i].input_dim;
        chip->layer_output_dim[i] = spec.linear_layers[i].output_dim;
        chip->layer_weights[i]    = NULL;
        chip->layer_weights_packed[i] = NULL;

        if (spec.linear_layers[i].weights_file[0] == '\0') {
            log_debug("Layer %d: no weights file", i);
            continue;
        }

        int K = spec.linear_layers[i].input_dim;
        int N = spec.linear_layers[i].output_dim;
        size_t weight_bytes = (size_t)N * K;

        FILE *wf = fopen(spec.linear_layers[i].weights_file, "rb");
        if (!wf) {
            log_error("trix_load: cannot open weights '%s'",
                      spec.linear_layers[i].weights_file);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_FILE_NOT_FOUND;
            return NULL;
        }

        chip->layer_weights[i] = (int8_t*)malloc(weight_bytes);
        if (!chip->layer_weights[i]) {
            fclose(wf);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_OUT_OF_MEMORY;
            return NULL;
        }

        size_t nread = fread(chip->layer_weights[i], 1, weight_bytes, wf);
        fclose(wf);

        if (nread != weight_bytes) {
            log_error("trix_load: weights file '%s' too short: got %zu, want %zu",
                      spec.linear_layers[i].weights_file, nread, weight_bytes);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_FILE_READ;
            return NULL;
        }

        log_debug("Layer %d: loaded %d×%d weights (%zu bytes)",
                  i, N, K, weight_bytes);
    }

    /* Validate constraints on linear layers */
    if (chip->num_linear_layers > 0) {
        /* First layer input_dim must fit in the 64-byte input and be 4-byte aligned */
        if (chip->layer_input_dim[0] > 64 || chip->layer_input_dim[0] % 4 != 0) {
            log_error("trix_load: layer 0 input_dim=%d must be <=64 and divisible by 4",
                      chip->layer_input_dim[0]);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_INVALID_DIMENSIONS;
            return NULL;
        }
        /* Last layer output_dim must equal state_bits */
        int last = chip->num_linear_layers - 1;
        if (chip->layer_output_dim[last] != chip->state_bits) {
            log_error("trix_load: last linear layer output_dim=%d != state_bits=%d",
                      chip->layer_output_dim[last], chip->state_bits);
            trix_chip_free(chip);
            if (error) *error = TRIX_ERROR_INVALID_DIMENSIONS;
            return NULL;
        }
    }

    if (error) *error = TRIX_OK;
    return chip;
}

/*
 * Load from compiled binary (stub for future)
 */
trix_chip_t* trix_load_binary(const uint8_t* data, size_t len, int* error) {
    (void)data;
    (void)len;
    if (error) *error = TRIX_ERROR_NOT_IMPLEMENTED;
    return NULL;
}

/*
 * Get chip information
 */
const trix_chip_info_t* trix_info(const trix_chip_t* chip) {
    static trix_chip_info_t info;
    
    if (!chip) return NULL;
    
    info.name = chip->name;
    info.version = chip->version;
    info.state_bits = chip->state_bits;
    info.num_signatures = chip->num_signatures;
    info.num_shapes = chip->num_shapes;
    info.num_linear_layers = chip->num_linear_layers;
    
    return &info;
}

/*
 * Get memory footprint
 */
size_t trix_memory_footprint(const trix_chip_t* chip) {
    if (!chip) return 0;
    size_t total = sizeof(trix_chip_t);
    for (int i = 0; i < chip->num_linear_layers; i++) {
        if (chip->layer_weights[i]) {
            total += (size_t)chip->layer_output_dim[i] * chip->layer_input_dim[i];
        }
        if (chip->layer_weights_packed[i]) {
            total += (size_t)(chip->layer_output_dim[i] / 2) * (chip->layer_input_dim[i] / 8) * 16;
        }
    }
    return total;
}

/*
 * Free chip
 */
void trix_chip_free(trix_chip_t* chip) {
    if (!chip) return;
    log_info("Freeing chip '%s'", chip->name);
    for (int i = 0; i < CHIP_MAX_LINEAR_LAYERS; i++) {
        free(chip->layer_weights[i]);
        free(chip->layer_weights_packed[i]);
    }
    free(chip);
}

/*
 * Run inference
 */
trix_result_t trix_infer(const trix_chip_t* chip, const uint8_t input[64]) {
    trix_result_t result = {-1, 512, 0, NULL, 0, 0};
    
    if (!chip || !input) {
        result.label = "error";
        return result;
    }

    /* Apply linear layers if present */
    uint8_t linear_out[64];
    const uint8_t *effective_input = input;

    if (chip->num_linear_layers > 0) {
        int lin_rc = trix_exec_linear(chip, input, linear_out);
        if (lin_rc != TRIX_OK) {
            result.label = "error";
            return result;
        }
        effective_input = linear_out;
    }

    if (chip->num_signatures <= 0) {
        result.label = NULL;
        return result;
    }
    
    int best_distance = 512;
    int best_index = -1;
    int best_threshold = 0;
    
    /* Check each signature */
    for (int i = 0; i < chip->num_signatures; i++) {
        /* Skip if threshold is invalid (negative) but NOT zero */
        if (chip->thresholds[i] < 0) continue;

        int dist = popcount64(effective_input, chip->signatures[i]);
        
        if (dist <= chip->thresholds[i] && dist < best_distance) {
            best_distance = dist;
            best_index = i;
            best_threshold = chip->thresholds[i];
            
            if (chip->mode == 0) {
                /* First match mode - found best match, stop searching */
                break;
            }
        }
    }
    
    result.match = best_index;
    result.distance = (best_index >= 0) ? best_distance : 512;
    result.threshold = best_threshold;
    result.label = (best_index >= 0) ? chip->labels[best_index] : NULL;
    
    return result;
}

/*
 * Infer all matches
 */
int trix_infer_all(const trix_chip_t* chip, const uint8_t input[64],
                   trix_result_t* matches, int max_matches) {
    if (!chip || !input || !matches || max_matches <= 0) {
        return 0;
    }
    
    int num_matches = 0;

    /* Apply linear layers if present */
    uint8_t linear_out_all[64];
    const uint8_t *effective_input = input;

    if (chip->num_linear_layers > 0) {
        int lin_rc = trix_exec_linear(chip, input, linear_out_all);
        if (lin_rc != TRIX_OK) return 0;
        effective_input = linear_out_all;
    }

    for (int i = 0; i < chip->num_signatures && num_matches < max_matches; i++) {
        int dist = popcount64(effective_input, chip->signatures[i]);
        
        if (dist <= chip->thresholds[i]) {
            matches[num_matches].match = i;
            matches[num_matches].distance = dist;
            matches[num_matches].threshold = chip->thresholds[i];
            matches[num_matches].label = chip->labels[i];
            matches[num_matches].trace_tick_start = 0;
            matches[num_matches].trace_tick_end   = 0;
            num_matches++;
        }
    }
    
    return num_matches;
}

/*
 * Get label for signature index
 */
const char* trix_label(const trix_chip_t* chip, int index) {
    if (!chip || index < 0 || index >= chip->num_signatures) {
        return NULL;
    }
    return chip->labels[index];
}

/*
 * Get threshold for signature index
 */
int trix_threshold(const trix_chip_t* chip, int index) {
    if (!chip || index < 0 || index >= chip->num_signatures) {
        return 0;
    }
    return chip->thresholds[index];
}

/*
 * Get signature pattern
 */
const uint8_t* trix_signature(const trix_chip_t* chip, int index) {
    if (!chip || index < 0 || index >= chip->num_signatures) {
        return NULL;
    }
    return chip->signatures[index];
}

/*
 * Get shape for signature
 */
int trix_shape(const trix_chip_t* chip, int index) {
    if (!chip || index < 0 || index >= chip->num_signatures) {
        return 0;
    }
    return chip->shapes[index];
}