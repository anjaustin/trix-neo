/*
 * softchip.h — Soft Chip data structures and parsing
 *
 * Part of TriX toolchain
 * Pure C, no dependencies
 */

#ifndef SOFTCHIP_H
#define SOFTCHIP_H

#include <stdint.h>
#include <stdbool.h>

/* Maximum limits */
#define MAX_NAME_LEN      64
#define MAX_SHAPES        32
#define MAX_SIGNATURES    128
#define MAX_LINEAR_LAYERS 16
#define MAX_DESCRIPTION   256
#define STATE_BYTES       64   /* 512 bits */

/* Signature structure */
typedef struct {
    char name[MAX_NAME_LEN];
    uint8_t pattern[STATE_BYTES];   /* 512-bit pattern */
    int threshold;                   /* Hamming distance threshold */
    int shape_index;                 /* Which shape to use */
} Signature;

/* Shape types (from the 5 Primes) */
typedef enum {
    SHAPE_XOR = 0,
    SHAPE_AND,
    SHAPE_OR,
    SHAPE_NOT,
    SHAPE_NAND,
    SHAPE_NOR,
    SHAPE_XNOR,
    SHAPE_RELU,
    SHAPE_SIGMOID,
    SHAPE_IDENTITY,
    SHAPE_COUNT
} ShapeType;

/* State layout */
typedef enum {
    LAYOUT_FLAT = 0,
    LAYOUT_CUBE
} StateLayout;

/* Inference mode */
typedef enum {
    MODE_FIRST_MATCH = 0,
    MODE_ALL_MATCH,
    MODE_WEIGHTED
} InferenceMode;

/* Linear layer activation type */
typedef enum {
    LINEAR_ACT_NONE = 0,
    LINEAR_ACT_RELU,
    LINEAR_ACT_SIGMOID,
    LINEAR_ACT_TANH,
    LINEAR_ACT_GELU,
    LINEAR_ACT_SOFTMAX
} LinearActivation;

/* Linear layer specification (Linear Kingdom) */
typedef struct {
    char name[MAX_NAME_LEN];     /* Layer name (e.g., "gate", "candidate") */
    int input_dim;               /* K dimension */
    int output_dim;              /* N dimension */
    char weights_file[256];      /* Path to weights binary file */
    char bias_file[256];         /* Path to bias file (optional) */
    LinearActivation activation; /* Fused activation */
} LinearLayerSpec;

/* Soft chip specification */
typedef struct {
    /* Metadata */
    char name[MAX_NAME_LEN];
    char version[16];
    char description[MAX_DESCRIPTION];

    /* State configuration */
    int state_bits;
    StateLayout layout;

    /* Shapes */
    ShapeType shapes[MAX_SHAPES];
    int num_shapes;

    /* Signatures */
    Signature signatures[MAX_SIGNATURES];
    int num_signatures;

    /* Linear Kingdom layers (MatVec with weights) */
    LinearLayerSpec linear_layers[MAX_LINEAR_LAYERS];
    int num_linear_layers;

    /* Inference */
    InferenceMode mode;
    char default_label[MAX_NAME_LEN];
} SoftChipSpec;

/* Parsing functions */
int softchip_parse(const char* filename, SoftChipSpec* spec);
int softchip_parse_string(const char* content, SoftChipSpec* spec);

/* Utility functions */
const char* shape_name(ShapeType type);
ShapeType shape_from_name(const char* name);
void softchip_init(SoftChipSpec* spec);
void softchip_print(const SoftChipSpec* spec);

/* Signature helpers */
int signature_from_hex(const char* hex, uint8_t* pattern);
int signature_from_base64(const char* b64, uint8_t* pattern);
void signature_to_hex(const uint8_t* pattern, char* hex);

#endif /* SOFTCHIP_H */
