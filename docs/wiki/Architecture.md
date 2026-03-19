# TriX Architecture

This document provides a deep dive into TriX's architectural design, philosophy, and implementation details.

## Core Concept: Soft Chips

TriX transforms neural networks into **soft chips** - cryptographic pattern-matching circuits. Unlike traditional neural networks that use floating-point matrix multiplication, TriX uses:

- **Hamming distance** as the core operation
- **Ternary weights** (-1, 0, +1) for memory efficiency
- **Fixed-point arithmetic** for determinism
- **Constant-time operations** for security

## The Five Primes

TriX is built on five fundamental operations (the "Primes"):

| Prime | Operation | Use Case |
|-------|-----------|----------|
| **XOR** | Pattern match | Similarity detection |
| **AND** | Conjunction | Feature combination |
| **OR** | Disjunction | Multi-pattern matching |
| **NOT** | Negation | Exclusion patterns |
| **XNOR** | Similarity | Anti-pattern matching |

These map directly to SIMD instructions:
- **SDOT** (XOR + POPCOUNT) = Pattern recognition at 16x efficiency
- **AND/OR** = Feature composition
- **Ternary** = 2-bit storage per weight

## System Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                        TriX Toolchain                           │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────────┐    │
│  │   Parser    │───▶│  Codegen    │───▶│  NEON/AVX2/x86  │    │
│  │ (softchip)  │    │             │    │    Compiler     │    │
│  └─────────────┘    └─────────────┘    └─────────────────┘    │
│        │                                      │                │
│        ▼                                      ▼                │
│  ┌─────────────┐                      ┌─────────────────┐    │
│  │   Linear    │                      │   Soft Chip     │    │
│  │   Forge     │                      │    Runtime      │    │
│  └─────────────┘                      └─────────────────┘    │
│        │                                      │                │
│        ▼                                      ▼                │
│  ┌─────────────┐                      ┌─────────────────┐    │
│  │    CfC      │                      │   Inference     │    │
│  │   Forge     │                      │    Engine       │    │
│  └─────────────┘                      └─────────────────┘    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

## Components

### 1. Parser (softchip.c)

Parses `.trix` specification files and validates the configuration.

```trix
softchip:
  name: gesture_recognizer
  version: 1.0.0

state:
  bits: 512
  layout: cube

shapes:
  - xor
  - and

signatures:
  gesture_a:
    pattern: 000102030405...
    threshold: 32
```

**Key responsibilities:**
- Parse YAML-like spec format
- Validate signatures and thresholds
- Extract linear layer specifications

### 2. Code Generator (codegen.c)

Generates optimized C code from SoftChipSpec.

**Supported targets:**
- `c` - Portable C (works everywhere)
- `neon` - ARM NEON with SDOT instructions
- `avx2` - Intel AVX2
- `avx512` - Intel AVX-512
- `wasm` - WebAssembly
- `verilog` - FPGA/ASIC synthesis

**Generated code includes:**
- Popcount implementations (portable or SIMD)
- Signature pattern arrays
- Inference loop with early termination
- State update logic

### 3. Linear Forge (linear_forge.c)

Generates optimized matrix-vector multiplication kernels.

**Strategies:**
- `C_PORTABLE` - Reference implementation
- `NEON_BLOCK_16` - 16 output channels, SDOT
- `NEON_BLOCK8_K64` - 8 output × 64 elements
- `NEON_GHOST_12` - Streaming optimization
- `I8MM_BATCH` - Batch inference with SMMLA

### 4. CfC Forge (cfc_forge.c)

Generates Complete Fixed-time Cell (CfC) implementations.

**CfC architecture:**
```
gate      = σ(Wg @ [x; h] + bg)
candidate = tanh(Wc @ [x; h] + bc)
decay     = exp(-dt / τ)
h_new    = (1 - gate) * h * decay + gate * candidate
```

## Memory Model

### State Representation

```
┌────────────────────────────────────────┐
│           512-bit State                │
│  ┌──────┬──────┬──────┬──────┐        │
│  │ 128b │ 128b │ 128b │ 128b │        │
│  │(cube)│(cube)│(cube)│(cube)│        │
│  └──────┴──────┴──────┴──────┘        │
└────────────────────────────────────────┘
```

### Weight Packing

TriX uses efficient weight layouts:

**Block-16 (NEON):**
```
┌─────────────────────┐
│ W[0,0..15]   (row 0)│  16 weights
│ W[1,0..15]   (row 1)│
│ ...                 │
│ W[15,0..15] (row 15)│  16 rows
└─────────────────────┘
Block size: 16×16 = 256 bytes
```

## Inference Flow

```
Input (512 bits)
      │
      ▼
┌─────────────────┐
│ XOR with each   │  ← SDOT instruction (16x)
│ signature       │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ POPCOUNT        │  ← Hamming distance
│ (bit count)     │
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│ Compare with    │
│ thresholds      │
└────────┬────────┘
         │
         ▼
   Match / No Match
```

## Performance Characteristics

### Latency

- **Deterministic**: Same input → Same cycles every time
- **No branches**: Constant-time popcount always runs full 64 bytes
- **Tunable**: Higher threshold = more signatures to check

### Throughput

| Configuration | GOP/s | Notes |
|--------------|-------|-------|
| M4 + I8MM (batch=4) | 235 | Best case |
| M4 + NEON SDOT | 185 | Typical |
| x86 + AVX2 | 45 | Good portability |
| Portable C | 10 | Works everywhere |

### Memory

- **Signature storage**: 64 bytes × N signatures
- **State**: 64 bytes (fixed)
- **Linear layers**: K × N bytes (ternary)

## Certification Readiness

TriX is designed for regulatory compliance:

### FDA 510(k) Considerations
- **Deterministic**: No floating-point nondeterminism
- **Reproducible**: Same result every time
- **Documented**: Full source and test coverage

### ISO 26262 (Functional Safety)
- **No dynamic memory**: All allocation at init
- **Bounds checking**: All arrays validated
- **Error handling**: Comprehensive error codes

## Design Principles

1. **Make it real** - Ship working code, not prototypes
2. **Determinism first** - Same input → Same output
3. **Safety by default** - Null checks, bounds validation
4. **Performance through SIMD** - SDOT is the Prime
5. **Certification-ready** - Design for FDA/ISO from day one

## File Structure

```
trix/
├── zor/                      # Runtime library
│   ├── include/trixc/       # Public headers
│   │   ├── errors.h         # Error handling
│   │   ├── logging.h        # Logging system
│   │   ├── memory.h         # Safe memory
│   │   ├── validation.h     # Input validation
│   │   ├── thread.h         # Thread safety
│   │   └── metrics.h        # Observability
│   ├── src/                 # Implementation
│   └── test/                # Test suite
│
├── tools/                   # Toolchain
│   ├── include/            # Tool headers
│   │   ├── softchip.h      # Parser API
│   │   ├── codegen.h       # Code generator API
│   │   ├── linear_forge.h  # Linear layer API
│   │   └── cfc_forge.h     # CfC API
│   ├── src/                # Implementation
│   │   ├── softchip.c      # Parser
│   │   ├── codegen.c       # Code generator
│   │   ├── linear_forge.c  # Linear forge
│   │   ├── cfc_forge.c     # CfC forge
│   │   └── trix.c          # CLI
│   └── test/               # Tool tests
│
├── docs/wiki/              # Documentation
└── Dockerfile             # Containerization
```

## Next Steps

- [Toolchain Guide](Toolchain.md) - Using the code generator
- [API Reference](API-Reference.md) - Detailed API documentation
- [Security](Security.md) - Security model and hardening