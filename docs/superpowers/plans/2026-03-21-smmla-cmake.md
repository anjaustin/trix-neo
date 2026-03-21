# SMMLA CMake Enablement Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Enable the SMMLA (`vmmlaq_s32`) code path in `linear_runtime.c` by adding ARMv8.6-a I8MM compiler flags to CMakeLists.txt, and validate with a benchmark showing SMMLA throughput numbers.

**Architecture:** `linear_runtime.c` already has a complete SMMLA backend gated on `#ifdef __ARM_FEATURE_MATMUL_INT8`. This flag is defined by the compiler only when `-march=armv8.6-a+i8mm` (or equivalent) is passed. The CMake change auto-detects Apple Silicon and sets the flag. All existing tests must still pass.

**Tech Stack:** CMake 3.15+, Clang (Apple Silicon), existing `linear_runtime.c` SMMLA path, existing `bench_linear.c`

---

## Background

`bench_linear.c` currently shows:
```
int8-sdot:  234.6 GOP/s   (50x fp32)
int8-smmla: N/A            ← this path is blocked by missing compiler flag
```

The SMMLA kernel in `linear_runtime.c` (`matvec_smmla()`) computes 2×8×2 matrix blocks using `vmmlaq_s32` — 32 operations per instruction vs 16 for SDOT. On hardware that supports I8MM (Apple M1+, ARM Cortex-A78AE, etc.) this should yield measurably higher throughput.

The `layer_weights_packed` field in `chip_private.h` exists precisely for SMMLA's `[N/2, K/8, 16]` weight layout. It is lazily populated on first SMMLA call.

---

## File Map

| File | Action | Purpose |
|---|---|---|
| `CMakeLists.txt` | Modify | Add `TRIX_ENABLE_I8MM` option + auto-detect + compile flag |
| `zor/src/linear_runtime.c` | Verify (read only) | Confirm SMMLA path is complete and gated correctly |
| `tools/test/bench_linear.c` | Verify (read only) | Confirm SMMLA benchmark path exists |

No new source files. No logic changes. This plan is purely build system + validation.

---

## Task 1: Verify SMMLA Code Path is Complete

Before touching CMake, confirm the code that will be enabled is correct.

- [ ] **Step 1: Check linear_runtime.c SMMLA gate**

```bash
grep -n "ARM_FEATURE_MATMUL\|SMMLA\|vmmlaq\|i8mm\|matvec_smmla\|pack_i8mm" \
    zor/src/linear_runtime.c | head -30
```

Expected: shows `#ifdef __ARM_FEATURE_MATMUL_INT8`, `vmmlaq_s32`, `pack_i8mm`, `matvec_smmla` — confirming the full path exists.

- [ ] **Step 2: Check bench_linear.c has an SMMLA benchmark case**

```bash
grep -n "SMMLA\|smmla\|i8mm\|MATMUL" tools/test/bench_linear.c | head -20
```

Expected: shows an SMMLA benchmark loop that currently prints `N/A`.

- [ ] **Step 3: Confirm Apple Silicon detection works in CMake**

```bash
cmake -B /tmp/trix_probe -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_VERBOSE_MAKEFILE=ON . 2>&1 | \
    grep -i "processor\|arm\|apple\|system"
```

Expected: shows `arm64` or `Apple` in system info.

---

## Task 2: Add I8MM CMake Option

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Read the current compiler flags section of CMakeLists.txt**

Read lines 25–50 of `CMakeLists.txt` to see the existing options and compiler flag block.

- [ ] **Step 2: Add the I8MM option after the existing sanitizer options**

In `CMakeLists.txt`, after the existing options block (after `option(TRIX_ENABLE_COVERAGE ...)`), add:

```cmake
option(TRIX_ENABLE_I8MM "Enable ARMv8.6-a I8MM extension (SMMLA instruction)" OFF)

# Auto-enable I8MM on Apple Silicon (M1/M2/M3/M4 all support ARMv8.6-a+i8mm).
# Only set if the user has not explicitly provided a value — FORCE would silently
# discard -DTRIX_ENABLE_I8MM=OFF. Use DEFINED check to respect user overrides.
if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm64")
    if(NOT DEFINED CACHE{TRIX_ENABLE_I8MM})
        set(TRIX_ENABLE_I8MM ON CACHE BOOL
            "Auto-enabled: Apple Silicon supports ARMv8.6-a+i8mm")
    endif()
endif()
```

