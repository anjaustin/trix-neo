# Soft Chips UX — NODES

*Lincoln Manifold Method: Concept extraction*
*The tangible elements of trusted computation UX.*

---

## Node 1: The Core Promise

```
"Define once. Forge anywhere. Trust everywhere."
```

This is the headline. Everything in the UX supports this promise:
- **Define once**: Single spec format (.trix)
- **Forge anywhere**: Multiple compilation targets
- **Trust everywhere**: Deterministic, verifiable behavior

---

## Node 2: The Four Personas

| Persona | Needs | UX Priority |
|---------|-------|-------------|
| **Embedded Developer** | Cycles, bytes, power | Minimal footprint, C output |
| **ML Engineer** | Easy conversion | Import from ONNX/TFLite |
| **Systems Integrator** | Drop-in API | Clear headers, minimal deps |
| **Safety-Critical Dev** | Proofs, audits | Verification reports, certs |

Different users, different entry points, same core system.

---

## Node 3: The Workflow

```
┌────────┐   ┌───────┐   ┌───────┐   ┌──────┐   ┌────────┐   ┌─────────┐
│ DEFINE │ → │ TRAIN │ → │ FORGE │ → │ TEST │ → │ DEPLOY │ → │ MONITOR │
└────────┘   └───────┘   └───────┘   └──────┘   └────────┘   └─────────┘
    │            │           │          │           │            │
  .trix     signatures    C/NEON     validate    device      observe
   spec      learned      output    correctness   ship       runtime
```

Each stage has a clear artifact and tooling.

---

## Node 4: The Spec Format (.trix)

Human-readable YAML:

```yaml
softchip:
  name: gesture_detector
  version: 1.0.0

state:
  bits: 512
  layout: cube  # 8×8×8

shapes:
  - xor
  - relu
  - sigmoid

signatures:
  swipe_left:
    pattern: "base64:..."
    threshold: 64
  swipe_right:
    pattern: "base64:..."
    threshold: 64

inference:
  mode: first_match
  default: unknown
```

Properties:
- Readable by humans
- Versionable in git
- Diffable
- Self-documenting

---

## Node 5: The CLI Ecosystem

```
trix-forge     Compile specs to platform targets
trix-verify    Generate verification reports
trix-trace     Debug inference step-by-step
trix-viz       Visualize cube and activations
trix-bench     Measure throughput and latency
trix-train     Learn signatures from examples
trix-convert   Import from ONNX, TFLite
trix-serve     HTTP API for inference
```

Unix philosophy: each tool does one thing well.

---

## Node 6: The Forge Command

```bash
$ trix forge spec.trix --target=neon

Parsing spec... ✓
Validating shapes... ✓
Generating NEON code... ✓

Output:
  my_chip.c       (NEON-optimized)
  my_chip.h       (API header)
  my_chip_test.c  (validation)
  Makefile        (build)
```

Targets supported:
- `c` — Pure portable C
- `neon` — ARM NEON intrinsics
- `sve2` — ARM SVE2
- `avx512` — Intel AVX-512
- `wasm` — WebAssembly SIMD
- `verilog` — FPGA/ASIC RTL

Same spec → multiple outputs → same behavior.

---

## Node 7: The Runtime API

Minimal C interface:

```c
#include "my_chip.h"

// Initialize
trix_chip_t chip;
trix_init(&chip, my_chip_spec);

// Infer (one call, deterministic)
trix_result_t result = trix_infer(&chip, input);

// Result contains:
//   result.match     — which signature matched
//   result.distance  — Hamming distance
//   result.threshold — activation threshold
```

Three functions: `init`, `infer`, `free`. That's it.

---

## Node 8: Trust as Tangible UX

The verification report makes trust visible:

```
╭──────────────────────────────────────────────╮
│  TriX Verification Report                    │
╰──────────────────────────────────────────────╯

Determinism: ✓ PROVEN
Reproducibility: ✓ PROVEN
Memory bounded: ✓ 64 bytes
Time bounded: ✓ O(n) signatures

Certificate: my_chip.cert
```

Trust isn't claimed. It's demonstrated.

---

## Node 9: The Trace Experience

Debugging shows every bit:

```bash
$ trix trace spec.trix --input=data.bin

Zit Detection:
  sig_a: distance=127 threshold=64  ✗
  sig_b: distance=42  threshold=48  ✓ MATCH

Shape Activation:
  sig_b → sigmoid(input) → 0.73

Output: sig_b (margin: 6 bits)
```

No black box. Every decision is traceable.

---

## Node 10: The Visual Experience

Cube visualization:

```
Resonance Cube (8×8×8):

Layer 0:        Layer 4:        Layer 7:
░░░░░░░░        ██░░░░██        ░░██░░░░
░░██░░░░        ░░██░░░░        ░░░░░░░░
░░░░░░░░   ...  ░░░░░░░░   ...  ░░░░██░░
...             ...             ...

Hamming Distance: 42 bits
Activation: ✓
```

See the geometry. Understand spatially.

---

## Node 11: Minimum Viable Path

Four commands from zero to deployed:

```bash
$ trix init my_chip.trix              # Create spec
$ trix train my_chip.trix --data=./   # Learn signatures
$ trix forge my_chip.trix --target=neon  # Compile
$ ./my_chip_test                      # Validate
```

Low barrier to entry. High ceiling for optimization.

---

## Node 12: Platform-Specific Output

### ARM NEON

```c
// Generated: NEON intrinsics
uint8x16_t xor0 = veorq_u8(state[0], sig[0]);
int dist = vaddvq_u8(vcntq_u8(xor0)) + ...;
```

### WebAssembly

```c
// Generated: WASM SIMD
v128_t xor0 = wasm_v128_xor(state[0], sig[0]);
int dist = wasm_i8x16_bitmask(xor0) + ...;
```

### Verilog

```verilog
// Generated: RTL
wire [511:0] xor_result = state ^ signature;
wire [9:0] distance = popcount(xor_result);
wire activate = (distance < threshold);
```

Same spec. Platform-optimal output.

---

## Summary: The Twelve Nodes

1. **Core promise**: Define once, forge anywhere, trust everywhere
2. **Four personas**: Embedded, ML, integrator, safety-critical
3. **Six-stage workflow**: Define → Train → Forge → Test → Deploy → Monitor
4. **Spec format**: Human-readable .trix YAML
5. **CLI ecosystem**: Unix-style single-purpose tools
6. **Forge command**: Multi-target compilation
7. **Runtime API**: Three functions (init, infer, free)
8. **Trust as UX**: Verification reports and certificates
9. **Trace experience**: Bit-level debugging
10. **Visual experience**: Cube and signature visualization
11. **Minimum viable path**: Four commands to deployed
12. **Platform outputs**: NEON, WASM, Verilog from same spec

---

*Nodes extracted.*
*Time to pressure test...*
