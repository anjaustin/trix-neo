/*
 * runtime.h — TriX Runtime Engine
 *
 * Load and run soft chips with a clean, simple API.
 *
 * Copyright 2026 TriX Research
 */

#ifndef TRIXC_RUNTIME_H
#define TRIXC_RUNTIME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Opaque chip handle
 */
typedef struct trix_chip trix_chip_t;

/*
 * Inference result
 */
typedef struct {
    int match;              /* Signature index (-1 = no match) */
    int distance;           /* Hamming distance to best match */
    int threshold;          /* Threshold used */
    const char* label;      /* Human-readable label (NULL if no match) */
} trix_result_t;

/*
 * Chip configuration (loaded from .trix spec)
 */
typedef struct {
    const char* name;
    const char* version;
    int state_bits;
    int num_signatures;
    int num_shapes;
    int num_linear_layers;
} trix_chip_info_t;

/*
 * ═══════════════════════════════════════════════════════════════════════════
 * LIFECYCLE
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * Load a chip from a .trix specification file.
 *
 * @param filename Path to .trix file
 * @param error    Optional error code output
 * @return Chip handle or NULL on error
 */
trix_chip_t* trix_load(const char* filename, int* error);

/**
 * Load a chip from a compiled binary (future: .trixc files).
 *
 * @param data     Compiled chip data
 * @param len      Data length
 * @param error    Optional error code output
 * @return Chip handle or NULL on error
 */
trix_chip_t* trix_load_binary(const uint8_t* data, size_t len, int* error);

/**
 * Get chip information.
 *
 * @param chip     Chip handle
 * @return Chip info (do not free)
 */
const trix_chip_info_t* trix_info(const trix_chip_t* chip);

/**
 * Get the memory footprint of a chip.
 *
 * @param chip     Chip handle
 * @return Bytes needed for chip state
 */
size_t trix_memory_footprint(const trix_chip_t* chip);

/**
 * Free a chip and all associated resources.
 *
 * @param chip     Chip handle (can be NULL)
 */
void trix_chip_free(trix_chip_t* chip);

/*
 * ═══════════════════════════════════════════════════════════════════════════
 * INFERENCE
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * Run inference on input data.
 *
 * @param chip     Chip handle
 * @param input    64-byte input pattern (512 bits)
 * @return Inference result
 */
trix_result_t trix_infer(const trix_chip_t* chip, const uint8_t input[64]);

/**
 * Run inference in "all match" mode - returns all signatures
 * that match within their thresholds.
 *
 * @param chip         Chip handle
 * @param input        64-byte input pattern
 * @param matches      Output array for matches
 * @param max_matches  Size of matches array
 * @return Number of matches found
 */
int trix_infer_all(const trix_chip_t* chip, const uint8_t input[64],
                   trix_result_t* matches, int max_matches);

/**
 * Get the label for a signature index.
 *
 * @param chip     Chip handle
 * @param index    Signature index (0 to num_signatures-1)
 * @return Label string (do not free)
 */
const char* trix_label(const trix_chip_t* chip, int index);

/**
 * Get threshold for a signature index.
 *
 * @param chip     Chip handle
 * @param index    Signature index
 * @return Threshold value
 */
int trix_threshold(const trix_chip_t* chip, int index);

/*
 * ═══════════════════════════════════════════════════════════════════════════
 * STATE MANAGEMENT
 * ═══════════════════════════════════════════════════════════════════════════
 */

/**
 * Get pointer to chip's signature patterns.
 * Useful for reading signatures, not modifying.
 *
 * @param chip     Chip handle
 * @param index    Signature index
 * @return 64-byte pattern (do not free)
 */
const uint8_t* trix_signature(const trix_chip_t* chip, int index);

/**
 * Get the shape type for a signature.
 *
 * @param chip     Chip handle
 * @param index    Signature index
 * @return Shape type (0=xor, 1=and, etc.)
 */
int trix_shape(const trix_chip_t* chip, int index);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_RUNTIME_H */