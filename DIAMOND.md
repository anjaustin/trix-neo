# DIAMOND — Repository Audit

> A clear-eyed assessment of what is novel, useful, understated, and fluff.
> Written 2026-03-19.

---

## Executive Summary

TriX is a legitimately novel, production-quality codebase built on a single powerful idea:
**frozen computation + learned routing = deterministic AI.**

The runtime is production-ready. The architecture is sound. The gaps are known and fixable.

- **Production runtime (`zor/`):** 95% complete, hardened, ASAN-clean
- **Toolchain (`tools/`):** 80% complete, works for common cases
- **Documentation:** Exceptional quality, high volume
- **Testing:** 70+ tests, determinism/security/generalization coverage
- **Overall:** 8.2/10 — real engineering, not hype

---

## What's Novel

### 1. Frozen Computation as a First-Class Design Principle
The core insight: shapes (activation functions) are frozen after training, making inference
fully deterministic, verifiable, and portable. Not unique in the industry — Qualcomm and
others do this — but the principled execution here is clean and complete.

### 2. Hollywood Squares OS (HSOS)
**`zor/src/hsos.c` — 1,063 lines, fully working.**

A deterministic message-passing microkernel. Features:
- Master + 8-worker star topology
- 16-byte fixed-format message frames
- CSWAP protocol for distributed bubble sort
- Constraint satisfaction (CSP) via domain bitset elimination
- Full execution recording and deterministic replay

The CSWAP protocol and causality-based computation model are clever. This is the most
intellectually interesting piece in the entire repo. Perfect for safety-critical systems
where you need to prove "this execution is identical to the validated execution."

**Novelty: 7/10** — The design principle is sound and well-executed.

### 3. CfC Shapes in Frozen Form
Using closed-form Liquid Time Constant cells (Hasani et al. 2020) instead of ODE
integration is elegant. Reduces a time-varying recurrent network to a single forward
pass, fully compatible with the frozen-computation model.

```c
// h(t) = (1 - gate) * h_prev * decay + gate * candidate
// All components frozen. No solver. No integration steps.
```

### 4. Ternary Weight + NEON SMMLA Path
Compiling `{-1, 0, +1}` weights to ARM SMMLA instructions in `linear_forge.c` is a
real embedded performance trick. Underutilized and underadvertised.

---

## What's Useful (Production-Ready)

### Core Inference Engine (`zor/src/runtime.c`, 410 lines)
Hamming distance matching via SIMD popcount. Correct, optimized, and clean.

- **ARM NEON:** `vcnt` + horizontal adds — fully implemented
- **x86 AVX2:** `_mm256_popcnt_u8` + `sad_epu8` — fully implemented
- **Portable C:** fallback — fully implemented
- Runtime dispatch selects the best available path

### Production Hardening
Unusually good for a research project:

| Component | File | Lines | Status |
|---|---|---|---|
| Error handling | `errors.c` | 350 | 70+ error codes, full context |
| Logging | `logging.c` | 340 | 5 levels, thread-safe |
| Input validation | `validation.c` | 700 | Bounds checking, sanitization |
| Thread safety | `thread.c` | 750 | Mutexes, atomics |
| Safe memory | `memory.c` | 200 | ASAN-clean |

### `.trix` Parser + C/NEON Code Generator
Works for the common cases. Hardened with proper error handling. The pipeline
`parse → codegen → compile` is coherent and functional.

### Build System + Test Suite
- CMake 3.15+, AddressSanitizer, ThreadSanitizer, UBSanitizer, coverage (lcov)
- 70+ tests: unit, integration, determinism, generalization, stability, security
- CTest integration

### Python Bindings
- `tools/python/trix.py` — ctypes wrapper (fixed 2026-03-19: proper `_TrixResult`
  and `_TrixChipInfo` ctypes structures, correct `trix_infer` return type, correct
  `trix_info` struct parsing)
- `tools/python/trix_python.cpp` — pybind11 C++ bindings

---

## What's Understated

These are the items that deserve more attention than they currently receive.

### 1. HSOS — The Most Novel Piece, Least Advertised
HSOS is buried in `zor/src/` and presented as a demo/example. It is actually the most
technically distinctive thing in the repo. A working deterministic distributed microkernel
with recording/replay is genuinely valuable for:

- Safety-critical systems (automotive, medical, aerospace)
- Certifiable AI (FDA, ISO 26262, DO-178C)
- Formal verification (deterministic execution = provable traces)
- Edge inference with auditability requirements

**Action:** HSOS should be a centerpiece, not a footnote.

### 2. Ternary Weight + SMMLA Path
`tools/src/linear_forge.c` compiles ternary `{-1, 0, +1}` weights to ARM NEON SMMLA
instructions. This is a real embedded performance optimization that most ML frameworks
don't do cleanly. It gets one paragraph in the docs.

**Action:** Benchmark this path explicitly. Quantify the speedup vs. fp32. Make it a
headline feature for embedded/edge positioning.

### 3. Determinism Tests
The test suite includes `determinism_test.c` — bit-exact reproducibility verification.
This is above average and directly supports certification claims, but it's not called
out prominently anywhere.

**Action:** Surface this in the README and any certification/safety docs. It's a
differentiator.

### 4. Parser Security Tests
`test_parser_security.c` tests malformed input, corrupted data, and adversarial specs.
Most projects at this stage don't have this. It's a signal of production maturity.

**Action:** Document what attack classes are tested. Consider fuzzing (libFuzzer/AFL).

---

## What's Fluff

### 1. GPU Code (`cortex.cu`, `visor.cu`, `visor_bridge.cu`, `visor_reflex.cu`, `visor_fallback.cu`)
~1,500 lines of CUDA stubs. None compile. None link into the library. CMakeLists.txt
does not include them. `runtime.h` has no GPU functions.

