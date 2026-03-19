/*
 * softchip.c — Soft Chip parsing and utilities
 *
 * Part of TriX toolchain
 * Simple line-based parser for .trix specs
 * 
 * Production-hardened with error handling and logging
 */

#include "../include/softchip.h"
#include "../../zor/include/trixc/memory.h"  /* Safe memory operations */
#include "../../zor/include/trixc/errors.h"   /* Error handling */
#include "../../zor/include/trixc/logging.h"  /* Logging system */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Toolchain-specific error codes */
typedef enum {
    TRIX_SOFTCHIP_ERROR_BASE = 0x2000,
    TRIX_SOFTCHIP_ERROR_FILE_OPEN = TRIX_SOFTCHIP_ERROR_BASE + 1,
    TRIX_SOFTCHIP_ERROR_PARSE_INVALID_FORMAT = TRIX_SOFTCHIP_ERROR_BASE + 2,
    TRIX_SOFTCHIP_ERROR_PARSE_INVALID_HEX = TRIX_SOFTCHIP_ERROR_BASE + 3,
    TRIX_SOFTCHIP_ERROR_PARSE_INVALID_BASE64 = TRIX_SOFTCHIP_ERROR_BASE + 4,
    TRIX_SOFTCHIP_ERROR_INVALID_SHAPE = TRIX_SOFTCHIP_ERROR_BASE + 5,
    TRIX_SOFTCHIP_ERROR_INVALID_LAYOUT = TRIX_SOFTCHIP_ERROR_BASE + 6,
    TRIX_SOFTCHIP_ERROR_INVALID_MODE = TRIX_SOFTCHIP_ERROR_BASE + 7,
    TRIX_SOFTCHIP_ERROR_TOO_MANY_SIGNATURES = TRIX_SOFTCHIP_ERROR_BASE + 8,
    TRIX_SOFTCHIP_ERROR_TOO_MANY_SHAPES = TRIX_SOFTCHIP_ERROR_BASE + 9,
    TRIX_SOFTCHIP_ERROR_TOO_MANY_LINEAR_LAYERS = TRIX_SOFTCHIP_ERROR_BASE + 10,
    TRIX_SOFTCHIP_ERROR_INVALID_LINEAR_ACTIVATION = TRIX_SOFTCHIP_ERROR_BASE + 11,
    TRIX_SOFTCHIP_ERROR_INVALID_THRESHOLD = TRIX_SOFTCHIP_ERROR_BASE + 12,
    TRIX_SOFTCHIP_ERROR_INVALID_STATE_BITS = TRIX_SOFTCHIP_ERROR_BASE + 13,
    TRIX_SOFTCHIP_ERROR_INVALID_DIMENSION = TRIX_SOFTCHIP_ERROR_BASE + 14,
    TRIX_SOFTCHIP_ERROR_INVALID_INDENT = TRIX_SOFTCHIP_ERROR_BASE + 15,
    TRIX_SOFTCHIP_ERROR_BUFFER_TOO_SMALL = TRIX_SOFTCHIP_ERROR_BASE + 16,
    TRIX_SOFTCHIP_ERROR_NULL_POINTER = TRIX_SOFTCHIP_ERROR_BASE + 17,
} TriXSoftChipError;

/* Shape name lookup table */
static const char* SHAPE_NAMES[] = {
    "xor", "and", "or", "not", "nand", "nor", "xnor",
    "relu", "sigmoid", "identity"
};

const char* shape_name(ShapeType type) {
    if (type >= 0 && type < SHAPE_COUNT) {
        return SHAPE_NAMES[type];
    }
    return "unknown";
}

ShapeType shape_from_name(const char* name) {
    if (!name) {
        return SHAPE_XOR;  /* Default on NULL */
    }
    for (int i = 0; i < SHAPE_COUNT; i++) {
        if (strcasecmp(name, SHAPE_NAMES[i]) == 0) {
            return (ShapeType)i;
        }
    }
    return SHAPE_XOR;  /* Default */
}

int softchip_init(SoftChipSpec* spec) {
    if (!spec) {
        return TRIX_ERROR_NULL_POINTER;
    }
    memset(spec, 0, sizeof(SoftChipSpec));
    trix_strcpy_safe(spec->name, "unnamed", sizeof(spec->name));
    trix_strcpy_safe(spec->version, "1.0.0", sizeof(spec->version));
    spec->state_bits = 512;
    spec->layout = LAYOUT_CUBE;
    spec->mode = MODE_FIRST_MATCH;
    trix_strcpy_safe(spec->default_label, "unknown", sizeof(spec->default_label));
    spec->num_linear_layers = 0;
    return TRIX_OK;
}

