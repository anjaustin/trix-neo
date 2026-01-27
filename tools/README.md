# TriX Toolchain

**The soft chip forge. Define once. Forge anywhere. Trust everywhere.**

---

## Quick Start

```bash
# Build the toolchain
make

# Create a new soft chip
../bin/trix init my_chip

# Forge to portable C
../bin/trix forge my_chip.trix --target=c

# Build and test
cd output && make && ./my_chip_test
```

---

## Commands

### `trix init <name>`

Create a new soft chip specification.

```bash
$ trix init gesture_detector

Created: gesture_detector.trix

Next steps:
  1. Edit gesture_detector.trix to define your signatures
  2. Run: trix forge gesture_detector.trix --target=c
  3. Build: cd output && make
  4. Test: ./output/gesture_detector_test
```

**Output:** A `.trix` YAML file with template signatures.

---

### `trix forge <spec> [options]`

Compile a soft chip specification to native code.

```bash
$ trix forge gesture_detector.trix --target=neon

╭──────────────────────────────────────────────────────────────╮
│  TriX Forge v0.1.0                                           │
│  Soft Chip: gesture_detector                                 │
╰──────────────────────────────────────────────────────────────╯

Parsing spec.......... ✓
Validating shapes..... ✓ (3 shapes)
Loading signatures.... ✓ (2 signatures)
Target: ARM NEON

Generating code:
  output/gesture_detector.h
  output/gesture_detector.c
  output/gesture_detector_test.c
  output/Makefile

Output directory: output/

To build and run:
  cd output && make && ./gesture_detector_test
```

**Options:**

| Flag | Description | Default |
|------|-------------|---------|
| `--target=<t>` | Target platform | `c` |
| `--output=<dir>` | Output directory | `output` |
| `--no-test` | Skip test harness generation | (generate) |

**Targets:**

| Target | Description |
|--------|-------------|
| `c` | Portable C99, runs anywhere |
| `neon` | ARM NEON intrinsics (ARM64) |
| `avx2` | Intel AVX2 intrinsics (x86-64) |
| `wasm` | WebAssembly with SIMD |
| `verilog` | RTL for FPGA/ASIC |

---

### `trix verify <spec>`

Generate a verification report proving determinism and bounds.

```bash
$ trix verify gesture_detector.trix

╭──────────────────────────────────────────────────────────────╮
│  TriX Verification Report                                    │
│  Soft Chip: gesture_detector                                 │
╰──────────────────────────────────────────────────────────────╯

DETERMINISM
├── Pure functions only............ ✓ PROVEN
├── No floating-point variance..... ✓ PROVEN
├── No platform-dependent ops...... ✓ PROVEN
└── Status: DETERMINISTIC

REPRODUCIBILITY
├── Same input → same output....... ✓ PROVEN
├── Platform-independent........... ✓ PROVEN
└── Status: REPRODUCIBLE

BOUNDS
├── Memory: 64 bytes (fixed)....... ✓ BOUNDED
├── Time: O(2) signatures.......... ✓ BOUNDED
├── Recursion: None................ ✓ SAFE
└── Status: BOUNDED

COVERAGE
├── Signatures defined: 2
├── Default handler: "unknown"
└── Status: COMPLETE

╭──────────────────────────────────────────────────────────────╮
│  VERIFICATION PASSED                                         │
╰──────────────────────────────────────────────────────────────╯
```

---

### `trix trace <spec> --input=<file>`

Debug inference step-by-step, showing Hamming distances and activation status.

```bash
$ trix trace gesture_detector.trix --input=data.bin

╭──────────────────────────────────────────────────────────────╮
│  TriX Trace                                                  │
│  Soft Chip: gesture_detector                                 │
╰──────────────────────────────────────────────────────────────╯

Input: data.bin (512 bits)

ZIT DETECTION
┌─────────────────┬──────────┬───────────┬────────┐
│ Signature       │ Distance │ Threshold │ Status │
├─────────────────┼──────────┼───────────┼────────┤
│ example_a       │        8 │        64 │   ✓    │
│ example_b       │      504 │        64 │   ✗    │
└─────────────────┴──────────┴───────────┴────────┘

RESULT: example_a (distance=8, margin=56)
```

**Options:**

| Flag | Description |
|------|-------------|
| `--input=<file>` | 64-byte binary file (512 bits) |

Without `--input`, uses zeros.

---

## The .trix Spec Format

Soft chips are defined in human-readable YAML:

```yaml
# my_chip.trix

softchip:
  name: my_chip
  version: 1.0.0
  description: What this chip does

state:
  bits: 512
  layout: cube  # or: flat

shapes:
  - xor
  - relu
  - sigmoid

signatures:
  class_a:
    pattern: "0xAAAA..."  # 128 hex chars = 64 bytes
    threshold: 64
    shape: sigmoid

  class_b:
    pattern: "base64:..."  # Base64 encoded
    threshold: 48
    shape: relu

inference:
  mode: first_match  # or: all_match
  default: unknown
```

