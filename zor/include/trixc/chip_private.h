/*
 * chip_private.h — Internal trix_chip struct definition
 *
 * Shared between runtime.c and linear_runtime.c.
 * NOT part of the public API.
 */

#ifndef TRIXC_CHIP_PRIVATE_H
#define TRIXC_CHIP_PRIVATE_H

#include <stdint.h>

#define CHIP_MAX_SIGNATURES   128
#define CHIP_STATE_BYTES       64
#define CHIP_MAX_LINEAR_LAYERS 16

struct trix_chip {
    /* Chip metadata */
    char name[64];
    char version[16];
    int state_bits;

    /* Signatures */
    uint8_t signatures[CHIP_MAX_SIGNATURES][CHIP_STATE_BYTES];
    int thresholds[CHIP_MAX_SIGNATURES];
    int shapes[CHIP_MAX_SIGNATURES];
    char labels[CHIP_MAX_SIGNATURES][64];
    int num_signatures;

    /* Shapes */
    int num_shapes;

    /* Linear layers */
    int num_linear_layers;
    int layer_input_dim[CHIP_MAX_LINEAR_LAYERS];
    int layer_output_dim[CHIP_MAX_LINEAR_LAYERS];
    int8_t *layer_weights[CHIP_MAX_LINEAR_LAYERS];      /* raw [N×K] row-major */
    int8_t *layer_weights_packed[CHIP_MAX_LINEAR_LAYERS]; /* I8MM [N/2, K/8, 16] */

    /* Mode */
    int mode;
    char default_label[64];
};

#endif /* TRIXC_CHIP_PRIVATE_H */