**Assessment:** Research exploration that didn't make it to production. Not part of the product.

### 2. Multi-Target Code Generation Claims
`codegen.c` has switch-case stubs for Verilog, WebAssembly, and TFLite. None are
implemented. Only C and NEON partially work.

**Assessment:** Aspirational. Remove the stubs or clearly gate them as `// TODO`.

### 3. "Addressable Intelligence" Framework
`docs/ADDRESSABLE_INTELLIGENCE.md` and related philosophy docs describe a theoretical
framework where "intelligence is an address, not a process." Intellectually interesting.
Zero implementation in the runtime.

**Assessment:** Vision, not code. Fine to keep as research notes; should not be cited
as a feature.

### 4. Periodic Table of Shapes
Beautiful theoretical framework for categorizing frozen shapes. Well-written in docs.
The library implements ~10 shapes; the "table" describes 100+.

**Assessment:** Great idea, 10% built. Don't claim the table until the shapes exist.

### 5. Strategy / Vision Documents
`BIRDS_EYE_VIEW.md` (91KB), `STRATEGY.md`, `PRODUCTS_AND_MOATS.md`, journal entries.
High quality prose. High volume. Not the product.

**Assessment:** Useful internally for alignment. Should not be mistaken for technical
deliverables.

---

## Architecture (What Actually Runs)

```
User Application
      │
      ▼
TRIX RUNTIME API  (zor/include/trixc/runtime.h)
  trix_load()   ──► parse .trix spec
  trix_infer()  ──► Hamming distance match
  trix_info()   ──► chip metadata
      │
      ▼
INFERENCE ENGINE  (zor/src/runtime.c)
  popcount64_neon()      ◄─ ARM SIMD
  popcount64_avx2()      ◄─ x86 SIMD
  popcount64_portable()  ◄─ C fallback
      │
      ▼
SHAPES LIBRARY  (zor/include/trixc/shapes.h, cfc_shapes.h, onnx_shapes.h)
  shape_xor / shape_and / shape_relu / shape_sigmoid  (frozen math)
  cfc_cell()   (Liquid Time Constants, closed-form)
  matmul()     (dense layers, ternary weights)
      │
      ▼
DISTRIBUTED KERNEL  (zor/src/hsos.c)  ← optional, standalone
  Message passing (16-byte frames, star topology)
  CSWAP distributed sort
  CSP constraint solver
  Recording / replay
      │
      ▼
PRODUCTION SUPPORT  (errors.c, logging.c, validation.c, thread.c, memory.c)
      │
      ▼
TOOLCHAIN  (tools/src/)
  softchip_parse()    ── .trix YAML parser
  codegen_generate()  ── C / NEON code generator
  linear_forge()      ── ternary weight → SMMLA compiler
  cfc_forge()         ── CfC compiler
```

---

## Completeness Matrix

| Component | Status | % Done | Notes |
|---|---|---|---|
| Core inference (popcount + matching) | ✅ Real | 100% | Fully working, SIMD-optimized |
| ARM NEON popcount | ✅ Real | 100% | Correct implementation |
| x86 AVX2 popcount | ✅ Real | 100% | Correct implementation |
| `.trix` parser | ✅ Real | 85% | Works, some edge cases missing |
| HSOS microkernel | ✅ Real | 95% | Fully working, well-tested |
| CfC shapes | ✅ Real | 90% | Math correct |
| Error handling | ✅ Real | 90% | Production-grade |
| Logging | ✅ Real | 85% | Thread-safe, configurable |
| Input validation | ✅ Real | 90% | Bounds checking, sanitization |
| CMake build system | ✅ Real | 100% | Clean, sanitizer support |
| Unit tests | ✅ Real | 80% | 70+ tests, good coverage |
| Python bindings | ✅ Real | 90% | ctypes + pybind11, fixed |
| Code generator (C/NEON) | ⚠️ Partial | 60% | Basic cases work, no optimization passes |
| Ternary → SMMLA compiler | ⚠️ Partial | 70% | Works, needs benchmarking |
| GPU support (CUDA) | ❌ Stub | 0% | Not compiled, not linked |
| Verilog codegen | ❌ Stub | 0% | Placeholder only |
| WebAssembly codegen | ❌ Stub | 0% | Placeholder only |
| TFLite format | ❌ Stub | 0% | Placeholder only |
| Addressable Intelligence | ❌ Docs only | 0% | Philosophy, no implementation |
| Periodic Table of Shapes | ❌ Partial | 10% | Framework exists, shapes don't |

---

## Next Actions

### High Value, Low Effort
1. **Benchmark HSOS** — measure message throughput, latency, determinism overhead
2. **Benchmark ternary SMMLA path** — quantify vs. fp32 on real ARM hardware
3. **Surface determinism tests** — promote to README, tie to certification narrative
4. **Fuzz the parser** — libFuzzer or AFL on `softchip_parse()`, build on existing security tests

### High Value, Higher Effort
5. **Make HSOS a centerpiece** — dedicated docs, examples, positioning for safety-critical use
6. **Complete code generator** — finish C/NEON, add optimization passes, defer Verilog/Wasm
7. **Platform testing** — Windows, Cortex-M4, verify ARM64 performance claims

### Cleanup
8. **Remove or clearly gate GPU stubs** — they create false impressions
9. **Mark Verilog/Wasm as `// NOT IMPLEMENTED`** in codegen.c
10. **Separate vision docs from technical docs** — keep `docs/` as technical reference only

---

*Generated from full codebase audit, 2026-03-19.*