### Available Shapes

| Shape | Description |
|-------|-------------|
| `xor` | Exclusive OR: `a + b - 2ab` |
| `and` | Logical AND: `a * b` |
| `or` | Logical OR: `a + b - ab` |
| `not` | Logical NOT: `1 - a` |
| `nand` | NOT AND: `1 - ab` |
| `nor` | NOT OR: `1 - a - b + ab` |
| `xnor` | Exclusive NOR: `1 - a - b + 2ab` |
| `relu` | ReLU: `max(0, x)` |
| `sigmoid` | Sigmoid: `1 / (1 + e^-x)` |
| `identity` | Pass-through: `x` |

---

## Generated Code Structure

After `trix forge`, you get:

```
output/
├── my_chip.h          # API header
├── my_chip.c          # Implementation
├── my_chip_test.c     # Test harness + benchmark
└── Makefile           # Build script
```

### Generated API

```c
#include "my_chip.h"

// Initialize
my_chip_state_t state;
my_chip_init(&state);

// Infer
uint8_t input[64] = { ... };
int result = my_chip_infer(&state, input);

// Result is signature index, or -1 for default
const char* label = my_chip_get_label(result);
```

---

## Building the Toolchain

```bash
cd tools
make

# Output: ../bin/trix
```

### Requirements

- GCC or Clang with C11 support
- No external dependencies

### Source Files

```
tools/
├── Makefile
├── include/
│   ├── softchip.h     # Data structures
│   └── codegen.h      # Code generator interface
└── src/
    ├── trix.c         # CLI entry point
    ├── softchip.c     # .trix parser
    └── codegen.c      # Code generator
```

---

## Performance

Generated soft chips achieve:

| Metric | Value |
|--------|-------|
| State size | 64 bytes (L1 cache resident) |
| Code size | 1-3 KB |
| Throughput | 10-100M inferences/sec |
| Latency | 10-100 ns |

---

## lnn2trix.py — LNN to TriX Compiler

Converts trained Liquid Neural Network / CfC weights into frozen TriX C code.

### Usage

```bash
# From PyTorch checkpoint
python lnn2trix.py model.pt --name=my_chip

# From NumPy weights (.npz)
python lnn2trix.py weights.npz --name=driver

# With fixed time step (precomputes decay for faster inference)
python lnn2trix.py model.pt --name=sensor --dt=0.01 --fixed

# Generate example weights for testing
python lnn2trix.py --example --name=test_chip --input-dim=4 --hidden-dim=8 --output-dim=3
```

### Arguments

| Argument | Description |
|----------|-------------|
| `input` | Input file (`.pt`, `.pth`, or `.npz`) |
| `--name` | **Required.** Chip name (used in generated code) |
| `--output`, `-o` | Output directory (default: current) |
| `--dt` | Fixed time step (enables decay precomputation) |
| `--fixed` | Use fixed time step mode (requires `--dt`) |
| `--example` | Generate random example weights |
| `--input-dim` | Input dimension for example (default: 4) |
| `--hidden-dim` | Hidden dimension for example (default: 8) |
| `--output-dim` | Output dimension for example (default: 3) |

### Output Files

| File | Description |
|------|-------------|
| `{name}_weights.h` | Frozen weight arrays as C constants |
| `{name}_chip.h` | Ready-to-use chip API with convenience functions |

### Example Workflow

```python
# train.py — Train with PyTorch/ncps
import torch
from ncps.torch import CfC
from ncps.wirings import AutoNCP

wiring = AutoNCP(8, 3)  # 8 hidden, 3 output
model = CfC(4, wiring)  # 4 input

# Train...
for epoch in range(100):
    # training loop
    pass

torch.save(model.state_dict(), 'trained.pt')
```

```bash
# Freeze to TriX
python lnn2trix.py trained.pt --name=detector --dt=0.1 --fixed
```

```c
// deploy.c — Use frozen chip
#include "detector_chip.h"

int main() {
    float h[DETECTOR_HIDDEN_DIM] = {0};
    float x[DETECTOR_INPUT_DIM];
    float probs[DETECTOR_OUTPUT_DIM];

    while (1) {
        read_sensors(x);
        detector_step(x, h, h);
        detector_classify(h, probs);
    }
}
```

### Weight File Formats

**PyTorch (`.pt`, `.pth`)** — Searches for common naming patterns:
- `gate.weight`, `wiring.gate_weight`, `ff1.weight`
- `candidate.weight`, `wiring.cand_weight`, `ff2.weight`
- `output.weight`, `readout.weight`, `fc.weight`
- `tau`, `time_constant`, `timescale`

**NumPy (`.npz`)** — Expected keys:
- `W_gate`, `b_gate`, `W_cand`, `b_cand`
- `W_out`, `b_out`, `tau`