/* Parse linear activation name */
static LinearActivation linear_act_from_name(const char* name) {
    if (strcasecmp(name, "relu") == 0) return LINEAR_ACT_RELU;
    if (strcasecmp(name, "sigmoid") == 0) return LINEAR_ACT_SIGMOID;
    if (strcasecmp(name, "tanh") == 0) return LINEAR_ACT_TANH;
    if (strcasecmp(name, "gelu") == 0) return LINEAR_ACT_GELU;
    if (strcasecmp(name, "softmax") == 0) return LINEAR_ACT_SOFTMAX;
    return LINEAR_ACT_NONE;
}

/* Trim whitespace from string */
static char* trim(char* str) {
    while (isspace(*str)) str++;
    if (*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = '\0';
    return str;
}

/* Parse hex string to bytes */
int signature_from_hex(const char* hex, uint8_t* pattern) {
    memset(pattern, 0, STATE_BYTES);
    size_t len = strlen(hex);

    /* Skip "0x" prefix if present */
    if (len >= 2 && hex[0] == '0' && (hex[1] == 'x' || hex[1] == 'X')) {
        hex += 2;
        len -= 2;
    }

    /* Parse hex pairs */
    for (size_t i = 0; i < len && i/2 < STATE_BYTES; i += 2) {
        unsigned int byte;
        if (sscanf(hex + i, "%2x", &byte) == 1) {
            pattern[i/2] = (uint8_t)byte;
        }
    }
    return 0;
}

/* Base64 decode table */
static const int B64_TABLE[256] = {
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
    52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
    -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
    15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
    -1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
    41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
    -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
};

int signature_from_base64(const char* b64, uint8_t* pattern) {
    memset(pattern, 0, STATE_BYTES);

    /* Skip "base64:" prefix if present */
    if (strncmp(b64, "base64:", 7) == 0) {
        b64 += 7;
    }

    size_t len = strlen(b64);
    size_t out_idx = 0;

    for (size_t i = 0; i < len && out_idx < STATE_BYTES; i += 4) {
        int a = B64_TABLE[(unsigned char)b64[i]];
        int b = (i+1 < len) ? B64_TABLE[(unsigned char)b64[i+1]] : 0;
        int c = (i+2 < len) ? B64_TABLE[(unsigned char)b64[i+2]] : 0;
        int d = (i+3 < len) ? B64_TABLE[(unsigned char)b64[i+3]] : 0;

        if (a < 0 || b < 0) break;

        if (out_idx < STATE_BYTES)
            pattern[out_idx++] = (a << 2) | (b >> 4);
        if (out_idx < STATE_BYTES && c >= 0)
            pattern[out_idx++] = ((b & 0xF) << 4) | (c >> 2);
        if (out_idx < STATE_BYTES && d >= 0)
            pattern[out_idx++] = ((c & 0x3) << 6) | d;
    }

    return 0;
}

void signature_to_hex(const uint8_t* pattern, char* hex) {
    for (int i = 0; i < STATE_BYTES; i++) {
        /* Safe: each iteration writes exactly 2 chars, total buffer needs STATE_BYTES*2+1 */
        snprintf(hex + i*2, 3, "%02x", pattern[i]);
    }
    hex[STATE_BYTES * 2] = '\0';
}

/* Validate softchip specification */
static int softchip_validate(const SoftChipSpec* spec, trix_error_context_t* ctx) {
    if (!spec) {
        trix_error_set(ctx, TRIX_ERROR_NULL_POINTER, "SoftChipSpec pointer is NULL");
        return TRIX_ERROR_NULL_POINTER;
    }

    if (spec->state_bits != 128 && spec->state_bits != 256 && 
        spec->state_bits != 512 && spec->state_bits != 1024) {
        log_warn("Non-standard state_bits %d, expected 128/256/512/1024", spec->state_bits);
    }

    if (spec->num_signatures > MAX_SIGNATURES) {
        trix_error_set(ctx, TRIX_ERROR_TOO_MANY_SIGNATURES, 
            "Too many signatures: %d (max %d)", spec->num_signatures, MAX_SIGNATURES);
        return TRIX_ERROR_TOO_MANY_SIGNATURES;
    }

    if (spec->num_shapes > MAX_SHAPES) {
        trix_error_set(ctx, TRIX_ERROR_TOO_MANY_SHAPES,
            "Too many shapes: %d (max %d)", spec->num_shapes, MAX_SHAPES);
        return TRIX_ERROR_TOO_MANY_SHAPES;
    }

    if (spec->num_linear_layers > MAX_LINEAR_LAYERS) {
        trix_error_set(ctx, TRIX_ERROR_TOO_MANY_LAYERS,
            "Too many linear layers: %d (max %d)", spec->num_linear_layers, MAX_LINEAR_LAYERS);
        return TRIX_ERROR_TOO_MANY_LAYERS;
    }

    for (int i = 0; i < spec->num_signatures; i++) {
        const Signature* sig = &spec->signatures[i];
        if (sig->threshold < 0 || sig->threshold > STATE_BYTES * 8) {
            trix_error_set(ctx, TRIX_ERROR_INVALID_THRESHOLD,
                "Invalid threshold %d for signature '%s'", sig->threshold, sig->name);
            return TRIX_ERROR_INVALID_THRESHOLD;
        }
    }

    for (int i = 0; i < spec->num_linear_layers; i++) {
        const LinearLayerSpec* layer = &spec->linear_layers[i];
        if (layer->input_dim <= 0 || layer->output_dim <= 0) {
            trix_error_set(ctx, TRIX_ERROR_INVALID_DIMENSIONS,
                "Invalid dimensions for linear layer '%s': %d -> %d",
                layer->name, layer->input_dim, layer->output_dim);
            return TRIX_ERROR_INVALID_DIMENSIONS;
        }
    }

    return TRIX_OK;
}

/* Simple line-based parser for .trix files */
int softchip_parse(const char* filename, SoftChipSpec* spec) {
    trix_error_context_t ctx_body;
    trix_error_context_t* ctx = &ctx_body;
    trix_error_init(ctx);
    trix_log_init();

    if (!filename || !spec) {
        trix_error_set(ctx, TRIX_ERROR_NULL_POINTER, 
            "filename or spec pointer is NULL");
        log_error("softchip_parse: %s", trix_error_description(ctx ? (trix_error_t)ctx->code : TRIX_ERROR_NULL_POINTER));
        return TRIX_ERROR_NULL_POINTER;
    }

    FILE* f = fopen(filename, "r");
    if (!f) {
        trix_error_set(ctx, TRIX_ERROR_FILE_NOT_FOUND, 
            "Cannot open file '%s'", filename);
        log_error("softchip_parse: %s", trix_error_description(ctx->code));
        return TRIX_ERROR_FILE_NOT_FOUND;
    }

    log_info("Parsing .trix file: %s", filename);

    int init_result = softchip_init(spec);
    if (init_result != TRIX_OK) {
        fclose(f);
        log_error("softchip_parse: Failed to initialize spec");
        return init_result;
    }

    char line[1024];
    char section[64] = "";
    Signature* current_sig = NULL;
    LinearLayerSpec* current_linear = NULL;

    while (fgets(line, sizeof(line), f)) {
        char* p = trim(line);

        /* Skip empty lines and comments */
        if (*p == '\0' || *p == '#') continue;

        /* Section headers */
        if (strncmp(p, "softchip:", 9) == 0) {
            trix_strcpy_safe(section, "softchip", sizeof(section));
            continue;
        }
        if (strncmp(p, "state:", 6) == 0) {
            trix_strcpy_safe(section, "state", sizeof(section));
            continue;
        }
        if (strncmp(p, "shapes:", 7) == 0) {
            trix_strcpy_safe(section, "shapes", sizeof(section));
            continue;
        }
        if (strncmp(p, "signatures:", 11) == 0) {
            trix_strcpy_safe(section, "signatures", sizeof(section));
            continue;
        }
        if (strncmp(p, "inference:", 10) == 0) {
            trix_strcpy_safe(section, "inference", sizeof(section));
            continue;
        }
        if (strncmp(p, "linear:", 7) == 0) {
            trix_strcpy_safe(section, "linear", sizeof(section));
            continue;
        }

        /* Parse key: value pairs */
        char* colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            char* key = trim(p);
            char* value = trim(colon + 1);

            /* Handle based on section */
            if (strcmp(section, "softchip") == 0) {
                if (strcmp(key, "name") == 0) {
                    strncpy(spec->name, value, MAX_NAME_LEN - 1);
                } else if (strcmp(key, "version") == 0) {
                    strncpy(spec->version, value, 15);
                } else if (strcmp(key, "description") == 0) {
                    strncpy(spec->description, value, MAX_DESCRIPTION - 1);
                }
            } else if (strcmp(section, "state") == 0) {
                if (strcmp(key, "bits") == 0) {
                    spec->state_bits = atoi(value);
                } else if (strcmp(key, "layout") == 0) {
                    if (strcmp(value, "cube") == 0) {
                        spec->layout = LAYOUT_CUBE;
                    } else {
                        spec->layout = LAYOUT_FLAT;
                    }
                }
            } else if (strcmp(section, "inference") == 0) {
                if (strcmp(key, "mode") == 0) {
                    if (strcmp(value, "first_match") == 0) {
                        spec->mode = MODE_FIRST_MATCH;
                    } else if (strcmp(value, "all_match") == 0) {
                        spec->mode = MODE_ALL_MATCH;
                    }
                } else if (strcmp(key, "default") == 0) {
                    strncpy(spec->default_label, value, MAX_NAME_LEN - 1);
                }
            } else if (strcmp(section, "signatures") == 0) {
                /* Count leading spaces to determine nesting level */
                int indent = 0;
                const char* lp = line;
                while (*lp == ' ') { indent++; lp++; }

                /* 2 spaces = signature name, 4+ spaces = property */
                if (indent <= 2 && strlen(key) > 0 &&
                    strcmp(key, "pattern") != 0 &&
                    strcmp(key, "threshold") != 0 &&
                    strcmp(key, "shape") != 0) {
                    /* New signature */
                    if (spec->num_signatures < MAX_SIGNATURES) {
                        current_sig = &spec->signatures[spec->num_signatures++];
                        memset(current_sig, 0, sizeof(Signature));
                        strncpy(current_sig->name, key, MAX_NAME_LEN - 1);
                        current_sig->threshold = 64;  /* Default */
                        current_sig->shape_index = SHAPE_SIGMOID;  /* Default */
                    }
                } else if (current_sig) {
                    /* Property of current signature */
                    if (strcmp(key, "pattern") == 0) {
                        if (strncmp(value, "base64:", 7) == 0) {
                            signature_from_base64(value, current_sig->pattern);
                        } else {
                            signature_from_hex(value, current_sig->pattern);
                        }
                    } else if (strcmp(key, "threshold") == 0) {
                        current_sig->threshold = atoi(value);
                    } else if (strcmp(key, "shape") == 0) {
                        current_sig->shape_index = shape_from_name(value);
                    }
                }
            } else if (strcmp(section, "linear") == 0) {
                /* Linear Kingdom layer parsing */
                /* Count leading spaces to determine nesting level */
                int indent = 0;
                const char* lp = line;
                while (*lp == ' ') { indent++; lp++; }

                /* 2 spaces = layer name, 4+ spaces = property */
                if (indent <= 2 && strlen(key) > 0 &&
                    strcmp(key, "input_dim") != 0 &&
                    strcmp(key, "output_dim") != 0 &&
                    strcmp(key, "weights") != 0 &&
                    strcmp(key, "bias") != 0 &&
                    strcmp(key, "activation") != 0) {
                    /* New linear layer */
                    if (spec->num_linear_layers < MAX_LINEAR_LAYERS) {
                        current_linear = &spec->linear_layers[spec->num_linear_layers++];
                        memset(current_linear, 0, sizeof(LinearLayerSpec));
                        strncpy(current_linear->name, key, MAX_NAME_LEN - 1);
                        current_linear->activation = LINEAR_ACT_NONE;
                    }
                } else if (current_linear) {
                    /* Property of current linear layer */
                    if (strcmp(key, "input_dim") == 0) {
                        current_linear->input_dim = atoi(value);
                    } else if (strcmp(key, "output_dim") == 0) {
                        current_linear->output_dim = atoi(value);
                    } else if (strcmp(key, "weights") == 0) {
                        strncpy(current_linear->weights_file, value, 255);
                    } else if (strcmp(key, "bias") == 0) {
                        strncpy(current_linear->bias_file, value, 255);
                    } else if (strcmp(key, "activation") == 0) {
                        current_linear->activation = linear_act_from_name(value);
                    }
                }
            }
        } else if (strcmp(section, "shapes") == 0) {
            /* List item: - shape_name */
            if (*p == '-') {
                p = trim(p + 1);
                if (spec->num_shapes < MAX_SHAPES) {
                    spec->shapes[spec->num_shapes++] = shape_from_name(p);
                }
            }
        }
    }

    fclose(f);

    int validation_result = softchip_validate(spec, ctx);
    if (validation_result != TRIX_OK) {
        log_error("softchip_parse: Validation failed for %s", filename);
        return validation_result;
    }

    log_info("Successfully parsed .trix file: %s (shapes=%d, signatures=%d, linear=%d)",
             filename, spec->num_shapes, spec->num_signatures, spec->num_linear_layers);

    return TRIX_OK;
}

