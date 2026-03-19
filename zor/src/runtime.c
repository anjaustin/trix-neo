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

#include "../../tools/include/softchip.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_SIGNATURES 128
#define STATE_BYTES 64

/*
 * Internal chip structure
 */
struct trix_chip {
    /* Chip metadata */
    char name[64];
    char version[16];
    int state_bits;
    
    /* Signatures */
    uint8_t signatures[MAX_SIGNATURES][STATE_BYTES];
    int thresholds[MAX_SIGNATURES];
    int shapes[MAX_SIGNATURES];
    char labels[MAX_SIGNATURES][64];
    int num_signatures;
    
    /* Shapes */
    int num_shapes;
    
    /* Linear layers */
    int num_linear_layers;
    
    /* Mode */
    int mode;  /* 0 = first_match, 1 = all_match */
    char default_label[64];
};

/* Portable popcount (works everywhere) */
static int popcount64(const uint8_t* a, const uint8_t* b) {
    if (!a || !b) return 512;  /* Max distance for NULL input */
    
    int count = 0;
    for (int i = 0; i < STATE_BYTES; i++) {
        uint8_t x = a[i] ^ b[i];
        while (x) {
            count += x & 1;
            x >>= 1;
        }
    }
    return count;
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
    for (int i = 0; i < spec.num_signatures && i < MAX_SIGNATURES; i++) {
        memcpy(chip->signatures[i], spec.signatures[i].pattern, STATE_BYTES);
        chip->thresholds[i] = spec.signatures[i].threshold;
        chip->shapes[i] = spec.signatures[i].shape_index;
        strncpy(chip->labels[i], spec.signatures[i].name, sizeof(chip->labels[i]) - 1);
        
        log_debug("Loaded signature %d: %s (threshold=%d, shape=%d)", 
                  i, chip->labels[i], chip->thresholds[i], chip->shapes[i]);
    }
    
    log_info("Loaded chip '%s' with %d signatures", chip->name, chip->num_signatures);
    
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
    return sizeof(trix_chip_t);
}

/*
 * Free chip
 */
void trix_chip_free(trix_chip_t* chip) {
    if (chip) {
        log_info("Freeing chip '%s'", chip->name);
        free(chip);
    }
}

/*
 * Run inference
 */
trix_result_t trix_infer(const trix_chip_t* chip, const uint8_t input[64]) {
    trix_result_t result = {-1, 512, 0, NULL};
    
    if (!chip || !input) {
        result.label = "error";
        return result;
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
        
        int dist = popcount64(input, chip->signatures[i]);
        
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
    
    for (int i = 0; i < chip->num_signatures && num_matches < max_matches; i++) {
        int dist = popcount64(input, chip->signatures[i]);
        
        if (dist <= chip->thresholds[i]) {
            matches[num_matches].match = i;
            matches[num_matches].distance = dist;
            matches[num_matches].threshold = chip->thresholds[i];
            matches[num_matches].label = chip->labels[i];
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