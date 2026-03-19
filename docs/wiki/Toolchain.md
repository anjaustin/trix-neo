# TriX Toolchain

The TriX toolchain transforms neural networks into optimized soft chips.

## Overview

```
.trix Spec File
      │
      ▼
┌─────────────┐
│   Parser    │  ← softchip.c
└─────────────┘
      │
      ▼
┌─────────────┐
│  Codegen    │  ← codegen.c
└─────────────┘
      │
      ▼
   C/NEON Code
```

## The `trix` Command

The main CLI tool for code generation.

### Usage

```bash
trix <specfile.trix> [options]
```

### Options

| Option | Description | Default |
|--------|-------------|---------|
| `--target=TARGET` | Code generation target | c |
| `--output=DIR` | Output directory | output |
| `--no-test` | Skip test file generation | false |

### Targets

- `c` - Portable C (works everywhere)
- `neon` - ARM NEON with SDOT
- `avx2` - Intel AVX2
- `avx512` - Intel AVX-512
- `wasm` - WebAssembly
- `verilog` - FPGA synthesis

### Example

```bash
# Generate C code
trix gesture.trix

# Generate NEON-optimized code
trix gesture.trix --target=neon

# Custom output directory
trix gesture.trix --target=neon --output=build/
```

## Spec File Format

### Basic Structure

```yaml
softchip:
  name: my_chip
  version: 1.0.0

state:
  bits: 512
  layout: cube

shapes:
  - xor
  - and
  - or
  - sigmoid

signatures:
  pattern_a:
    pattern: 0001020304050607...
    threshold: 32
    shape: xor

inference:
  mode: first_match
  default: unknown
```

### Sections

#### `softchip:` - Metadata

| Field | Type | Description |
|-------|------|-------------|
| `name` | string | Chip name |
| `version` | string | Version string |
| `description` | string | Optional description |

#### `state:` - State Configuration

| Field | Type | Description |
|-------|------|-------------|
| `bits` | int | State bits (128, 256, 512, 1024) |
| `layout` | string | `cube` or `flat` |

#### `shapes:` - Available Shapes

List of shapes to use:
- `xor` - XOR pattern matching
- `and` - AND conjunction
- `or` - OR disjunction  
- `not` - NOT negation
- `sigmoid` - Sigmoid activation

#### `signatures:` - Pattern Definitions

```yaml
signature_name:
  pattern: <hex string or base64:...>
  threshold: <int>  # Hamming distance threshold
  shape: <shape name>
```

Pattern format:
- Hex: `001122334455...`
- Base64: `base64:AAASSS...`

#### `inference:` - Inference Options

| Field | Type | Description |
|-------|------|-------------|
| `mode` | string | `first_match` or `all_match` |
| `default` | string | Default label when no match |

#### `linear:` - Linear Layers (Optional)

```yaml
linear:
  layer_name:
    input_dim: 64
    output_dim: 32
    weights: path/to/weights.bin
    bias: path/to/bias.bin
    activation: relu
```

## Generated Files

### Header (`.h`)

Contains:
- State size constants
- Signature label definitions
- API declarations
- Handle typedef

### Source (`.c`)

Contains:
- Signature pattern data
- Popcount implementation
- Inference function
- State management

### Test (`_test.c`)

Auto-generated test harness that verifies all signatures match their patterns.

### Makefile

Build system for the generated code.

## Advanced: Custom Code Generation

### Direct API Usage

```c
#include <softchip.h>
#include <codegen.h>

// Parse spec
SoftChipSpec spec;
softchip_parse("chip.trix", &spec);

// Configure options
CodegenOptions opts;
codegen_options_init(&opts);
opts.target = TARGET_NEON;
opts.generate_test = true;

// Generate code
codegen_generate(&spec, &opts);
```

### Linear Forge

Generate optimized matrix-vector multiplication:

```c
#include <linear_forge.h>

AggregateShapeSpec spec = {
    .name = "dense1",
    .K = 128,
    .N = 64,
    .strategy = FORGE_STRATEGY_NEON_BLOCK8_K64,
    .activation = ACT_RELU,
};

char kernel[65536];
forge_kernel_to_string(&spec, kernel, sizeof(kernel));
```

### CfC Forge

Generate Complete Fixed-time Cell:

```c
#include <cfc_forge.h>

CfCCellSpec spec = {
    .name = "cfc1",
    .input_dim = 64,
    .hidden_dim = 128,
    .has_output = false,
};

char cell[131072];
forge_cfc_to_string(&spec, cell, sizeof(cell));
```

## Performance Tuning

### Target Selection

| Target | Use Case |
|--------|----------|
| `c` | Portability, debugging |
| `neon` | ARM devices (Apple Silicon, embedded) |
| `avx2` | Intel/AMD x86 |
| `wasm` | Browser, edge devices |

### Threshold Tuning

Lower threshold = stricter matching = fewer false positives
Higher threshold = looser matching = more matches

```
threshold = 32   # 512-bit state → ~6% bit error tolerance
threshold = 64   # ~12% tolerance
threshold = 128  # ~25% tolerance (very loose)
```

### Mode Selection

- `first_match` - Return first matching signature (faster)
- `all_match` - Check all signatures, return closest (thorough)