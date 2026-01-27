# Linear Kingdom Forge - Test Suite

Test suite for the Linear Kingdom circuit stamper and CfC soft-chip forge.

## Quick Start

```bash
cd trix/tools

# Run all tests
make test

# Run specific tests
make test-i8mm     # I8MM (SMMLA) batch inference
make bench         # Performance benchmarks
make demo          # End-to-end CfC demo
```

## Test Files

| Test | Target | Description |
|------|--------|-------------|
| `test_linear_forge.c` | Correctness | Verifies forge produces valid C/NEON code |
| `test_forged_neon_compile.c` | Compilation | Ensures forged kernels compile with clang |
| `test_cfc_forge.c` | CfC Pipeline | Tests complete CfC soft-chip generation |
| `test_i8mm_forge.c` | I8MM Backend | Tests ARMv8.6 SMMLA batch inference |
| `bench_forged_vs_handwritten.c` | Performance | Benchmarks forged vs hand-written kernels |
| `e2e_cfc_demo.c` | Integration | Full pipeline: weights -> forge -> compile -> run |

## Test Details

### test_linear_forge.c

Verifies that `forge_kernel_to_string()` produces valid code for all backends:

- **C Portable** - Reference implementation (any platform)
- **NEON Block-16** - 16 output channels per iteration
- **NEON Block-8-K64** - 8 outputs x 64 K-elements (optimal for M4)

Tests weight packing, ternary quantization, and output format.

### test_forged_neon_compile.c

Actually compiles forged kernels with clang and runs them:

```
Step 1: Forge NEON kernel
Step 2: Compile with clang -mcpu=apple-m4
Step 3: Run compiled binary
Step 4: Verify output correctness
```

Both Block-16 and Block-8-K64 strategies are tested.

### test_cfc_forge.c

Tests the complete CfC soft-chip forge pipeline:

- Creates CfC spec (input/hidden/output dimensions)
- Forges all 5 CfC weight matrices (W_gate, W_cand, W_out, etc.)
- Generates timestep integration code
- Compiles and runs the forged CfC

### test_i8mm_forge.c

Tests the I8MM (SMMLA) backend for ARMv8.6-a chips:

- Generates I8MM kernel with 2x8x2 matrix blocks
- Packs weights for optimal SMMLA access
- Verifies correctness against reference implementation
- Benchmarks batch inference (batch=4)

Requires: `-march=armv8.6-a+i8mm` compiler flag.

### bench_forged_vs_handwritten.c

Performance benchmark comparing forged kernels to hand-written NEON:

```
Benchmark: 4096 x 4096 matvec, 100 iterations

Target (M4 base):      150.0 GOP/s
Block-16 kernel:       ~189 GOP/s
Block-8-K64 kernel:    ~178 GOP/s
I8MM batch=4:          ~235 GOP/s
```

The benchmark verifies forged kernels match hand-written performance.

### e2e_cfc_demo.c

End-to-end demonstration of the complete forge pipeline:

1. **Generate** synthetic CfC weights (simulating PyTorch export)
2. **Forge** the CfC as a NEON soft-chip
3. **Compile** the forged header with clang
4. **Run** inference on test input
5. **Verify** correctness against reference implementation

This is the canonical example of "from weights to wires in one command."

## Performance Targets

| Strategy | Target | Achieved | Status |
|----------|--------|----------|--------|
| Block-16 | 150 GOP/s | 189 GOP/s | PASS |
| Block-8-K64 | 150 GOP/s | 178 GOP/s | PASS |
| I8MM batch=4 | 150 GOP/s | 235 GOP/s | PASS |

All benchmarks on Apple M4 (base chip).

## Adding New Tests

1. Create `test_<name>.c` in this directory
2. Add compile target to `Makefile`
3. Add to the test runner in `make test`
4. Document in this README

## Related Documentation

- [tools/README.md](../README.md) - Main tools documentation
- [include/linear_forge.h](../include/linear_forge.h) - Forge API
- [include/cfc_forge.h](../include/cfc_forge.h) - CfC forge API
