# API Reference

Complete API documentation for TriX.

## Table of Contents
- [Runtime Foundation API](#runtime-foundation-api)
- [Toolchain API](#toolchain-api)
- [Error Codes](#error-codes)
- [Logging](#logging)

---

## Runtime Inference API

The runtime (`zor/include/trixc/runtime.h`) loads `.trix` chip files and runs inference.

### `trix_load`

Load a chip from a `.trix` spec file.

```c
trix_chip_t *chip = trix_load("chip.trix", &error);
```

If the chip has `linear:` layers, `trix_load` reads the weight files and validates dimension constraints:
- First layer `input_dim` must be ≤ 64 and divisible by 4
- Consecutive layers: `layer[i].output_dim == layer[i+1].input_dim`
- Last layer `output_dim` must equal `state_bits`

### `trix_infer`

Run inference on a 64-byte input.

```c
trix_result_t result = trix_infer(chip, input);
```

**If the chip has linear layers**, `trix_infer` automatically:
1. Applies each linear layer (int8 MatVec via SDOT/SMMLA)
2. Clamps activations to `[-127, 127]` between layers
3. Sign-binarizes the final output (`z > 0 → 1`) into a 512-bit code
4. Computes Hamming distance against each signature

**If the chip has no linear layers**, the raw 64-byte input is used directly for Hamming matching.

**Result fields:**
| Field | Type | Description |
|-------|------|-------------|
| `match` | int | Signature index (-1 = no match) |
| `distance` | int | Hamming distance to best match (512 = no match sentinel) |
| `threshold` | int | Threshold of matched signature |
| `label` | char* | Label string (NULL if no match) |
| `trace_tick_start` | uint32_t | HSOS tick when inference began (0 if not using HSOS) |
| `trace_tick_end` | uint32_t | HSOS tick when result was selected |

---

## Runtime Foundation API

The runtime foundation (`zor/include/trixc/`) provides error handling, logging, memory safety, and validation.

```c
#include <trixc/errors.h>
#include <trixc/logging.h>
#include <trixc/memory.h>
#include <trixc/validation.h>
```

### Error Handling

#### `trix_error_init`

Initialize an error context.

```c
trix_error_context_t ctx;
trix_error_init(&ctx);
```

#### `trix_error_set`

Set an error with context.

```c
trix_error_set(&ctx, TRIX_ERROR_FILE_NOT_FOUND, "Cannot open %s", filename);
```

#### `trix_error_description`

Get human-readable error message.

```c
const char* msg = trix_error_description(ctx.code);
printf("Error: %s\n", msg);
```

### Safe Memory Operations

#### `trix_malloc`

Safe allocation with zero-initialization.

```c
int* arr = trix_malloc(1024 * sizeof(int), &ctx);
if (!arr) {
    // Handle error
}
```

#### `trix_strcpy_safe`

Prevent buffer overflows.

```c
char dest[64];
trix_strcpy_safe(dest, source, sizeof(dest));
```

#### `trix_snprintf`

Safe formatted output.

```c
char buffer[256];
trix_snprintf(buffer, sizeof(buffer), "Value: %d", value);
```

### Validation

#### `trix_validate_string`

Validate string input.

```c
trix_validation_result_t result = trix_validate_string(
    input, 
    TRIX_VALIDATE_NON_EMPTY | TRIX_VALIDATE_PRINTABLE
);

if (result.is_valid) {
    // Safe to use
}
```

#### `trix_validate_int`

Validate integer range.

```c
result = trix_validate_int(value, 0, 512);
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

**Example:**
```c
SoftChipSpec spec;
int result = softchip_parse("gesture.trix", &spec);
if (result != TRIX_OK) {
    fprintf(stderr, "Parse failed: %d\n", result);
    return 1;
}
printf("Loaded chip: %s with %d signatures\n", 
       spec.name, spec.num_signatures);
```

#### `softchip_init`

Initialize a spec structure.

```c
int softchip_init(SoftChipSpec* spec);
```

#### `softchip_parse_string`

Parse spec from string content.

```c
int softchip_parse_string(const char* content, SoftChipSpec* spec);
```

### Code Generation

```c
#include <codegen.h>
```

#### `codegen_options_init`

Initialize code generation options.

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
int result = codegen_generate(&spec, &opts);
if (result == TRIX_OK) {
    printf("Generated: output/%s.c\n", spec.name);
}
```

**Targets:**
- `TARGET_C` - Portable C
- `TARGET_NEON` - ARM NEON
- `TARGET_AVX2` - Intel AVX2
- `TARGET_AVX512` - Intel AVX-512
- `TARGET_WASM` - WebAssembly
- `TARGET_VERILOG` - FPGA synthesis

### Linear Forge

```c
#include <linear_forge.h>
```

#### `forge_kernel_to_string`

Generate linear layer kernel as string.

```c
char kernel[65536];
size_t len = forge_kernel_to_string(&spec, kernel, sizeof(kernel));
if (len > 0) {
    printf("Generated %zu bytes of kernel code\n", len);
}
```

### CfC Forge

```c
#include <cfc_forge.h>
```

#### `forge_cfc_to_string`

Generate CfC cell kernel as string.

```c
char cell[131072];
size_t len = forge_cfc_to_string(&cfccell, cell, sizeof(cell));
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
```

### Validation (300-399)
```c
TRIX_ERROR_INVALID_SPEC = 300
TRIX_ERROR_INVALID_DIMENSIONS = 318
TRIX_ERROR_INVALID_THRESHOLD = 313
```

### Memory (500-599)
```c
TRIX_ERROR_OUT_OF_MEMORY = 500
TRIX_ERROR_ALLOCATION_FAILED = 501
TRIX_ERROR_BUFFER_TOO_SMALL = 502
```

---

## Logging

TriX includes a comprehensive logging system.

### Initialization

```c
#include <trixc/logging.h>

trix_log_init();
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
```

---

## Data Structures

### SoftChipSpec

```c
typedef struct {
    char name[MAX_NAME_LEN];      // Chip name
    char version[16];             // Version string
    char description[256];        // Description
    
    int state_bits;               // 128, 256, 512, 1024
    StateLayout layout;           // LAYOUT_CUBE or LAYOUT_FLAT
    
    ShapeType shapes[MAX_SHAPES]; // Available shapes
    int num_shapes;
    
    Signature signatures[MAX_SIGNATURES];
    int num_signatures;
    
    LinearLayerSpec linear_layers[MAX_LINEAR_LAYERS];
    int num_linear_layers;
    
    InferenceMode mode;           // MODE_FIRST_MATCH or MODE_ALL_MATCH
    char default_label[MAX_NAME_LEN];
} SoftChipSpec;
```

### Signature

```c
typedef struct {
    char name[MAX_NAME_LEN];
    uint8_t pattern[STATE_BYTES];  // 64 bytes for 512-bit
    int threshold;                  // Hamming distance threshold
    int shape_index;                // Shape type index
} Signature;
```

### CodegenOptions

```c
typedef struct {
    CodegenTarget target;    // Target platform
    bool generate_test;      // Generate test file
    bool generate_bench;     // Generate benchmark
    bool optimize;           // Enable optimizations
    char output_dir[256];    // Output directory
} CodegenOptions;
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