Note: `add_compile_options` is directory-scoped. Adding `-march=armv8.6-a+i8mm` here applies to ALL targets defined later in `CMakeLists.txt`, including test targets. This is intentional — test binaries need the flag to exercise the SMMLA path. If cross-compiling for x86_64 on an arm64 host (via `CMAKE_OSX_ARCHITECTURES=x86_64`), disable I8MM explicitly with `-DTRIX_ENABLE_I8MM=OFF`.

- [ ] **Step 3: Add the compile flag after the existing sanitizer blocks**

After the UBSan block (after `endif()` for `TRIX_ENABLE_UBSAN`), add:

```cmake
if(TRIX_ENABLE_I8MM)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU" AND
       CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        add_compile_options(-march=armv8.6-a+i8mm)
        message(STATUS "I8MM: enabled (-march=armv8.6-a+i8mm)")
    else()
        message(WARNING "TRIX_ENABLE_I8MM is ON but compiler/arch may not support I8MM")
    endif()
endif()
```

- [ ] **Step 4: Add I8MM to the Configuration Summary at the bottom**

Find the summary `message(STATUS ...)` block at the bottom of CMakeLists.txt. After the `Code Coverage:` line (the last entry in the summary block), add:

```cmake
message(STATUS "  I8MM (SMMLA):         ${TRIX_ENABLE_I8MM}")
```

- [ ] **Step 5: Rebuild from scratch to pick up the new flag**

```bash
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

Verify I8MM is enabled in the output:
```
-- I8MM: enabled (-march=armv8.6-a+i8mm)
```

And in the summary:
```
  I8MM (SMMLA):         ON
```

- [ ] **Step 6: Build**

```bash
cmake --build build -j4
```

Expected: clean build, no errors. If you see:
```
error: unknown target CPU 'armv8.6-a'
```
Then the Clang version is too old (< 12.0). Check with `clang --version`. On macOS, `xcode-select --install` to update.

- [ ] **Step 7: Commit the CMake change**

```bash
git add CMakeLists.txt
git commit -m "build: enable ARMv8.6-a I8MM (SMMLA) auto-detection on Apple Silicon"
```

---

## Task 3: Verify SMMLA is Active

Confirm the compiler flag propagated correctly and the SMMLA instruction is actually being emitted.

- [ ] **Step 1: Check that `__ARM_FEATURE_MATMUL_INT8` is defined**

```bash
cmake --build build --target trix_runtime_static -- VERBOSE=1 2>&1 | \
    grep "linear_runtime" | head -5
```

Copy the full compile command shown, then run:
```bash
<that compile command> -dM -E - < /dev/null | grep MATMUL
```
Or more directly:
```bash
clang -march=armv8.6-a+i8mm -dM -E - < /dev/null | grep MATMUL
```

Expected: `#define __ARM_FEATURE_MATMUL_INT8 1`

- [ ] **Step 2: Verify SMMLA instruction appears in the compiled object**

```bash
objdump -d build/CMakeFiles/trix_runtime_static.dir/zor/src/linear_runtime.c.o \
    2>/dev/null | grep -i "smmla\|vmmlaq" | head -10
```

Or with `otool` on macOS:
```bash
otool -tv build/CMakeFiles/trix_runtime_static.dir/zor/src/linear_runtime.c.o \
    2>/dev/null | grep -i smmla | head -10
```

Expected: one or more `smmla` instructions appear in the disassembly. If zero: the `#ifdef` guard is not being satisfied — double-check the compile flag is actually being passed to that specific file.

---

## Task 4: Run Test Suite

- [ ] **Step 1: Run all tests**

```bash
cd build && ctest --output-on-failure
```

Expected: all tests PASS. No regressions. The SMMLA path adds lazy weight packing on first call — if there's a fault here it would show in `test_linear_runtime`.

- [ ] **Step 2: If `test_linear_runtime` fails with memory errors**

