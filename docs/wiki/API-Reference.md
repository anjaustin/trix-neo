# API Reference

Complete API documentation for TriX.

## Table of Contents
- [Runtime API](#runtime-api)
- [Toolchain API](#toolchain-api)
- [Error Codes](#error-codes)
- [Logging](#logging)

---

## Runtime API

The runtime API (`zor/include/trixc/`) provides the core inference functionality.

### Initialization

```c
#include <trixc/runtime.h>
```

#### `trix_init`

Initialize a TriX chip instance.

```c
trix_chip_t* trix_init(void);
```

**Returns:** Pointer to initialized chip, or NULL on failure.

**Example:**
```c
trix_chip_t* chip = trix_init();
if (!chip) {
    fprintf(stderr, "Failed to initialize\n");
    return 1;
}
```

### Inference

#### `trix_infer`

Run inference on input data.

```c
trix_result_t trix_infer(trix_chip_t* chip, const uint8_t input[64]);
```

**Parameters:**
- `chip` - Chip instance from `trix_init()`
- `input` - 64-byte input pattern (512 bits)

**Returns:** `trix_result_t` with match information.

**Result structure:**
```c
typedef struct {
    int match;              // Signature index (-1 = no match)
    int distance;           // Hamming distance to match
    int threshold;          // Threshold that was used
    const char* label;      // Human-readable label
} trix_result_t;
```

**Example:**
```c
uint8_t input[64] = {0x00, 0x01, 0x02, /* ... */};
trix_result_t result = trix_infer(chip, input);

if (result.match >= 0) {
    printf("Recognized: %s (distance: %d)\n", 
           result.label, result.distance);
} else {
    printf("No match (closest: %d)\n", result.distance);
}
```

### State Management

#### `trix_reset`

Reset chip state to initial values.

```c
void trix_reset(trix_chip_t* chip);
```

#### `trix_get_state`

Get current chip state.

```c
const uint8_t* trix_get_state(const trix_chip_t* chip);
```

#### `trix_free`

Free chip resources.

```c
void trix_free(trix_chip_t* chip);
```

---

## Toolchain API

The toolchain API (`tools/include/`) provides parsing and code generation.

### Parser

```c
#include <softchip.h>
```

#### `softchip_parse`

Parse a `.trix` specification file.

```c
int softchip_parse(const char* filename, SoftChipSpec* spec);
```

**Parameters:**
- `filename` - Path to .trix file
- `spec` - Output specification structure

**Returns:** `TRIX_OK` on success, error code on failure.

#### `softchip_init`

Initialize a spec structure.

```c
int softchip_init(SoftChipSpec* spec);
```

### Code Generation

```c
#include <codegen.h>
```

#### `codegen_options_init`

Initialize code generation options.

```c
int codegen_options_init(CodegenOptions* opts);
```

**Example:**
```c
CodegenOptions opts;
codegen_options_init(&opts);
opts.target = TARGET_NEON;
opts.generate_test = true;
strcpy(opts.output_dir, "output");
```

#### `codegen_generate`

Generate code from specification.

```c
int codegen_generate(const SoftChipSpec* spec, const CodegenOptions* opts);
```

**Returns:** `TRIX_OK` on success, error code on failure.

### Linear Forge

```c
#include <linear_forge.h>
```

#### `forge_kernel_to_string`

Generate linear layer kernel as string.

```c
size_t forge_kernel_to_string(const AggregateShapeSpec* spec, 
                               char* buffer, size_t buffer_size);
```

### CfC Forge

```c
#include <cfc_forge.h>
```

#### `forge_cfc_to_string`

Generate CfC cell kernel as string.

```c
size_t forge_cfc_to_string(const CfCCellSpec* spec, char* buffer, size_t size);
```

---

## Error Codes

All TriX functions return `trix_error_t` error codes.

### Success
```c
TRIX_OK = 0  // Success
```

### General Errors (1-99)
```c
TRIX_ERROR_UNKNOWN = 1
TRIX_ERROR_NOT_IMPLEMENTED = 2
TRIX_ERROR_INVALID_ARGUMENT = 3
TRIX_ERROR_NULL_POINTER = 4
TRIX_ERROR_INTERNAL = 5
```

### File I/O (100-199)
```c
TRIX_ERROR_FILE_NOT_FOUND = 100
TRIX_ERROR_FILE_READ = 101
TRIX_ERROR_FILE_WRITE = 102
TRIX_ERROR_FILE_PERMISSION = 103
TRIX_ERROR_INVALID_PATH = 105
```

### Validation (300-399)
```c
TRIX_ERROR_INVALID_SPEC = 300
TRIX_ERROR_INVALID_NAME = 301
TRIX_ERROR_INVALID_STATE_BITS = 304
TRIX_ERROR_INVALID_SHAPE = 305
TRIX_ERROR_TOO_MANY_SHAPES = 307
TRIX_ERROR_INVALID_THRESHOLD = 313
TRIX_ERROR_INVALID_DIMENSIONS = 318
```

### Memory (500-599)
```c
TRIX_ERROR_OUT_OF_MEMORY = 500
TRIX_ERROR_ALLOCATION_FAILED = 501
TRIX_ERROR_BUFFER_TOO_SMALL = 502
```

### Getting Error Descriptions

```c
const char* trix_error_name(trix_error_t error);
const char* trix_error_description(trix_error_t error);
```

---

## Logging

TriX includes a comprehensive logging system.

### Initialization

```c
#include <trixc/logging.h>

trix_log_init();  // Initialize with defaults
```

### Log Levels

```c
typedef enum {
    TRIX_LOG_ERROR = 0,
    TRIX_LOG_WARN  = 1,
    TRIX_LOG_INFO  = 2,
    TRIX_LOG_DEBUG = 3,
    TRIX_LOG_TRACE = 4
} trix_log_level_t;
```

### Logging Functions

```c
log_error("Failed to open file: %s", filename);
log_warn("Non-standard configuration: %d", value);
log_info("Processing %d items", count);
log_debug("Variable x = %d", x);
log_trace("Entering function");
```

### Exporting Metrics

```c
char buffer[8192];
trix_metrics_export_prometheus(buffer, sizeof(buffer));
// Output for Prometheus scraping
```

---

## Data Structures

### SoftChipSpec

```c
typedef struct {
    char name[MAX_NAME_LEN];
    char version[16];
    char description[MAX_DESCRIPTION];
    
    int state_bits;
    StateLayout layout;
    
    ShapeType shapes[MAX_SHAPES];
    int num_shapes;
    
    Signature signatures[MAX_SIGNATURES];
    int num_signatures;
    
    LinearLayerSpec linear_layers[MAX_LINEAR_LAYERS];
    int num_linear_layers;
    
    InferenceMode mode;
    char default_label[MAX_NAME_LEN];
} SoftChipSpec;
```

### Signature

```c
typedef struct {
    char name[MAX_NAME_LEN];
    uint8_t pattern[STATE_BYTES];  // 64 bytes
    int threshold;
    int shape_index;
} Signature;
```

### AggregateShapeSpec

```c
typedef struct {
    char name[64];
    int K;  // Input dimension
    int N;  // Output dimension
    
    const void* weights;
    size_t weights_size;
    Quantization quant;
    const float* bias;
    Activation activation;
    ForgeStrategy strategy;
} AggregateShapeSpec;
```

---

## Configuration Constants

| Constant | Value | Description |
|----------|-------|-------------|
| `MAX_NAME_LEN` | 64 | Maximum name length |
| `MAX_SHAPES` | 32 | Maximum shapes per chip |
| `MAX_SIGNATURES` | 128 | Maximum signatures |
| `MAX_LINEAR_LAYERS` | 16 | Maximum linear layers |
| `STATE_BYTES` | 64 | State size (512 bits) |
| `STATE_BITS` | 512 | State bits |