---

## lnn2trix_forge.py — NEON Forge Compiler (235 GOP/s)

High-performance variant of lnn2trix.py that generates **forged NEON soft-chips**.

Unlike the standard compiler which outputs float weights, this compiler:
1. **Quantizes** weights to ternary {-1, 0, +1} (BitNet b1.58 style)
2. **Packs** weights into Block-8-K64 layout for NEON SDOT
3. **Generates** complete, compilable C headers with embedded kernels

### Usage

```bash
# From PyTorch checkpoint
python lnn2trix_forge.py model.pt --name=my_chip

# From NumPy weights with explicit dimensions
python lnn2trix_forge.py weights.npz --name=driver --dt=0.01

# Generate synthetic example and forge it
python lnn2trix_forge.py --example --name=test_chip --input-dim=64 --hidden-dim=32

# Specify output directory
python lnn2trix_forge.py model.pt --name=chip --output=/path/to/output
```

### Arguments

| Argument | Description |
|----------|-------------|
| `input` | Input file (`.pt`, `.pth`, or `.npz`) |
| `--name` | **Required.** Chip name (used in generated code) |
| `--output`, `-o` | Output directory (default: current) |
| `--dt` | Fixed time step (enables decay precomputation) |
| `--example` | Generate random example weights |
| `--input-dim` | Input dimension for example (default: 64) |
| `--hidden-dim` | Hidden dimension for example (default: 32) |
| `--output-dim` | Output dimension for example (default: 10) |

### Output

Single file: `{name}_forged.h` containing:
- Frozen ternary weights in Block-8-K64 layout
- NEON SDOT matvec kernels
- Complete CfC cell and forward functions
- Softmax output layer (if output weights present)

### Performance

| Metric | Value |
|--------|-------|
| Single-vector throughput | 178-189 GOP/s |
| Batch inference (I8MM) | 235 GOP/s |
| Memory compression | 16-32x vs float32 |
| Target chip | Apple M4 / ARM64 |

### Example Workflow

```python
# train.py — Train with PyTorch/ncps
import torch
from ncps.torch import CfC
from ncps.wirings import AutoNCP

wiring = AutoNCP(32, 10)  # 32 hidden, 10 output
model = CfC(64, wiring)   # 64 input

# Train...
torch.save(model.state_dict(), 'trained.pt')
```

```bash
# Forge to NEON soft-chip
python lnn2trix_forge.py trained.pt --name=detector
```

```c
// deploy.c — Use forged chip (193+ GOP/s)
#include "detector_forged.h"

int main() {
    int8_t h[DETECTOR_HIDDEN_DIM] = {0};
    int8_t x[DETECTOR_INPUT_DIM];
    int32_t y[DETECTOR_OUTPUT_DIM];
    
    while (1) {
        read_sensors_quantized(x);
        detector_forward(x, h, h);
        detector_output(h, y);
    }
}
```

```bash
# Build with NEON optimization
clang -O3 -mcpu=apple-m4 -march=armv8.6-a+dotprod deploy.c -o deploy
```

### Block-8-K64 Layout

Weights are packed for optimal NEON SDOT throughput:

```
Block-8-K64: 8 output channels × 64 K-elements per iteration

  K=0..63     K=64..127   ...
  ┌─────────┬─────────┬─────────┐
  │  N=0..7 │  N=0..7 │  ...    │  Block 0
  ├─────────┼─────────┼─────────┤
  │ N=8..15 │ N=8..15 │  ...    │  Block 1
  └─────────┴─────────┴─────────┘

Each block: 8 rows × 64 columns = 512 bytes
SDOT: 4 int8 × 4 int8 → int32, 16 ops per instruction
```

### Ternary Quantization

Uses BitNet b1.58 method:

```python
# Scale by absolute mean, round to {-1, 0, +1}
gamma = np.abs(weights).mean()
scaled = weights / gamma
quantized = np.round(np.clip(scaled, -1, 1))
```

Typical weight distribution:
- `+1`: 25-35%
- `0`: 30-50%
- `-1`: 25-35%

### Comparison: lnn2trix.py vs lnn2trix_forge.py

| Feature | lnn2trix.py | lnn2trix_forge.py |
|---------|-------------|-------------------|
| Weight format | float32 | ternary int8 |
| Memory | ~4x larger | 16-32x smaller |
| Throughput | ~10 GOP/s | 178-235 GOP/s |
| Platform | Any | ARM64 w/ NEON |
| Accuracy | Full precision | ~0.001 max error |

Use `lnn2trix.py` for:
- Development and debugging
- Non-ARM platforms
- Maximum precision requirements

Use `lnn2trix_forge.py` for:
- Production deployment on Apple Silicon
- Battery-constrained devices
- Real-time inference (< 1ms latency)

---

*"Define once. Forge anywhere. Trust everywhere."*
