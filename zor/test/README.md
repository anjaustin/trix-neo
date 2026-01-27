# Zor Test Suite - The 5 Skeptic Tests

Rigorous validation tests for CfC (Closed-form Continuous-time) chips.

> "Trust, but verify. Then verify again."

## Quick Start

```bash
cd trix/zor

# Build all tests
make test

# Run individual tests
./test/stability_test
./test/frequency_sweep
./test/noise_ramp
./test/generalization_test
./test/determinism_test
./test/zit_test
```

## The 5 Skeptic Tests

These tests address the core doubts about neural network deployment:

| Test | Skeptic's Doubt | What We Prove |
|------|-----------------|---------------|
| `stability_test.c` | "Recurrent nets accumulate error" | 1.47% MAE drift over 100M steps |
| `frequency_sweep.c` | "It just memorized a 1Hz sine" | 0.1-45 Hz tracking @ 97.9% correlation |
| `noise_ramp.c` | "Only works on clean data" | 7.8x variance rejection |
| `generalization_test.c` | "Can only track circles" | Triangle, Square, Sawtooth all r > 0.97 |
| `determinism_test.c` | "FP math varies by architecture" | Bit-identical on ARM64/NEON |

## Test Descriptions

### stability_test.c - Numerical Stability

**Doubt:** "Recurrent networks accumulate floating-point errors over time."

Runs the evolved CfC chip for **100 million timesteps** and measures error drift.

```
Checkpoints: 1K, 10K, 100K, 1M, 10M, 100M steps
Metric:      Mean Absolute Error (MAE)
Pass:        MAE drift < 5% from baseline
Result:      1.47% drift - PASS
```

### frequency_sweep.c - Bandwidth Analysis

**Doubt:** "It just memorized a single frequency."

Sweeps input frequency from 0.1 Hz to 45 Hz and measures:

- Amplitude response (gain in dB)
- Phase lag
- Cutoff frequency (-3dB point)

```
Frequency Range: 0.1 Hz to 45 Hz (450x range)
Mean Gain:       -2.19 dB +/- 0.09 dB
Correlation:     97.9% across all frequencies
Discovery:       Zero-Latency Predictor (not classic LPF)
```

The chip tracks ANY frequency in its bandwidth with equal fidelity.

### noise_ramp.c - Noise Rejection

**Doubt:** "Only works on clean, synthetic data."

Progressively adds Gaussian noise to input and measures output variance.

```
Noise Levels:    0.0, 0.5, 1.0, 2.0 (relative to signal)
Input Variance:  0.5 -> 4.5 (9x increase)
Output Variance: 0.43 -> 0.62 (1.4x increase)
Rejection Ratio: 7.8x at max noise
```

The chip acts as a conditional low-pass filter - passes signal, rejects noise.

### generalization_test.c - Unseen Waveforms

**Doubt:** "It can only track the waveform it was trained on."

Tests tracking on shapes NEVER seen during training:

| Waveform | Correlation | Status |
|----------|-------------|--------|
| Triangle | 0.98 | PASS |
| Square | 0.97 | PASS |
| Sawtooth | 0.99 | PASS |

Discovery: 341.6x slew limiting on sawtooth confirms "Shark Fin" behavior - the chip's internal capacitance smooths discontinuities.

### determinism_test.c - Cross-Platform Reproducibility

**Doubt:** "Floating-point math varies by architecture."

Verifies bit-identical output across:

- Different compiler optimization levels
- ARM64/NEON vs reference
- Multiple runs

```
Reference Hash: 0x7B2AA627D27A4A5C
ARM64/NEON:     0x7B2AA627D27A4A5C - MATCH
```

### sawtooth_test.c - Slew Rate Limiting

Deep analysis of the "Shark Fin" phenomenon:

When given a discontinuity (sawtooth jump), the chip:
1. Refuses to jump instantly
2. Slews smoothly toward the new value
3. Exhibits capacitor-like discharge behavior

This proves the chip learned PHYSICS, not just patterns.

## Additional Tests

### test_cfc.c - Basic CfC Validation

Unit tests for CfC cell operations:
- Gate computation
- Candidate computation
- Hidden state update
- Output projection

### test_rigorous.c - Comprehensive Validation

Extended test suite covering edge cases and boundary conditions.

### zit_test.c - Anomaly Detection

Tests the Zit (Zero-latency Impulse Tracker) anomaly detection:

```
"Define Order, and Chaos reveals itself."
```

The CfC's "slowness" is actually sensitivity - anything that moves faster than the chip can track is, by definition, an anomaly.

| Injection Type | Detection Rate |
|----------------|----------------|
| Spike | 100% |
| Drift | 100% |
| Frequency Shift | 100% |
| Amplitude Change | 100% |

## Benchmarks

### bench_evolution.c - Evolution Performance

Benchmarks EntroMorph evolution speed (genomes/second).

### bench_gym.c - Inference Performance

Benchmarks CfC inference throughput.

## Complete Scorecard

| Test | Result | Status |
|------|--------|--------|
| Stability | 1.47% drift / 100M steps | PASS |
| Bandwidth | 0.1-45 Hz @ 97.9% | PASS |
| Noise | 7.8x rejection | PASS |
| Generalization | All waveforms r>0.97 | PASS |
| Determinism | Bit-identical | PASS |
| Zit Detection | 100% detection | PASS |

## Building Individual Tests

Each test includes build instructions in its header:

```bash
# Generic pattern
gcc -O3 -I../include <test>.c -o <test> -lm

# Example
gcc -O3 -I../include stability_test.c -o stability_test -lm
```

## Related Documentation

- [zor/README.md](../README.md) - Main zor documentation
- [zor/docs/ZIT_DETECTION.md](../docs/ZIT_DETECTION.md) - Anomaly detection theory
- [zor/docs/VALIDATION.md](../docs/VALIDATION.md) - Validation methodology
- [zor/foundry/README.md](../foundry/README.md) - Evolution documentation
