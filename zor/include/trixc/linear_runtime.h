/*
 * linear_runtime.h — Linear layer forward pass for TriX
 *
 * Bridges linear MatVec layers to the Hamming inference engine.
 * Internal functions exposed for testing only.
 */

#ifndef TRIXC_LINEAR_RUNTIME_H
#define TRIXC_LINEAR_RUNTIME_H

#include <stdint.h>
#include "runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Run all linear layers on input, produce 64-byte binary output.
 *
 * Requires chip->num_linear_layers > 0 and weights loaded.
 * Returns TRIX_OK on success, error code otherwise.
 *
 * @param chip   Chip handle (must have linear layers loaded)
 * @param input  Raw input bytes; first layer uses layer_input_dim[0] bytes
 * @param out    64-byte sign-bit-quantized output (512 bits)
 */
int trix_exec_linear(const trix_chip_t *chip, const uint8_t *input, uint8_t out[64]);

/* Exposed for unit testing */
void linear_matvec_portable(int K, int N, const int8_t *x, const int8_t *W, int32_t *y);
void linear_sign_binarize(const int32_t *z, int N, uint8_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TRIXC_LINEAR_RUNTIME_H */