The SMMLA pack path (`pack_i8mm()`) has specific dimension requirements:
- `N` must be divisible by 2
- `K` must be divisible by 8

**Important:** `trix_exec_linear()` does not guard against these constraints before calling `pack_i8mm()`. If a layer has `N` odd or `K` not divisible by 8, the pack will silently produce an incorrectly-sized buffer with out-of-bounds reads — there is no automatic fallback to SDOT. The test suite uses `K=N=512` (which satisfies both), so tests pass. If you are testing with custom layer dimensions, ensure they satisfy these constraints or the SMMLA path will corrupt memory. Run with ASAN (see below) to verify.

Run with ASAN to catch any memory issues:
```bash
rm -rf build_asan
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DTRIX_ENABLE_ASAN=ON -DTRIX_ENABLE_I8MM=ON
cmake --build build_asan -j4
cd build_asan && ctest --output-on-failure
```

Expected: PASS with no ASAN errors.

---

## Task 5: Benchmark SMMLA

- [ ] **Step 1: Run bench_linear**

```bash
./build/bench_linear
```

Expected output (Apple Silicon, Release build):
```
TriX Linear Layer Throughput Benchmark
  K=512, N=512, iters=1000

  fp32:          x.x GOP/s   (1.0x baseline)
  int8-portable: xxx.x GOP/s  (xx.x x fp32)
  int8-sdot:    234.x GOP/s  (50.x x fp32)
  int8-smmla:   xxx.x GOP/s  (xx.x x fp32)   ← now populated
```

SMMLA should be ≥ SDOT. If SMMLA < SDOT, it means either:
1. The packed weight layout is causing cache pressure — expected for single-inference (batch=1) workloads
2. The compiler is not actually emitting SMMLA (check Task 3 Step 2)

Note: SMMLA's advantage is primarily for batched inference (batch=2). The current `bench_linear.c` tests batch=1 (single vector). The number may be close to SDOT or slightly below for batch=1.

- [ ] **Step 2: Record the benchmark results**

Copy the output into `docs/wiki/Benchmarks.md` under a new section:

```markdown
## SMMLA Benchmark (2026-03-21)

Platform: Apple M-series, Release build, -march=armv8.6-a+i8mm

| Backend | GOP/s | vs fp32 |
|---|---|---|
| fp32 | x.x | 1.0× |
| int8-portable | xxx.x | xx× |
| int8-sdot | 234.x | 50× |
| int8-smmla | xxx.x | xx× |
```

- [ ] **Step 3: Commit the benchmark results**

```bash
git add docs/wiki/Benchmarks.md
git commit -m "docs: record SMMLA benchmark results after I8MM enablement"
```

---

## Troubleshooting

**"error: use of undeclared identifier 'vmmlaq_s32'"**
The compiler accepted `-march=armv8.6-a+i8mm` but the ARM NEON header is not exposing the intrinsic. This happens on some Clang versions. Try adding `-mfpu=neon` or check that `arm_neon.h` version includes `vmmlaq_s32` (requires Clang ≥ 12).

**SMMLA shows 0 GOP/s or crashes**
The `pack_i8mm()` function requires `N % 2 == 0` and `K % 8 == 0`. The benchmark uses K=N=512, which satisfies both. When `__ARM_FEATURE_MATMUL_INT8` is defined, `choose_matvec()` returns `matvec_smmla` unconditionally — there is no runtime fallback to SDOT. If a chip has weights that violate the alignment constraints, `pack_i8mm()` silently produces an undersized buffer and subsequent writes go out of bounds, corrupting memory. Run with ASAN (see Task 4 Step 2) to catch any such violations.

**All tests pass but SMMLA still shows N/A in bench_linear**
The `bench_linear.c` SMMLA benchmark is gated on `__ARM_FEATURE_MATMUL_INT8`. If the benchmark binary was compiled without this flag (e.g., it's in `if(TRIX_BUILD_TOOLS)` which has separate target flags), add `target_compile_options(bench_linear PRIVATE -march=armv8.6-a+i8mm)` or let it inherit from `add_compile_options` (which is global and should apply).
