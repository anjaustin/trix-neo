# TriX/Zor Examples

A progressive learning path from "Hello XOR" to production CfC systems.

> "AI is just math, organized carefully."

## Quick Start

```bash
cd trix/zor/examples

# Build any example
gcc -O3 -I../include 01_hello_xor.c -o hello_xor
./hello_xor

# Examples requiring math library
gcc -O3 -I../include 06_tiny_mlp.c -o tiny_mlp -lm
./tiny_mlp
```

## Learning Path

| # | Example | Concepts | Prerequisites |
|---|---------|----------|---------------|
| 01 | Hello XOR | Frozen shapes, polynomial representation | None |
| 02 | Logic Gates | AND, OR, NAND, NOR as polynomials | 01 |
| 03 | Full Adder | Multi-output shapes, composition | 02 |
| 04 | Activations | ReLU, sigmoid, tanh, softmax | 01 |
| 05 | MatMul | Matrix multiplication, dot product | 04 |
| 06 | Tiny MLP | Complete neural network from scratch | 05 |
| 07 | CfC Demo | Closed-form Continuous-time cells | 06 |
| 08 | Evolution | EntroMorph genetic optimization | 07 |
| 09 | HSOS | Hollywood Squares distributed OS | 06 |

## Example Details

### 01_hello_xor.c - Your First Frozen Shape

The simplest possible example. Shows that XOR is just a polynomial:

```c
// XOR(a, b) = a + b - 2ab
float frozen_xor(float a, float b) {
    return a + b - 2.0f * a * b;
}
```

**Key insight:** This is exact for binary inputs, not an approximation.

### 02_logic_gates.c - Boolean Logic as Polynomials

All boolean gates expressed as frozen polynomials:

| Gate | Polynomial |
|------|------------|
| AND | a * b |
| OR | a + b - ab |
| NAND | 1 - ab |
| NOR | 1 - a - b + ab |
| XOR | a + b - 2ab |
| XNOR | 1 - a - b + 2ab |

### 03_full_adder.c - Composition

Shows how frozen shapes compose to build complex circuits:

```
Half Adder → Full Adder → Ripple Carry Adder
```

Demonstrates multi-output shapes (sum, carry).

### 04_activations.c - Neural Network Building Blocks

Implements common activation functions:

- **ReLU:** `x > 0 ? x : 0`
- **Sigmoid:** `1 / (1 + exp(-x))`
- **Tanh:** `(exp(x) - exp(-x)) / (exp(x) + exp(-x))`
- **Softmax:** Normalized exponentials

Shows approximations vs exact implementations.

### 05_matmul.c - The Heart of Deep Learning

Matrix multiplication as nested loops:

```c
void matmul(const float* A, const float* B, float* C, int M, int N, int K) {
    for (int i = 0; i < M; i++)
        for (int j = 0; j < N; j++) {
            float sum = 0.0f;
            for (int k = 0; k < K; k++)
                sum += A[i*K + k] * B[k*N + j];
            C[i*N + j] = sum;
        }
}
```

Understanding this is understanding 90% of AI compute.

### 06_tiny_mlp.c - Complete Neural Network

A working 2-layer MLP in ~100 lines of C:

```
Input (2) → Hidden (4) → Output (1)
```

Learns XOR from scratch. No frameworks, just frozen shapes.

**What you learn:**
- Forward propagation
- Weight organization
- Bias terms
- Activation chaining

### 07_cfc_demo.c - Closed-form Continuous-time Cells

Introduction to CfC (Closed-form Continuous-time) neural networks:

```
CfC = Gate * Candidate + (1 - Gate) * Hidden_prev
```

Key properties:
- Continuous-time dynamics
- Learnable time constants (tau)
- Stable long-term memory

### 08_evolution_demo.c - EntroMorph Genetic Optimization

Shows how to evolve CfC weights using genetic algorithms:

1. **Initialize** random population
2. **Evaluate** fitness (tracking error)
3. **Select** best performers
4. **Crossover** and mutate
5. **Repeat** for N generations

Demonstrates the "Genesis" evolution that discovered the V3 Efficient Species.

### 09_hsos_demo.c - Hollywood Squares OS

Distributed computing patterns using the HSOS protocol:

- **BubbleMachine:** Distributed sorting via compare-swap
- **ConstraintField:** CSP solving via propagation

> "Structure is meaning. The wiring determines the behavior."

## Build Options

### Basic build
```bash
gcc -O3 -I../include example.c -o example -lm
```

### With NEON optimization (ARM64)
```bash
gcc -O3 -mcpu=apple-m4 -I../include example.c -o example -lm
```

### Debug build
```bash
gcc -g -O0 -I../include example.c -o example -lm
```

## Prerequisites by Example

| Example | Headers Required | Libraries |
|---------|------------------|-----------|
| 01-03 | None | None |
| 04-06 | None | `-lm` |
| 07 | `trixc/cfc_shapes.h` | `-lm` |
| 08 | `trixc/entromorph.h` | `-lm` |
| 09 | `hsos.h` | None |

## Related Documentation

- [zor/README.md](../README.md) - Main zor documentation
- [zor/docs/THE_5_PRIMES.md](../docs/THE_5_PRIMES.md) - Irreducible operations
- [zor/docs/SOFT_CHIPS.md](../docs/SOFT_CHIPS.md) - Frozen computation concept
- [zor/foundry/README.md](../foundry/README.md) - Evolution/Genesis docs