/* Parse from string content (for fuzzing and testing) */
int softchip_parse_string(const char* content, SoftChipSpec* spec) {
    if (!content || !spec) {
        return TRIX_ERROR_NULL_POINTER;
    }
    
    trix_error_context_t ctx_body;
    trix_error_context_t* ctx = &ctx_body;
    trix_error_init(ctx);
    
    int init_result = softchip_init(spec);
    if (init_result != TRIX_OK) {
        log_error("softchip_parse_string: Failed to initialize spec");
        return init_result;
    }
    
    char line[1024];
    char section[64] = "";
    Signature* current_sig = NULL;
    LinearLayerSpec* current_linear = NULL;
    
    const char* ptr = content;
    while (ptr && *ptr) {
        /* Get next line */
        size_t line_len = strcspn(ptr, "\n");
        if (line_len >= sizeof(line)) {
            line_len = sizeof(line) - 1;
        }
        memcpy(line, ptr, line_len);
        line[line_len] = '\0';
        
        char* p = trim(line);
        
        if (*p == '\0' || *p == '#') {
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        
        if (strncmp(p, "softchip:", 9) == 0) {
            trix_strcpy_safe(section, "softchip", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        if (strncmp(p, "state:", 6) == 0) {
            trix_strcpy_safe(section, "state", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        if (strncmp(p, "shapes:", 7) == 0) {
            trix_strcpy_safe(section, "shapes", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        if (strncmp(p, "signatures:", 11) == 0) {
            trix_strcpy_safe(section, "signatures", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        if (strncmp(p, "inference:", 10) == 0) {
            trix_strcpy_safe(section, "inference", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        if (strncmp(p, "linear:", 7) == 0) {
            trix_strcpy_safe(section, "linear", sizeof(section));
            ptr += line_len + 1;
            if (*ptr == '\n') ptr++;
            continue;
        }
        
        char* colon = strchr(p, ':');
        if (colon) {
            *colon = '\0';
            char* key = trim(p);
            char* value = trim(colon + 1);
            
            if (strcmp(section, "softchip") == 0) {
                if (strcmp(key, "name") == 0) {
                    trix_strcpy_safe(spec->name, value, sizeof(spec->name));
                } else if (strcmp(key, "version") == 0) {
                    trix_strcpy_safe(spec->version, value, sizeof(spec->version));
                } else if (strcmp(key, "description") == 0) {
                    trix_strcpy_safe(spec->description, value, sizeof(spec->description));
                }
            } else if (strcmp(section, "state") == 0) {
                if (strcmp(key, "bits") == 0) {
                    spec->state_bits = atoi(value);
                } else if (strcmp(key, "layout") == 0) {
                    if (strcmp(value, "cube") == 0) {
                        spec->layout = LAYOUT_CUBE;
                    } else {
                        spec->layout = LAYOUT_FLAT;
                    }
                }
            } else if (strcmp(section, "inference") == 0) {
                if (strcmp(key, "mode") == 0) {
                    if (strcmp(value, "first_match") == 0) {
                        spec->mode = MODE_FIRST_MATCH;
                    } else if (strcmp(value, "all_match") == 0) {
                        spec->mode = MODE_ALL_MATCH;
                    }
                } else if (strcmp(key, "default") == 0) {
                    trix_strcpy_safe(spec->default_label, value, sizeof(spec->default_label));
                }
            } else if (strcmp(section, "signatures") == 0) {
                int indent = 0;
                const char* lp = line;
                while (*lp == ' ') { indent++; lp++; }
                
                if (indent <= 2 && strlen(key) > 0 &&
                    strcmp(key, "pattern") != 0 &&
                    strcmp(key, "threshold") != 0 &&
                    strcmp(key, "shape") != 0) {
                    if (spec->num_signatures < MAX_SIGNATURES) {
                        current_sig = &spec->signatures[spec->num_signatures++];
                        memset(current_sig, 0, sizeof(Signature));
                        trix_strcpy_safe(current_sig->name, key, sizeof(current_sig->name));
                        current_sig->threshold = 64;
                        current_sig->shape_index = SHAPE_SIGMOID;
                    }
                } else if (current_sig) {
                    if (strcmp(key, "pattern") == 0) {
                        if (strncmp(value, "base64:", 7) == 0) {
                            signature_from_base64(value, current_sig->pattern);
                        } else {
                            signature_from_hex(value, current_sig->pattern);
                        }
                    } else if (strcmp(key, "threshold") == 0) {
                        current_sig->threshold = atoi(value);
                    } else if (strcmp(key, "shape") == 0) {
                        current_sig->shape_index = shape_from_name(value);
                    }
                }
            } else if (strcmp(section, "linear") == 0) {
                int indent = 0;
                const char* lp = line;
                while (*lp == ' ') { indent++; lp++; }
                
                if (indent <= 2 && strlen(key) > 0 &&
                    strcmp(key, "input_dim") != 0 &&
                    strcmp(key, "output_dim") != 0 &&
                    strcmp(key, "weights") != 0 &&
                    strcmp(key, "bias") != 0 &&
                    strcmp(key, "activation") != 0) {
                    if (spec->num_linear_layers < MAX_LINEAR_LAYERS) {
                        current_linear = &spec->linear_layers[spec->num_linear_layers++];
                        memset(current_linear, 0, sizeof(LinearLayerSpec));
                        trix_strcpy_safe(current_linear->name, key, sizeof(current_linear->name));
                        current_linear->activation = LINEAR_ACT_NONE;
                    }
                } else if (current_linear) {
                    if (strcmp(key, "input_dim") == 0) {
                        current_linear->input_dim = atoi(value);
                    } else if (strcmp(key, "output_dim") == 0) {
                        current_linear->output_dim = atoi(value);
                    } else if (strcmp(key, "weights") == 0) {
                        trix_strcpy_safe(current_linear->weights_file, value, sizeof(current_linear->weights_file));
                    } else if (strcmp(key, "bias") == 0) {
                        trix_strcpy_safe(current_linear->bias_file, value, sizeof(current_linear->bias_file));
                    } else if (strcmp(key, "activation") == 0) {
                        current_linear->activation = linear_act_from_name(value);
                    }
                }
            }
        } else if (strcmp(section, "shapes") == 0) {
            if (*p == '-') {
                p = trim(p + 1);
                if (spec->num_shapes < MAX_SHAPES) {
                    spec->shapes[spec->num_shapes++] = shape_from_name(p);
                }
            }
        }
        
        ptr += line_len + 1;
        if (*ptr == '\n') ptr++;
    }
    
    return TRIX_OK;
}

void softchip_print(const SoftChipSpec* spec) {
    printf("Soft Chip: %s v%s\n", spec->name, spec->version);
    if (spec->description[0]) {
        printf("Description: %s\n", spec->description);
    }
    printf("State: %d bits (%s)\n", spec->state_bits,
           spec->layout == LAYOUT_CUBE ? "cube" : "flat");

    printf("Shapes (%d):", spec->num_shapes);
    for (int i = 0; i < spec->num_shapes; i++) {
        printf(" %s", shape_name(spec->shapes[i]));
    }
    printf("\n");

    printf("Signatures (%d):\n", spec->num_signatures);
    for (int i = 0; i < spec->num_signatures; i++) {
        const Signature* sig = &spec->signatures[i];
        printf("  %s: threshold=%d, shape=%s\n",
               sig->name, sig->threshold, shape_name((ShapeType)sig->shape_index));
    }

    if (spec->num_linear_layers > 0) {
        printf("Linear Layers (%d):\n", spec->num_linear_layers);
        for (int i = 0; i < spec->num_linear_layers; i++) {
            const LinearLayerSpec* layer = &spec->linear_layers[i];
            const char* act_names[] = {"none", "relu", "sigmoid", "tanh", "gelu", "softmax"};
            printf("  %s: %d -> %d, activation=%s, weights=%s\n",
                   layer->name, layer->input_dim, layer->output_dim,
                   act_names[layer->activation], layer->weights_file);
        }
    }

    printf("Inference: mode=%s, default=%s\n",
           spec->mode == MODE_FIRST_MATCH ? "first_match" : "all_match",
           spec->default_label);
}
