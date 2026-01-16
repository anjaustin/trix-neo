# The Foundry: Genesis-Based Soft Chip Production

> "Don't train. Grow."

The Foundry is the TriX evolution laboratory for growing Soft Chips from scratch. Instead of training neural networks with gradients, we **evolve** them through mutation and selection.

---

## Overview

**Genesis** is a genetic algorithm that evolves CfC (Closed-form Continuous-time) networks in pure C. It produces **Frozen Seeds** — self-contained `.h` files that can be deployed anywhere.

### Key Properties

| Property | Value |
|----------|-------|
| Evolution Speed | ~2,300 gen/sec |
| Seed Memory | 396 bytes |
| Genesis Time | 60 seconds |
| Output Correlation | r > 0.97 |
| Dependencies | None (libc + libm) |

---

## Directory Structure

```
foundry/
├── genesis.c           # The evolution engine
├── test_seed.c         # Seed verification harness
├── seeds/              # Generated frozen seeds
│   └── sine_seed.h     # Phase-locked sine tracker
└── README.md           # This file
```

---

## Quick Start

### Build Genesis

```bash
cd zor
make genesis
```

### Grow a Seed

```bash
# Evolve a sine tracker (default)
make genesis-sine

# Evolve an anomaly detector
make genesis-anomaly

# Evolve a controller
make genesis-control

# Evolve a pattern recognizer
make genesis-pattern
```

### Test the Seed

```bash
cd foundry
gcc -O3 test_seed.c -o test_seed -lm
./test_seed
```

---

## Fitness Kernels

Genesis uses different fitness functions for different targets:

### 1. Sine Tracker (`TARGET_SINE`)

**Goal:** Track a complex multi-frequency signal with phase-shifted correlation.

**Key Insight:** CfC neurons are **smoothers**, not predictors. They naturally produce phase-shifted versions of their input. We reward this instead of fighting it.

```c
// Cross-correlation at multiple lags
// Finds best phase shift and rewards correlation
fitness = max(correlation(output, input, lag=-5..+5))
```

**Metric:** Pearson correlation coefficient (r > 0.97 = excellent)

### 2. Anomaly Detector (`TARGET_ANOMALY`)

**Goal:** Detect sudden deviations in signal patterns.

**Key Insight:** The CfC's "slowness" IS its sensitivity. Anything that moves too fast for the tracker to follow is, by definition, an anomaly.

```c
// Reward networks that track normal, flag abnormal
fitness = normal_tracking_quality - false_positive_rate
```

**Metric:** Detection rate with low false positives

### 3. Controller (`TARGET_CONTROL`)

**Goal:** Produce control signals that minimize a setpoint error.

```c
// Reward error reduction over time
fitness = -integral_absolute_error
```

**Metric:** Integrated absolute error (lower = better)

### 4. Pattern Recognizer (`TARGET_PATTERN`)

**Goal:** Classify input patterns into categories.

```c
// Reward correct classification
fitness = accuracy
```

**Metric:** Classification accuracy

---

## The Genesis Algorithm

### 1. Population Initialization

```c
// Create diverse initial population
for (int i = 0; i < POPULATION_SIZE; i++) {
    random_topology(&population[i]);
    random_weights(&population[i]);
}
```

### 2. Evaluation Loop

```c
for (int gen = 0; gen < MAX_GENERATIONS; gen++) {
    // Evaluate fitness
    for (int i = 0; i < POPULATION_SIZE; i++) {
        population[i].fitness = evaluate(&population[i]);
    }

    // Selection (tournament)
    // Crossover (uniform)
    // Mutation (weight + topology)
}
```

### 3. Convergence Detection

Genesis stops when:
- **Target fitness achieved** (e.g., correlation > 0.98)
- **Stagnation detected** (no improvement for 50 generations)
- **Time limit reached** (default: 60 seconds)

### 4. Seed Export

The winner is frozen to a `.h` file:

```c
entro_export_seed("seeds/my_seed.h", &winner, "my_seed");
```

---

## The Variance Penalty

A critical discovery: Networks can get stuck in a **DC Trap** where they output a constant value that happens to minimize error. We kill these flat-liners:

```c
// Calculate output variance
float variance = mean(output^2) - mean(output)^2;

// Kill flat outputs
if (variance < 0.01f) {
    return 0.0f;  // Zero fitness
}
```

This causes a dramatic **Purge** in early generations (most random networks are DC-trapped), followed by a **Resurgence** as non-trivial dynamics emerge.

---

## The Phase-Lock Discovery

Traditional thinking: "Train the network to predict the next value."

**Problem:** CfC neurons are smoothers, not differentiators. They naturally lag behind their input.

**Solution:** Reward **correlation** at any phase shift, not prediction accuracy.

```c
// Instead of: error = output - future_input
// We use:     correlation = max_lag(corr(output, input, lag))

// This lets the network find its natural phase relationship
```

Result: Networks converge in 5 generations instead of 500, with r > 0.97 correlation.

---

## The Gluttony Penalty

A critical evolutionary constraint discovered while solving **High-Gain Instability**.

### The Problem: Infinite Gain

Early evolved networks discovered a "cheat code": pump internal state values to massive magnitudes (±4,000,000). This creates an implicit high-gain amplifier that achieves good short-term tracking but:

1. Accumulates floating-point error over time
2. Becomes numerically unstable over millions of steps
3. Eventually explodes or produces NaN/Inf

### The Solution: Metabolic Constraint

Just like biological neurons have metabolic limits on firing rates, we constrain network state:

```c
#define MAX_ALLOWED_STATE   5.0f   // Maximum state magnitude
#define GLUTTONY_PENALTY    100.0f // Cost per unit excess

// After evaluation, penalize gluttons
float gluttony_cost = 0.0f;
if (max_state_magnitude > MAX_ALLOWED_STATE) {
    gluttony_cost = (max_state_magnitude - MAX_ALLOWED_STATE) * GLUTTONY_PENALTY;
}
float final_fitness = fitness - gluttony_cost;
```

### The Extinction Event

When the Gluttony Penalty is first applied to an evolved population:

```
Gen 0: Best=0.00 Avg=-inf Dead=512/512  ← EXTINCTION
```

All previous high-gain species die immediately. Evolution must start fresh.

### The Efficient Species

After ~200-300 generations, a new species emerges — the **V3 Efficient Species**:

| Property | V1 (High-Gain) | V3 (Efficient) |
|----------|----------------|----------------|
| State range | ±4,200,000 | ±4.4 |
| Output range | ±210,000 | ±0.79 |
| 100M step drift | 27% | 1.5% |
| Stable? | No | Yes |

### Anti-Identity Pressure

Evolution can also discover the **Identity Trap**: output = input (a trivial pass-through). We kill these with correlation analysis:

```c
// Correlation between output and input
if (identity_corr > 0.995f) {
    return 0.0f;  // Identity = death
}
```

### Prediction vs Tracking

Combined with anti-identity pressure, we force networks to **predict** rather than track:

```c
#define PREDICT_HORIZON 3  // Predict 3 steps ahead

// Correlate output[t] with signal[t + PREDICT_HORIZON]
// This forces actual computation, not just pass-through
```

The result: A stable, efficient species that actually computes.

---

## Seed File Format

Generated seeds are self-contained C headers:

```c
// sine_seed.h — EntroMorphic Frozen Seed
//
// Target: Time-series prediction (noisy sine wave)
// Generated: Fri Jan 16 03:32:49 2026
// Fitness: 624.00
// Generation: 5

#ifndef sine_SEED_H
#define sine_SEED_H

#define sine_SEED_NODES 98
#define sine_SEED_REGS 99
#define sine_SEED_FITNESS 624.00f
#define sine_SEED_GEN 5

// Frozen topology and weights
static const uint8_t sine_seed_opcodes[98] = { ... };
static const uint16_t sine_seed_out_idx[98] = { ... };
static const uint32_t sine_seed_in_a[98] = { ... };
static const float sine_seed_values[98] = { ... };

// Execution function
static inline float sine_seed_step(float input, float* state);

// Reset function
static inline void sine_seed_reset(float* state);

#endif
```

### Usage

```c
#include "seeds/sine_seed.h"

float state[sine_SEED_REGS];
sine_seed_reset(state);

while (running) {
    float input = read_sensor();
    float output = sine_seed_step(input, state);
    // output tracks input with phase shift
}
```

---

## Telemetry

Genesis prints progress during evolution:

```
╔══════════════════════════════════════════════════════════════╗
║  GENESIS: Growing a sine tracker                             ║
╚══════════════════════════════════════════════════════════════╝

Gen 0: Best=0.00 Avg=-12.34 Dead=512/512
Gen 1: Best=0.15 Avg=0.02 Dead=234/512
Gen 2: Best=0.67 Avg=0.31 Dead=45/512
Gen 3: Best=0.89 Avg=0.54 Dead=12/512
Gen 4: Best=0.94 Avg=0.71 Dead=3/512
Gen 5: Best=0.98 Avg=0.82 Dead=0/512
>>> CONVERGED at generation 5 <<<

Exporting seed to seeds/sine_seed.h...
Done.
```

### Key Metrics

- **Best:** Highest fitness in population
- **Avg:** Average fitness
- **Dead:** Networks with zero variance (DC-trapped)

---

## Performance Tuning

### Population Size

| Size | Speed | Diversity | Recommendation |
|------|-------|-----------|----------------|
| 64 | Fast | Low | Simple problems |
| 256 | Medium | Good | Most problems |
| 1024 | Slow | High | Complex problems |

### Mutation Rates

| Rate | Effect |
|------|--------|
| Low (0.01) | Refinement mode |
| Medium (0.1) | Balanced exploration |
| High (0.3) | Escape local optima |

### Time Constants (τ)

CfC networks have time constants that control response speed:

| τ | Behavior |
|---|----------|
| 0.1 | Fast, noisy tracking |
| 1.0 | Balanced (default) |
| 10.0 | Slow, smooth tracking |

---

## Integration with Sentinel

Seeds from Genesis can be deployed in the [Sentinel daemon](../src/sentinel.c):

```c
#include "foundry/seeds/sine_seed.h"

typedef struct {
    float fast_state[sine_SEED_REGS];
    float anchor;
} DualTauDetector;

// Fast tracker: CfC chip
output = sine_seed_step(input, detector.fast_state);

// Slow anchor: EMA
detector.anchor = 0.98f * detector.anchor + 0.02f * input;

// Anomaly: fast deviates from slow
if (fabsf(output - detector.anchor) > threshold) {
    alert("DRIFT DETECTED");
}
```

---

## Future Directions

1. **Multi-objective evolution** — Optimize for both accuracy and chip size
2. **Incremental curriculum** — Start simple, gradually increase complexity
3. **Ensemble seeds** — Multiple chips voting on anomalies
4. **Hardware synthesis** — Generate Verilog from seeds

---

## References

- [CFC_ENTROMORPH.md](../docs/CFC_ENTROMORPH.md) — CfC + Evolution integration
- [ZIT_DETECTION.md](../docs/ZIT_DETECTION.md) — Anomaly detection theory
- [SOFT_CHIPS.md](../docs/SOFT_CHIPS.md) — Soft chip concept
- Hasani et al., "Closed-form Continuous-time Neural Networks" (2021)

---

*"The seed that grew in 60 seconds."*
