/*
 * runtime.c — TriX Runtime Engine
 *
 * Load and run soft chips.
 *
 * Copyright 2026 TriX Research
 */

#include "../include/trixc/runtime.h"
#include "../include/trixc/errors.h"
#include "../include/trixc/memory.h"
#include "../include/trixc/logging.h"
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
    
    /* Use toolchain to parse the spec */
    extern int softchip_parse(const char* filename, void* spec);
    extern int softchip_init(void* spec);
    
    /* We need the toolchain to parse - let's use a simpler approach
     * by parsing the file directly here for now */
    
    FILE* f = fopen(filename, "r");
    if (!f) {
        if (error) *error = TRIX_ERROR_FILE_NOT_FOUND;
        log_error("trix_load: Cannot open file '%s'", filename);
        return NULL;
    }
    
    /* Allocate chip */
    trix_chip_t* chip = (trix_chip_t*)calloc(1, sizeof(trix_chip_t));
    if (!chip) {
        fclose(f);
        if (error) *error = TRIX_ERROR_OUT_OF_MEMORY;
        return NULL;
    }
    
    /* Set defaults */
    chip->state_bits = 512;
    chip->mode = 0;  /* first_match */
    strcpy(chip->default_label, "unknown");
    
    /* Parse the .trix file */
    char line[1024];
    char section[64] = "";
    int current_sig = -1;
    
    while (fgets(line, sizeof(line), f)) {
        /* Trim whitespace */
        char* p = line;
        while (*p == ' ' || *p == '\t') p++;
        char* end = p + strlen(p) - 1;
        while (end > p && (*end == '\n' || *end == '\r' || *end == ' ')) *end-- = '\0';
        
        if (*p == '\0' || *p == '#') continue;
        
        /* Section headers */
        if (strncmp(p, "softchip:", 9) == 0) {
            strcpy(section, "softchip");
            continue;
        }
        if (strncmp(p, "state:", 6) == 0) {
            strcpy(section, "state");
            continue;
        }
        if (strncmp(p, "shapes:", 7) == 0) {
            strcpy(section, "shapes");
            continue;
        }
        if (strncmp(p, "signatures:", 11) == 0) {
            strcpy(section, "signatures");
            continue;
        }
        if (strncmp(p, "inference:", 10) == 0) {
            strcpy(section, "inference");
            continue;
        }
        
        /* Key:value pairs */
        char* colon = strchr(p, ':');
        if (!colon) continue;
        
        *colon = '\0';
        char* key = p;
        char* value = colon + 1;
        
        /* Skip leading whitespace in key/value */
        while (*key == ' ') key++;
        while (*value == ' ') value++;
        
        /* Remove trailing whitespace from key */
        char* key_end = key + strlen(key) - 1;
        while (key_end > key && *key_end == ' ') *key_end-- = '\0';
        
        /* Parse based on section */
        if (strcmp(section, "softchip") == 0) {
            if (strcmp(key, "name") == 0) {
                strncpy(chip->name, value, sizeof(chip->name) - 1);
            } else if (strcmp(key, "version") == 0) {
                strncpy(chip->version, value, sizeof(chip->version) - 1);
            }
        } else if (strcmp(section, "state") == 0) {
            if (strcmp(key, "bits") == 0) {
                chip->state_bits = atoi(value);
            }
        } else if (strcmp(section, "signatures") == 0) {
            /* Check for new signature (indent <= 2 spaces, not a property) */
            int indent = 0;
            const char* lp = line;
            while (*lp == ' ') { indent++; lp++; }
            
            if (indent <= 2 && strcmp(key, "pattern") != 0 &&
                strcmp(key, "threshold") != 0 && strcmp(key, "shape") != 0) {
                /* New signature */
                if (chip->num_signatures < MAX_SIGNATURES) {
                    current_sig = chip->num_signatures++;
                    strncpy(chip->labels[current_sig], key, sizeof(chip->labels[current_sig]) - 1);
                    chip->thresholds[current_sig] = 32;  /* Default */
                    chip->shapes[current_sig] = 0;       /* Default: xor */
                }
            } else if (current_sig >= 0) {
                if (strcmp(key, "pattern") == 0) {
                    /* Parse hex pattern */
                    const char* hex = value;
                    size_t hex_len = strlen(hex);
                    for (size_t i = 0; i < STATE_BYTES * 2 && i < hex_len; i += 2) {
                        char byte_str[3] = {hex[i], hex[i+1], '\0'};
                        chip->signatures[current_sig][i/2] = (uint8_t)strtol(byte_str, NULL, 16);
                    }
                } else if (strcmp(key, "threshold") == 0) {
                    chip->thresholds[current_sig] = atoi(value);
                } else if (strcmp(key, "shape") == 0) {
                    if (strcmp(value, "xor") == 0) chip->shapes[current_sig] = 0;
                    else if (strcmp(value, "and") == 0) chip->shapes[current_sig] = 1;
                    else if (strcmp(value, "or") == 0) chip->shapes[current_sig] = 2;
                    else if (strcmp(value, "sigmoid") == 0) chip->shapes[current_sig] = 8;
                }
            }
        } else if (strcmp(section, "inference") == 0) {
            if (strcmp(key, "mode") == 0) {
                if (strcmp(value, "all_match") == 0) {
                    chip->mode = 1;
                }
            } else if (strcmp(key, "default") == 0) {
                strncpy(chip->default_label, value, sizeof(chip->default_label) - 1);
            }
        }
    }
    
    fclose(f);
    
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
    
    int best_distance = 512;
    int best_index = -1;
    int best_threshold = 0;
    
    /* Check each signature */
    for (int i = 0; i < chip->num_signatures; i++) {
        int dist = popcount64(input, chip->signatures[i]);
        
        if (dist <= chip->thresholds[i] && dist < best_distance) {
            best_distance = dist;
            best_index = i;
            best_threshold = chip->thresholds[i];
            
            if (chip->mode == 0) {
                /* First match mode - stop at first match */
                break;
            }
        }
    }
    
    result.match = best_index;
    result.distance = best_distance;
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